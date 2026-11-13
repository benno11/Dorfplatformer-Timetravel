#include "CrashReporter.h"
#include "AssetPath.h"
#include <SDL3/SDL.h>
#include <csignal>
#include <exception>
#include <new>
#include <cstdio>
#include <filesystem>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace CrashReporter {
namespace {
bool active=false;
std::terminate_handler previousTerminate=nullptr;
std::new_handler previousNew=nullptr;
#ifdef _WIN32
PVOID exceptionHandler=nullptr;
LONG CALLBACK memoryException(EXCEPTION_POINTERS* info) {
    const auto code=info->ExceptionRecord->ExceptionCode;
    if(code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_STACK_OVERFLOW||code==0xC0000374UL||code==0xC0000409UL) {
        // Do not log, allocate, save, or display UI in a corrupted process.
        TerminateProcess(GetCurrentProcess(),code);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
[[noreturn]] void fatal(int code) {TerminateProcess(GetCurrentProcess(),code);std::_Exit(code);}
#else
int marker=-1;
alignas(16) unsigned char alternateStack[64*1024];
stack_t oldStack{};
constexpr int signals[]={SIGSEGV,SIGABRT,SIGBUS,SIGILL,SIGFPE};
struct sigaction previousSignals[5]{};
[[noreturn]] void fatal(int code) {
    // write and _exit are async-signal-safe. No locks, SDL calls or heap access.
    if(marker>=0) { const char value='1'; (void)write(marker,&value,1); }
    _exit(code);
}
void signalHandler(int signal){fatal(128+signal);}
#endif
[[noreturn]] void terminateHandler(){fatal(134);}
[[noreturn]] void allocationFailure(){fatal(134);}
}
void start() {
    if(active)return;
    active=true;
#if defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    auto path=std::filesystem::u8path(GetAppSaveRootPath())/"critical_error.pending";
    std::error_code ec;
    if(std::filesystem::exists(path,ec)&&std::filesystem::file_size(path,ec)>0&&!ec) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Previous session stopped",
            "The previous game session stopped after a critical memory error.\nDisable recently added mods before trying again.",nullptr);
    }
    marker=open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
#endif
#ifdef _WIN32
    HeapSetInformation(nullptr,HeapEnableTerminationOnCorruption,nullptr,0);
    exceptionHandler=AddVectoredExceptionHandler(1,memoryException);
#else
    stack_t stack{};stack.ss_sp=alternateStack;stack.ss_size=sizeof(alternateStack);
    sigaltstack(&stack,&oldStack);
    struct sigaction action{};action.sa_handler=signalHandler;sigemptyset(&action.sa_mask);
    action.sa_flags=SA_ONSTACK|SA_RESETHAND;
    for(size_t i=0;i<5;++i)sigaction(signals[i],&action,&previousSignals[i]);
#endif
    previousTerminate=std::set_terminate(terminateHandler);
    previousNew=std::set_new_handler(allocationFailure);
}
void stop() {
    if(!active)return;
    active=false;
    std::set_terminate(previousTerminate);std::set_new_handler(previousNew);
#ifdef _WIN32
    if(exceptionHandler)RemoveVectoredExceptionHandler(exceptionHandler);
    exceptionHandler=nullptr;
#else
    for(size_t i=0;i<5;++i)sigaction(signals[i],&previousSignals[i],nullptr);
    sigaltstack(&oldStack,nullptr);
    if(marker>=0)close(marker);marker=-1;
#endif
}
}
