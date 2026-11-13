#include "SandboxPointers.h"
#include <cassert>
#include <cstring>
#include <iostream>
#ifndef _WIN32
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
static int updates=0;
static int gameEntry(int argc,char**,const DFSandboxAPI* api){return argc+int(api->version);}
int main(){
    auto& api=SandboxPointers::api();
    DFSandboxUpdateStatus status{};status.struct_size=sizeof(status);
    assert(api.update_status(&status)&&status.current_sandbox[0]);
    status.struct_size=0;assert(!api.update_status(&status));
    assert(!api.update_action(12345));
    SandboxPointers::Handle buffer(api.allocate(16));
    const char value[]="checked";char readback[sizeof(value)]{};
    api.write(buffer.get(),3,value,sizeof(value));api.read(buffer.get(),3,readback,sizeof(readback));assert(!std::strcmp(value,readback));
    SandboxPointers::Handle immutable(api.store_immutable(value,sizeof(value)));
    api.read(immutable.get(),0,readback,sizeof(readback));assert(!std::strcmp(value,readback));
    SandboxPointers::Handle game(api.register_game(gameEntry));assert(api.call_game(game.get(),5,nullptr)==6);
    DFModCallbacks callbacks{};callbacks.struct_size=sizeof(callbacks);callbacks.on_update=[](void*,double){++updates;};
    SandboxPointers::Handle mod(api.register_mod_callbacks(&callbacks));callbacks.on_update=nullptr;
    api.mod_update(mod.get(),0.01);assert(updates==1);
    api.mod_level_start(mod.get(),nullptr);api.mod_menu_load(mod.get(),nullptr);api.mod_menu_action(mod.get(),"menu","action");api.mod_unload(mod.get());
#ifndef _WIN32
    struct rlimit noCore{0,0};setrlimit(RLIMIT_CORE,&noCore);
    auto dies=[&](auto operation){auto pid=fork();assert(pid>=0);if(!pid){operation();_exit(0);}int status=0;assert(waitpid(pid,&status,0)==pid);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);};
    dies([&]{auto forged=buffer.get();forged.authentication[0]^=1;api.read(forged,0,readback,1);});
    dies([&]{api.read(buffer.get(),SIZE_MAX,readback,1);});
    dies([&]{api.write(immutable.get(),0,value,1);});
    dies([&]{api.call_game(buffer.get(),0,nullptr);});
    dies([&]{auto stale=api.allocate(1);api.release(stale);api.read(stale,0,readback,1);});
    dies([&]{auto stale=api.allocate(1);api.release(stale);api.release(stale);});
#endif
    std::cout<<"Pointer API validates authentication, type, permissions, bounds and lifetime before access/jumps\n";
}
