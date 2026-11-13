#include "GameSandbox.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/ptrace.h>
#include <fcntl.h>
#include <cerrno>
#include <sys/socket.h>
int main(int argc,char** argv){
    bool syscallsOnly=argc>1;
    auto root=std::filesystem::temp_directory_path()/("df-sandbox-test-"+std::to_string(getpid()));
    auto game=root/"game",save=root/"save",unrelated=root/"unrelated";
    std::filesystem::create_directories(game/"assets/levels");std::filesystem::create_directories(save);std::filesystem::create_directories(unrelated);
    std::ofstream(unrelated/"private")<<"unrelated-data";std::ofstream(game/"platformer.exe")<<"broker";
    std::filesystem::create_symlink(unrelated/"private",save/"escape");
    std::string invalidStatus;
    assert(!GameSandbox::install("/",save,invalidStatus));
    assert(!GameSandbox::install(game,game,invalidStatus));
    std::filesystem::create_directories(save/"installation");
    assert(!GameSandbox::install(save/"installation",save,invalidStatus));
    auto pid=fork();assert(pid>=0);
    if(!pid){
        std::string status;bool active=GameSandbox::install(game,save,status);
        if(!active&&!syscallsOnly){std::cerr<<status<<" errno="<<errno<<"\n";_exit(77);}
        assert(GameSandbox::nativeModsAllowed()==active);
        std::ofstream allowed(save/"allowed");allowed<<"works";allowed.close();assert(allowed.good());
        if(!syscallsOnly) {
        std::ifstream denied(unrelated/"private");assert(!denied);
        std::ifstream escape(save/"escape");assert(!escape);
        assert(open((game/"platformer.exe").c_str(),O_WRONLY|O_TRUNC)==-1&&errno==EACCES);
        std::ofstream level(game/"assets/levels/editor.txt");level<<"level";level.close();assert(level.good());
        assert(open((unrelated/"write").c_str(),O_CREAT|O_WRONLY,0600)==-1&&errno==EACCES);
        }
        assert(GameSandbox::sealRuntime());
        assert(socket(AF_UNIX,SOCK_STREAM,0)==-1&&errno==EPERM);
        int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==-1&&errno==EPERM);
        int internet=socket(AF_INET,SOCK_STREAM,0);assert(internet>=0);close(internet);
        bool ran=false;std::thread thread([&]{ran=true;});thread.join();assert(ran);
        assert(ptrace(PTRACE_ATTACH,getppid(),nullptr,nullptr)==-1&&errno==EPERM);
        assert(kill(getppid(),0)==-1&&errno==EPERM);
        assert(fork()==-1&&errno==EPERM);
        char* args[]={const_cast<char*>("/bin/true"),nullptr};assert(execv(args[0],args)==-1&&errno==EPERM);
        _exit(0);
    }
    int status=0;assert(waitpid(pid,&status,0)==pid);
    std::filesystem::remove_all(root);
    if(WIFEXITED(status)&&WEXITSTATUS(status)==77){std::cerr<<"Sandbox test requires Landlock ABI 3\n";return 77;}
    assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    std::cout<<"Sandbox denied unrelated data, binary writes, exec, fork, signals and ptrace; gameplay threads and saves work\n";
}
