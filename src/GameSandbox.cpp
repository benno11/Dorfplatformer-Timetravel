#include "GameSandbox.h"
#include "SandboxUpdates.h"
#include "ProcessEnvironment.h"
#include "EditionPolicy.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <cerrno>
#include <cstring>
#if defined(__linux__) && !defined(__ANDROID__)
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstddef>
#include <sched.h>
#include <sys/socket.h>
// Stable kernel ABI declarations allow building with older distro headers.
struct LandlockRules { uint64_t handled; };
struct LandlockPath { uint64_t allowed; int32_t parent; } __attribute__((packed));
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE
#include <sandbox.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
#elif defined(_WIN32)
#include <windows.h>
#include <userenv.h>
#include <aclapi.h>
#include <sddl.h>
#endif
namespace GameSandbox {
namespace {
bool restricted=false;
bool safeRoots(const std::filesystem::path& game,const std::filesystem::path& save) {
    auto same=[](const std::filesystem::path& a,const std::filesystem::path& b) {
#ifdef _WIN32
        return _wcsicmp(a.c_str(),b.c_str())==0;
#else
        return a==b;
#endif
    };
    std::error_code error;
    auto g=std::filesystem::canonical(game,error);if(error||same(g,g.root_path()))return false;
    auto s=std::filesystem::canonical(save,error);if(error||same(s,s.root_path())||same(s,g))return false;
    for(auto ancestor=g.parent_path();!ancestor.empty();) {
        if(same(ancestor,s))return false;
        const auto parent=ancestor.parent_path();if(same(parent,ancestor))break;ancestor=parent;
    }
#ifdef _WIN32
    const auto home=ProcessEnvironment::get("USERPROFILE");
#else
    const auto home=ProcessEnvironment::get("HOME");
#endif
    if(!home.empty()) {
        auto h=std::filesystem::weakly_canonical(std::filesystem::u8path(home),error);
        if(!error)for(;;) {
            // Never grant the whole home directory, or one of its ancestors.
            if(same(g,h)||same(s,h))return false;
            auto parent=h.parent_path();if(parent.empty()||same(parent,h))break;h=parent;
        }
    }
    return true;
}

bool containedPath(const std::filesystem::path& root,const std::filesystem::path& path) {
    std::error_code error;
    auto base=std::filesystem::weakly_canonical(root,error);if(error)return false;
    auto target=std::filesystem::weakly_canonical(path,error);if(error)return false;
    auto relative=target.lexically_relative(base);
    return !relative.empty()&&!relative.is_absolute()&&*relative.begin()!="..";
}
void crashNotice() {
    constexpr const char* message="A critical memory error was detected.\nThe game was stopped immediately.\n"
        "Unsaved progress may be lost.\nDisable recently added mods before trying again.";
#ifdef _WIN32
    MessageBoxA(nullptr,message,"Game stopped",MB_OK|MB_ICONERROR);
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Game stopped",message,nullptr);
#endif
}
#if defined(__linux__) && !defined(__ANDROID__)
#ifndef __NR_landlock_create_ruleset
#if defined(__x86_64__) || defined(__aarch64__) || defined(__i386__)
#define __NR_landlock_create_ruleset 444
#define __NR_landlock_add_rule 445
#define __NR_landlock_restrict_self 446
#endif
#endif
bool linuxSeccomp() {
#if defined(__x86_64__) || defined(__aarch64__) || defined(__i386__)
    if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0))return false;
    std::vector<sock_filter> filter;
    auto stmt=[&](unsigned short code,unsigned int k){filter.push_back(BPF_STMT(code,k));};
    auto jump=[&](unsigned short code,unsigned int k,unsigned char yes,unsigned char no){filter.push_back(BPF_JUMP(code,k,yes,no));};
    stmt(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,arch));
#if defined(__x86_64__)
    constexpr unsigned architecture=AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    constexpr unsigned architecture=AUDIT_ARCH_AARCH64;
#else
    constexpr unsigned architecture=AUDIT_ARCH_I386;
#endif
    jump(BPF_JMP|BPF_JEQ|BPF_K,architecture,1,0); stmt(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS);
    stmt(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,nr));
#ifdef __x86_64__
    jump(BPF_JMP|BPF_JSET|BPF_K,0x40000000,0,1); stmt(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS);
#endif
    auto deny=[&](unsigned n){jump(BPF_JMP|BPF_JEQ|BPF_K,n,0,1);stmt(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);};
#ifdef __NR_fork
    deny(__NR_fork);
#endif
#ifdef __NR_vfork
    deny(__NR_vfork);
#endif
#ifdef __NR_clone3
    jump(BPF_JMP|BPF_JEQ|BPF_K,__NR_clone3,0,1);stmt(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|ENOSYS);
#endif
    jump(BPF_JMP|BPF_JEQ|BPF_K,__NR_clone,0,4);
    stmt(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args[0]));
    jump(BPF_JMP|BPF_JSET|BPF_K,CLONE_THREAD,1,0);
    stmt(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);
    stmt(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
    deny(__NR_ptrace); deny(__NR_process_vm_readv); deny(__NR_process_vm_writev);
    deny(__NR_execve); deny(__NR_execveat); deny(__NR_mount); deny(__NR_umount2); deny(__NR_unshare); deny(__NR_setns);
#ifdef __NR_pidfd_getfd
    deny(__NR_pidfd_getfd);
#endif
#ifdef __NR_open_by_handle_at
    deny(__NR_open_by_handle_at);
#endif
#ifdef __NR_bpf
    deny(__NR_bpf);
#endif
#ifdef __NR_perf_event_open
    deny(__NR_perf_event_open);
#endif
#ifdef __NR_userfaultfd
    deny(__NR_userfaultfd);
#endif
    // Prevent signalling unrelated processes (threads and normal process exit still work).
    deny(__NR_kill); deny(__NR_tkill); deny(__NR_rt_sigqueueinfo);
    auto selfSignal=[&](unsigned n) {
        jump(BPF_JMP|BPF_JEQ|BPF_K,n,0,4);
        stmt(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args[0]));
        jump(BPF_JMP|BPF_JEQ|BPF_K,static_cast<unsigned>(getpid()),1,0);
        stmt(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);
        stmt(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
    };
    selfSignal(__NR_tgkill); selfSignal(__NR_rt_tgsigqueueinfo);
    stmt(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
    sock_fprog program{static_cast<unsigned short>(filter.size()),filter.data()};
    return syscall(__NR_seccomp,SECCOMP_SET_MODE_FILTER,SECCOMP_FILTER_FLAG_TSYNC,&program)==0;
#else
    return false;
#endif
}
bool linuxInstall(const std::filesystem::path& game, const std::filesystem::path& save) {
#if defined(__NR_landlock_create_ruleset) && (defined(__x86_64__) || defined(__aarch64__) || defined(__i386__))
    int abi=static_cast<int>(syscall(__NR_landlock_create_ruleset,nullptr,0,1));
    if(abi<3)return false; // truncate mediation is required
    constexpr uint64_t read=(1ULL<<2)|(1ULL<<3);
    constexpr uint64_t write=(1ULL<<1)|(1ULL<<4)|(1ULL<<5)|(1ULL<<7)|(1ULL<<8)|(1ULL<<9)|(1ULL<<10)|(1ULL<<12)|(1ULL<<13)|(1ULL<<14);
    LandlockRules rules{(1ULL<<15)-1}; // all file rights through ABI 3
    int fd=static_cast<int>(syscall(__NR_landlock_create_ruleset,&rules,sizeof(rules),0));
    if(fd<0)return false;
    bool ok=true;
    auto allow=[&](const std::filesystem::path& path,uint64_t rights,bool required=false) {
        int p=open(path.c_str(),O_PATH|O_CLOEXEC);
        if(p<0) { if(required)ok=false; return; }
        struct stat st{}; if(fstat(p,&st))ok=false;
        if(!S_ISDIR(st.st_mode))rights &= (1ULL<<0)|(1ULL<<1)|(1ULL<<2)|(1ULL<<14);
        LandlockPath rule{rights,p};
        if(syscall(__NR_landlock_add_rule,fd,1,&rule,0))ok=false;
        close(p);
    };
    allow(game,read,true); allow(save,read|write,true);
    for(auto relative:{"assets/levels","client_settings.json","client_settings_lite.json"})
        if(containedPath(game,game/relative))allow(game/relative,read|write);
    // System runtime data only. No home, Documents, Downloads, credentials or arbitrary /tmp access.
    for(auto p:{"/usr","/lib","/lib64","/etc/fonts","/etc/ssl","/etc/pki","/etc/ld.so.cache","/etc/hosts","/etc/resolv.conf","/etc/nsswitch.conf","/etc/localtime","/etc/os-release","/etc/alsa","/var/cache/fontconfig","/sys/devices","/sys/class/drm","/proc/self"})allow(p,read);
    for(auto p:{"/dev/null","/dev/zero","/dev/urandom","/dev/random","/dev/dri","/dev/snd","/dev/shm"})allow(p,read|write);
    const auto home=std::getenv("HOME");
    if(home) { allow(std::filesystem::path(home)/".config/pulse",read); allow(std::filesystem::path(home)/".config/pipewire",read); }
    if(const auto auth=std::getenv("XAUTHORITY"))allow(auth,read);
    else if(home)allow(std::filesystem::path(home)/".Xauthority",read);
    // Do not whitelist arbitrary LD_LIBRARY_PATH directories. Installed builds
    // keep runtime dependencies under the game or system directories.
    struct rlimit core{0,0}; setrlimit(RLIMIT_CORE,&core);
    if(ok)ok=prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)==0;
    if(ok)ok=syscall(__NR_landlock_restrict_self,fd,0)==0;
    close(fd); if(!ok)return false;
    return true;
#else
    return false;
#endif
}
#endif
}
bool sealRuntime() {
#if defined(__linux__) && !defined(__ANDROID__)
    // Landlock mediates file access, not Unix sockets. Prevent mods from
    // opening session-bus/keyring IPC after SDL has opened display/audio.
    sock_filter filter[]={
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,nr)),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_socketpair,0,1),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,__NR_socket,0,3),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args[0])),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AF_UNIX,0,1),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW)
    };
    sock_fprog program{static_cast<unsigned short>(sizeof(filter)/sizeof(filter[0])),filter};
    bool ok=syscall(__NR_seccomp,SECCOMP_SET_MODE_FILTER,SECCOMP_FILTER_FLAG_TSYNC,&program)==0;
    if(!ok)restricted=false;
    return ok;
#else
    return true;
#endif
}
bool nativeModsAllowed(){return restricted;}
bool install(const std::filesystem::path& gameRoot,const std::filesystem::path& saveRoot,std::string& status) {
    if(restricted){status="Process sandbox active";return true;}
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    if(!safeRoots(gameRoot,saveRoot)) {
        status="Sandbox requires separate game and save folders, outside the home/root directory; native mods disabled";
        return false;
    }
#endif
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    restricted=true; status="OS application sandbox active";
#elif defined(__linux__)
    const bool files=linuxInstall(gameRoot,saveRoot);
    const bool calls=linuxSeccomp();
    restricted=files&&calls;
    status=restricted?"Landlock filesystem and seccomp restrictions active":
        (calls?"Syscall restrictions active; filesystem sandbox unavailable; native mods disabled (Landlock ABI 3 required)":
        "Kernel sandbox unavailable; native mods disabled");
#elif defined(_WIN32)
    HANDLE token=nullptr; DWORD isContainer=0,size=0;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
        GetTokenInformation(token,TokenIsAppContainer,&isContainer,sizeof(isContainer),&size); CloseHandle(token);
    }
    restricted=isContainer!=0;
    HeapSetInformation(nullptr,HeapEnableTerminationOnCorruption,nullptr,0);
    status=restricted?"Windows AppContainer active":"AppContainer unavailable; native mods are disabled";
#elif defined(__APPLE__)
    auto quote=[](std::string s){std::string out="\"";for(char c:s){if(c=='\\'||c=='\"')out+='\\';out+=c;}return out+'\"';};
    std::string profile="(version 1)(allow default)(deny file-read* file-write*)(deny process-exec process-fork)";
    for(auto p:{"/System","/usr","/Library/Fonts","/Library/Audio","/private/etc","/dev","/private/var/db/dyld"})
        profile+="(allow file-read* (subpath "+quote(p)+"))";
    profile+="(allow file-read* (subpath "+quote(gameRoot.string())+"))";
    for(auto p:{saveRoot,gameRoot/"assets/levels"})profile+="(allow file-read* file-write* (subpath "+quote(p.string())+"))";
    for(auto name:{"client_settings.json","client_settings_lite.json"})profile+="(allow file-write* (literal "+quote((gameRoot/name).string())+"))";
    char* error=nullptr; restricted=sandbox_init(profile.c_str(),0,&error)==0;
    if(error)sandbox_free_error(error);
    status=restricted?"macOS filesystem sandbox active":"macOS sandbox unavailable; native mods are disabled";
#else
    status="Process sandbox unavailable; native mods are disabled";
#endif
    return restricted;
}
#ifdef _WIN32
namespace {
bool containerToken() {
    HANDLE token=nullptr; DWORD yes=0,size=0;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) { GetTokenInformation(token,TokenIsAppContainer,&yes,sizeof(yes),&size);CloseHandle(token); }
    return yes!=0;
}
#if !defined(PLATFORMER_GAME_LIBRARY)
bool noReparsePoints(const std::filesystem::path& root) {
    std::error_code error;
    if(GetFileAttributesW(root.c_str())&FILE_ATTRIBUTE_REPARSE_POINT)return false;
    std::filesystem::recursive_directory_iterator it(root,error),end;
    for(;!error&&it!=end;it.increment(error)) {
        DWORD attributes=GetFileAttributesW(it->path().c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    }
    return !error;
}
bool grant(PSID sid,const std::filesystem::path& path,bool write) {
    PACL old=nullptr,updated=nullptr; PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(GetNamedSecurityInfoW(path.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&old,nullptr,&descriptor)!=ERROR_SUCCESS)return false;
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions=write?FILE_ALL_ACCESS:(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE);
    access.grfAccessMode=GRANT_ACCESS;
    access.grfInheritance=std::filesystem::is_directory(path)?SUB_CONTAINERS_AND_OBJECTS_INHERIT:NO_INHERITANCE;
    access.Trustee.TrusteeForm=TRUSTEE_IS_SID;access.Trustee.TrusteeType=TRUSTEE_IS_GROUP;access.Trustee.ptstrName=static_cast<LPWSTR>(sid);
    bool ok=SetEntriesInAclW(1,&access,old,&updated)==ERROR_SUCCESS;
    if(ok)ok=SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,updated,nullptr)==ERROR_SUCCESS;
    if(updated)LocalFree(updated);if(descriptor)LocalFree(descriptor);
    if(ok&&write) {
        PSECURITY_DESCRIPTOR label=nullptr; PACL sacl=nullptr; BOOL present=FALSE,defaulted=FALSE;
        ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"S:(ML;OICI;NW;;;LW)",SDDL_REVISION_1,&label,nullptr)!=FALSE;
        if(ok)ok=GetSecurityDescriptorSacl(label,&present,&sacl,&defaulted)!=FALSE;
        if(ok)ok=SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,LABEL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,sacl)==ERROR_SUCCESS;
        if(label)LocalFree(label);
    }
    return ok;
}
int windowsLaunch() {
    wchar_t exe[32768];DWORD n=GetModuleFileNameW(nullptr,exe,32768);
    if(n==0||n>=32768)return -1;
    std::filesystem::path game=std::filesystem::path(exe).parent_path();
    char* pref=SDL_GetPrefPath("Benno111",Edition::storageName);
    if(!pref)return -1;
    auto save=std::filesystem::u8path(pref); SDL_free(pref);
    std::error_code ec;std::filesystem::create_directories(save,ec);if(ec)return -1;
    if(!safeRoots(game,save)||!noReparsePoints(game)||!noReparsePoints(save))return -1;
    const wchar_t* profile=Edition::lite?L"Benno111.DorfplatformerTimetravel.Lite":L"Benno111.DorfplatformerTimetravel.Full";
    PSID sid=nullptr;
    HRESULT hr=CreateAppContainerProfile(profile,profile,L"Game files and network access only",nullptr,0,&sid);
    if(hr==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))hr=DeriveAppContainerSidFromAppContainerName(profile,&sid);
    if(FAILED(hr)||!sid)return -1;
    bool ok=grant(sid,game,false)&&grant(sid,save,true);
    if(std::filesystem::exists(game/L"assets/levels"))ok=ok&&grant(sid,game/L"assets/levels",true);
    for(auto name:{L"client_settings.json",L"client_settings_lite.json"})
        if(std::filesystem::exists(game/name))ok=ok&&grant(sid,game/name,true);
    SID_IDENTIFIER_AUTHORITY authority=SECURITY_APP_PACKAGE_AUTHORITY;
    PSID internet=nullptr,localNetwork=nullptr;
    ok=ok&&AllocateAndInitializeSid(&authority,2,SECURITY_CAPABILITY_BASE_RID,1,0,0,0,0,0,0,&internet)
         &&AllocateAndInitializeSid(&authority,2,SECURITY_CAPABILITY_BASE_RID,3,0,0,0,0,0,0,&localNetwork);
    SID_AND_ATTRIBUTES capabilities[2]={{internet,SE_GROUP_ENABLED},{localNetwork,SE_GROUP_ENABLED}};
    SECURITY_CAPABILITIES security{};security.AppContainerSid=sid;security.Capabilities=capabilities;security.CapabilityCount=2;
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<unsigned char> storage(bytes);
    auto attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    bool initialized=InitializeProcThreadAttributeList(attributes,1,0,&bytes)!=FALSE;
    ok=ok&&initialized;
    if(ok)ok=UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,&security,sizeof(security),nullptr,nullptr)!=FALSE;
    // The broker chooses the save directory; the child may use it only with an actual AppContainer token.
    const auto oldRoot=ProcessEnvironment::wide(L"DF_SANDBOX_SAVE_ROOT");
    SetEnvironmentVariableW(L"DF_SANDBOX_SAVE_ROOT",save.c_str());
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
    std::wstring command=GetCommandLineW(); std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(0);
    PROCESS_INFORMATION process{};
    if(ok)ok=CreateProcessW(exe,mutableCommand.data(),nullptr,nullptr,FALSE,EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED,nullptr,game.c_str(),&startup.StartupInfo,&process)!=FALSE;
    SetEnvironmentVariableW(L"DF_SANDBOX_SAVE_ROOT",oldRoot.empty()?nullptr:oldRoot.c_str());
    if(initialized)DeleteProcThreadAttributeList(attributes);
    if(internet)FreeSid(internet);if(localNetwork)FreeSid(localNetwork);FreeSid(sid);
    HANDLE job=nullptr;
    if(ok) {
        job=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit=1;
        ok=job&&SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))&&AssignProcessToJobObject(job,process.hProcess);
    }
    if(!ok) {
        if(process.hProcess) {TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);}
        if(job)CloseHandle(job);return -1;
    }
    ResumeThread(process.hThread);CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hProcess);CloseHandle(job);
    if(code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_STACK_OVERFLOW||code==EXCEPTION_ILLEGAL_INSTRUCTION||
       code==0xC0000374UL||code==0xC0000409UL||code==0xC0000417UL||code==134||code==139)crashNotice();
    return static_cast<int>(code);
}
#endif // broker-only implementation
}
#endif
std::string saveRootOverride() {
#ifdef _WIN32
    if(containerToken()) return ProcessEnvironment::get("DF_SANDBOX_SAVE_ROOT");
#endif
    return {};
}
int supervise(int argc,char** argv,int (*entry)(int,char**)) {
#if defined(_WIN32) && defined(PLATFORMER_GAME_LIBRARY)
    return entry(argc,argv);
#elif defined(_WIN32)
    if(containerToken())return entry(argc,argv);
    int result;
    do {
        result=windowsLaunch();
        if(result==updateExitCode)result=SandboxUpdates::handoff()?0:75;
    } while(result==75);
    if(result==-1) {
        MessageBoxA(nullptr,"The protected game process could not start. Check that the game and save folders are writable. The game has not been started without protection.","Sandbox startup failed",MB_OK|MB_ICONERROR);
        return 1;
    }
    return result;
#elif (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && !TARGET_OS_IPHONE)
    for(;;) {
    const auto parent=getpid();
    pid_t child=fork();
    if(child<0) { std::fputs("Could not start protected game process\n",stderr);return 1; }
    if(child==0) {
#if defined(__linux__)
        prctl(PR_SET_PDEATHSIG,SIGKILL);
        if(getppid()!=parent)_exit(1);
#endif
        return entry(argc,argv);
    }
    int status=0;pid_t waited;
    do {waited=waitpid(child,&status,0);}while(waited<0&&errno==EINTR);
    if(waited<0)return 1;
    int result=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
    if(result==updateExitCode){if(SandboxUpdates::handoff())return 0;continue;}
    if(result==75)continue;
    if(result==128+SIGSEGV||result==128+SIGBUS||result==128+SIGABRT||result==128+SIGILL||result==128+SIGFPE
#ifdef SIGSYS
       ||result==128+SIGSYS
#endif
    )crashNotice();
    return result;
    }
#else
    return entry(argc,argv);
#endif
}
}
