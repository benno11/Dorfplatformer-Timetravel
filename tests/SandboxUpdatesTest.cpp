#include "SandboxUpdates.h"
#include "SandboxVersion.h"
#include "UpdateManifest.h"
#include <cassert>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
using namespace SandboxUpdates;
int main(){
    const std::string os=AppUpdate::platform(),arch=AppUpdate::architecture();
    nlohmann::json manifest={{"version","3.0.0"},{"version_id",42},{"sandbox_version","1.0.0"},{"notes","Test release"}};
    manifest["installer_urls"][os][arch]="https://example.invalid/update";
    auto current=evaluate(manifest.dump(),"42","1.0.0");assert(current.state==DF_UPDATE_CURRENT);
    manifest["sandbox_version"]="1.1.0";
    auto sandbox=evaluate(manifest.dump(),"42","1.0.0");assert(sandbox.state==DF_UPDATE_AVAILABLE&&sandbox.url=="https://example.invalid/update");
    manifest["version_id"]=43;
    assert(evaluate(manifest.dump(),"42","1.0.0").state==DF_UPDATE_AVAILABLE);
    assert(evaluate(manifest.dump(),"44","1.0.0").state==DF_UPDATE_ERROR);
    manifest["sandbox_version"]="0.9.0";assert(evaluate(manifest.dump(),"42","1.0.0").state==DF_UPDATE_ERROR);
    manifest.erase("sandbox_version");assert(evaluate(manifest.dump(),"42","1.0.0").state==DF_UPDATE_AVAILABLE);
    manifest["version_id"]=42;manifest.erase("installer_urls");assert(evaluate(manifest.dump(),"42","1.0.0").state==DF_UPDATE_CURRENT);
    assert(evaluate("invalid json","42","1.0.0").state==DF_UPDATE_ERROR);
    assert(evaluate(std::string(1024*1024+1,'x'),"42","1.0.0").state==DF_UPDATE_ERROR);
    manifest["version_id"]=43;manifest["installer_urls"][os][arch]="file:///tmp/not-a-web-download";
    assert(evaluate(manifest.dump(),"42","1.0.0").state==DF_UPDATE_ERROR);
    manifest["installer_urls"][os][arch]="https://example.invalid/update";
    Checker checker;std::atomic<int> calls{0};
    std::promise<void> gate;auto ready=gate.get_future().share();
    checker.configure("https://example.invalid/manifest","42",[&](const std::string&){++calls;ready.wait();return manifest.dump();});
    assert(checker.snapshot().state==DF_UPDATE_READY);assert(checker.check());
    assert(checker.snapshot().state==DF_UPDATE_CHECKING);assert(!checker.check());
    gate.set_value();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(checker.snapshot().state==DF_UPDATE_CHECKING&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    auto status=checker.snapshot();assert(status.state==DF_UPDATE_AVAILABLE&&calls==1);
    assert(std::string(status.current_sandbox)==GameSandbox::version);
    checker.configure("file:///tmp/manifest","42",[](const std::string&)->std::string{assert(false);return {};});
    assert(!checker.check()&&checker.snapshot().state==DF_UPDATE_ERROR);
    checker.configure("","42",{});assert(!checker.check()&&checker.snapshot().state==DF_UPDATE_NOT_CONFIGURED);
    assert(!SandboxUpdates::request(12345));
}
