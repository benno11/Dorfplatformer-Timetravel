#include "CrashReporter.h"
#include <cassert>
#include <csignal>
#include <chrono>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <string>
#include <exception>
std::string GetAppSaveRootPath(){return ".";}
int main(){
    struct rlimit noCore{0,0};setrlimit(RLIMIT_CORE,&noCore);
    for(int signal:{SIGSEGV,SIGBUS,SIGABRT,SIGILL}) {
        auto start=std::chrono::steady_clock::now();auto child=fork();assert(child>=0);
        if(!child){CrashReporter::start();raise(signal);_exit(0);}
        int status=0;assert(waitpid(child,&status,0)==child);
        assert(WIFEXITED(status)&&WEXITSTATUS(status)==128+signal);
        assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
    }
    auto child=fork();assert(child>=0);if(!child){CrashReporter::start();std::terminate();}
    int status=0;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==134);
    CrashReporter::start();CrashReporter::stop();CrashReporter::start();CrashReporter::stop();
}
