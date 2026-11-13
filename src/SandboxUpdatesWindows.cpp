#include "SandboxUpdates.h"
#include "SandboxVersion.h"
#include "UpdateManifest.h"
#include "BuildInfo.h"
#include <windows.h>
#include <winhttp.h>
#include <SDL3/SDL.h>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace SandboxUpdates {
namespace {
std::wstring wide(const std::string& value) {
    if(value.empty())return {};
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!size)throw std::runtime_error("Invalid UTF-8 update metadata.");
    std::wstring out(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),size);return out;
}
std::wstring quote(const std::wstring& value) {
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t c:value) {
        if(c==L'\\'){++slashes;continue;}
        if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}
        slashes=0;
    }
    out.append(slashes*2,L'\\');return out+L'\"';
}
class Internet {
public:
    explicit Internet(HINTERNET handle=nullptr):handle_(handle){}
    ~Internet(){if(handle_)WinHttpCloseHandle(handle_);}
    operator HINTERNET()const{return handle_;}
private:HINTERNET handle_;
};
std::string fetchManifest(const std::string& url) {
    if(!AppUpdate::isWebUrl(url))throw std::runtime_error("Invalid update manifest URL.");
    const auto address=wide(url);URL_COMPONENTS components{};components.dwStructSize=sizeof(components);
    components.dwHostNameLength=components.dwUrlPathLength=components.dwExtraInfoLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(address.c_str(),static_cast<DWORD>(address.size()),0,&components))throw std::runtime_error("Invalid update manifest URL.");
    const std::wstring host(components.lpszHostName,components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath,components.dwUrlPathLength);
    if(components.dwExtraInfoLength)path.append(components.lpszExtraInfo,components.dwExtraInfoLength);
    if(path.empty())path=L"/";
    const auto agent=wide(std::string("DF-Sandbox/")+GameSandbox::version);
    Internet session(WinHttpOpen(agent.c_str(),WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session)throw std::runtime_error("Could not initialize the sandbox update connection.");
    WinHttpSetTimeouts(session,5000,5000,10000,10000);
    Internet connection(WinHttpConnect(session,host.c_str(),components.nPort,0));
    Internet request(WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,
        components.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0));
    DWORD redirectPolicy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if(!connection||!request||!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirectPolicy,sizeof(redirectPolicy))||
       !WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr))
        throw std::runtime_error("Could not download the update manifest.");
    DWORD status=0,size=sizeof(status);
    if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)||status!=200)
        throw std::runtime_error("The update server did not return a valid manifest.");
    std::string body;char buffer[8192];const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    for(;;) {
        DWORD count=0;if(!WinHttpReadData(request,buffer,sizeof(buffer),&count))throw std::runtime_error("The update connection was interrupted.");
        if(!count)break;
        if(body.size()+count>1024*1024||std::chrono::steady_clock::now()>deadline)throw std::runtime_error("The update manifest exceeds its size or time limit.");
        body.append(buffer,count);
    }
    return body;
}
std::filesystem::path executableRoot() {
    wchar_t path[32768];const auto count=GetModuleFileNameW(nullptr,path,32768);
    if(!count||count>=32768)throw std::runtime_error("Could not locate the sandbox executable.");
    return std::filesystem::path(path).parent_path();
}
}
void configureWindows(const std::filesystem::path& root){configureFile(root/L"assets/config.json",fetchManifest);}
bool handoff() {
    try {
        const auto root=executableRoot();configureWindows(root);
        if(!request(DF_UPDATE_CHECK))throw std::runtime_error("Update checking is not configured.");
        DFSandboxUpdateStatus status{};
        do{status=snapshot();if(status.state==DF_UPDATE_CHECKING)Sleep(10);}while(status.state==DF_UPDATE_CHECKING);
        if(status.state!=DF_UPDATE_AVAILABLE){MessageBoxA(nullptr,status.detail,"Sandbox updater",MB_OK|MB_ICONINFORMATION);return false;}
        const auto url=downloadUrl();
        if(url.rfind("https://",0)!=0)throw std::runtime_error("Automatic installation requires an HTTPS download URL.");
        std::string message="Install the new game and sandbox bundle?\nGame: ";message+=status.latest_game;
        if(status.latest_sandbox[0]){message+="\nSandbox: ";message+=status.latest_sandbox;}
        message+="\n\nThe installer will download in the background and reopen the game when finished.";
        if(MessageBoxA(nullptr,message.c_str(),"Sandbox updater",MB_YESNO|MB_ICONQUESTION)!=IDYES)return false;
        std::filesystem::path updater;
        for(const auto& candidate:{root/L"df-updater.exe",root.parent_path()/L"df-updater.exe",root.parent_path().parent_path()/L"df-updater.exe"})
            if(std::filesystem::is_regular_file(candidate)){updater=candidate;break;}
        if(updater.empty())throw std::runtime_error("df-updater.exe is missing from the installed game package.");
        std::wstring command=quote(updater.wstring())+L" --silent --mode install --relaunch --app-dir "+quote(root.wstring());
        command+=L" --installer-url "+quote(wide(url));
        command+=L" --current-version "+quote(wide(PLATFORMER_CLIENT_VERSION));
        command+=L" --current-version-id "+quote(wide(PLATFORMER_CLIENT_VERSION_ID));
        command+=L" --latest-version "+quote(wide(status.latest_game));
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        std::vector<wchar_t> text(command.begin(),command.end());text.push_back(0);
        if(!CreateProcessW(updater.c_str(),text.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process))
            throw std::runtime_error("The sandbox could not start the update installer helper.");
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
        return true; // The broker exits, releasing its EXE before installation.
    }catch(const std::exception& error){MessageBoxA(nullptr,error.what(),"Sandbox updater",MB_OK|MB_ICONERROR);return false;}
}
}
