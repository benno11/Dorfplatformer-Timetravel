#include "SandboxUpdates.h"
#include "SandboxVersion.h"
#include "UpdateManifest.h"
#include "BuildInfo.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
namespace SandboxUpdates {
namespace {
constexpr size_t maximumManifest = 1024*1024;
void copy(char* destination,size_t capacity,const std::string& text) {
    const size_t count=std::min(capacity-1,text.size());
    std::memcpy(destination,text.data(),count);destination[count]='\0';
}
Checker& checker(){static Checker value;return value;}
std::mutex& mutex(){static std::mutex value;return value;}
}
Result evaluate(const std::string& body,const std::string& gameId,const std::string& sandboxVersion) {
    Result result;
    try {
        if(body.empty())throw std::runtime_error("Could not load the update manifest. Check your connection.");
        if(body.size()>maximumManifest)throw std::runtime_error("The update manifest exceeds the size limit.");
        const auto manifest=nlohmann::json::parse(body);
        result.latestGame=manifest.at("version").get<std::string>();
        result.notes=manifest.value("notes",std::string());
        result.latestSandbox=manifest.value("sandbox_version",std::string());
        const int gameOrder=AppUpdate::compareVersions(gameId,AppUpdate::versionId(manifest));
        const int sandboxOrder=result.latestSandbox.empty()?0:AppUpdate::compareVersions(sandboxVersion,result.latestSandbox);
        if(gameOrder<0||sandboxOrder<0) {
            if(gameOrder>0||sandboxOrder>0)throw std::runtime_error("The update bundle would downgrade the game or sandbox.");
            result.url=AppUpdate::installerUrl(manifest);
            result.state=DF_UPDATE_AVAILABLE;
            result.detail="A new game/sandbox bundle is available.";
        } else {
            result.state=DF_UPDATE_CURRENT;result.detail="The game and sandbox are up to date.";
        }
    }catch(const std::exception& error){result.state=DF_UPDATE_ERROR;result.detail=error.what();result.url.clear();}
    return result;
}
void Checker::configure(std::string url,std::string gameId,Fetch fetch) {
    shutdown();url_=std::move(url);gameId_=std::move(gameId);fetch_=std::move(fetch);
    result_={};result_.state=url_.empty()?DF_UPDATE_NOT_CONFIGURED:DF_UPDATE_READY;
}
bool Checker::check() {
    poll();if(pending_.valid())return false;
    if(url_.empty()){result_.state=DF_UPDATE_NOT_CONFIGURED;result_.detail="No update manifest URL is configured.";return false;}
    if(!AppUpdate::isWebUrl(url_)||!fetch_){result_.state=DF_UPDATE_ERROR;result_.detail="The update manifest must use HTTP or HTTPS.";return false;}
    result_={};result_.state=DF_UPDATE_CHECKING;result_.detail="Checking game and sandbox updates...";
    auto fetch=fetch_;auto url=url_;auto gameId=gameId_;
    pending_=std::async(std::launch::async,[fetch=std::move(fetch),url=std::move(url),gameId=std::move(gameId)]{
        try {return evaluate(fetch(url),gameId,GameSandbox::version);}
        catch(const std::exception& error){Result result;result.state=DF_UPDATE_ERROR;result.detail=error.what();return result;}
        catch(...){Result result;result.state=DF_UPDATE_ERROR;result.detail="The update request failed.";return result;}
    });
    return true;
}
void Checker::poll() {
    if(pending_.valid()&&pending_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)result_=pending_.get();
}
DFSandboxUpdateStatus Checker::snapshot() {
    poll();DFSandboxUpdateStatus out{};out.struct_size=sizeof(out);out.state=result_.state;
    copy(out.current_sandbox,sizeof(out.current_sandbox),GameSandbox::version);
    copy(out.latest_game,sizeof(out.latest_game),result_.latestGame);
    copy(out.latest_sandbox,sizeof(out.latest_sandbox),result_.latestSandbox);
    copy(out.detail,sizeof(out.detail),result_.detail);copy(out.notes,sizeof(out.notes),result_.notes);
    return out;
}
std::string Checker::downloadUrl(){poll();return result_.state==DF_UPDATE_AVAILABLE?result_.url:std::string();}
void Checker::shutdown(){if(pending_.valid()){pending_.wait();(void)pending_.get();}}
void configure(std::string url,std::string gameId,Fetch fetch){std::lock_guard<std::mutex> lock(mutex());checker().configure(std::move(url),std::move(gameId),std::move(fetch));}
void configureFile(const std::filesystem::path& path,Fetch fetch) {
    std::string url;
    try {
        std::ifstream file(path);nlohmann::json config;if(file)file>>config;
        url=config.value("update_manifest_url",config.value("windows_update_manifest_url",std::string()));
    }catch(...){}
    configure(std::move(url),PLATFORMER_CLIENT_VERSION_ID,std::move(fetch));
}
bool request(uint32_t action){std::lock_guard<std::mutex> lock(mutex());return action==DF_UPDATE_CHECK&&checker().check();}
DFSandboxUpdateStatus snapshot(){std::lock_guard<std::mutex> lock(mutex());return checker().snapshot();}
std::string downloadUrl(){std::lock_guard<std::mutex> lock(mutex());return checker().downloadUrl();}
void shutdown(){std::lock_guard<std::mutex> lock(mutex());checker().shutdown();}
#ifndef _WIN32
bool handoff() {
    if(!request(DF_UPDATE_CHECK))return false;
    for(;;) {
        const auto status=snapshot();
        if(status.state!=DF_UPDATE_CHECKING) {
            if(status.state==DF_UPDATE_AVAILABLE) {
                const auto url=downloadUrl();
                if(SDL_OpenURL(url.c_str()))return true;
            }
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,"Sandbox updater",status.detail,nullptr);
            return false;
        }
        SDL_Delay(10);
    }
}
#endif
}
