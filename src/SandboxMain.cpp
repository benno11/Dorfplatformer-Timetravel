// Windows broker: this binary does not link the game or its mod loader.
#include "GameSandbox.h"
#include "SandboxPointers.h"
#include "SandboxUpdates.h"
#include "EditionPolicy.h"
#include "BuildPairIntegrity.h"
#include "BuildPairTrust.h"
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <algorithm>

extern "C" const unsigned char dfBuildPairSeal[BuildPair::sealSize];
namespace {
class LockedImage {
public:
    explicit LockedImage(const std::filesystem::path& path)
        : file_(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)) {}
    ~LockedImage(){if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);}
    LockedImage(const LockedImage&)=delete;
    bool read(BuildPair::Bytes& data) {
        LARGE_INTEGER size{};
        if(file_==INVALID_HANDLE_VALUE||!GetFileSizeEx(file_,&size)||size.QuadPart<0||size.QuadPart>512LL*1024*1024)return false;
        data.resize(static_cast<size_t>(size.QuadPart));
        size_t offset=0;
        while(offset<data.size()) {
            DWORD count=0,chunk=static_cast<DWORD>(std::min<size_t>(data.size()-offset,8*1024*1024));
            if(!ReadFile(file_,data.data()+offset,chunk,&count,nullptr)||count!=chunk)return false;
            offset+=count;
        }
        return true;
    }
private:
    HANDLE file_=INVALID_HANDLE_VALUE;
};
}

static int loadGame(int argc, char** argv) {
    wchar_t executable[32768];
    DWORD length=GetModuleFileNameW(nullptr,executable,32768);
    if(!length||length>=32768)return 1;
    auto root=std::filesystem::path(executable).parent_path();
    SetCurrentDirectoryW(root.c_str());
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_USER_DIRS);
    auto directory=AddDllDirectory(root.c_str());
    auto dll=root/L"platformer.dll";
    // Keep both handles open until the DLL unloads. Denying write/delete sharing
    // prevents replacing or modifying the verified game file before it loads.
    LockedImage exeImage(executable),dllImage(dll);
    BuildPair::Bytes exeBytes,dllBytes;
    BuildPair::Seal loadedSeal{};
    auto stored=reinterpret_cast<const volatile unsigned char*>(dfBuildPairSeal);
    for(size_t i=0;i<loadedSeal.size();++i)loadedSeal[i]=stored[i];
    std::string integrityError;
    const bool readable=exeImage.read(exeBytes)&&dllImage.read(dllBytes);
    if(!readable||!BuildPair::verify(exeBytes,dllBytes,BuildPair::trustedPublicKey,Edition::lite?1:0,integrityError,&loadedSeal)) {
        std::string message="The game was not loaded.\n";
        message+=readable?integrityError:"The launcher or game DLL could not be locked and read.";
        message+="\nReinstall the complete matching game package.";
        MessageBoxA(nullptr,message.c_str(),"Game integrity check failed",MB_OK|MB_ICONERROR);
        if(directory)RemoveDllDirectory(directory);
        return 1;
    }
    // Verification data is no longer needed; file locks remain held.
    BuildPair::Bytes{}.swap(exeBytes);BuildPair::Bytes{}.swap(dllBytes);
    HMODULE game=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_USER_DIRS);
    if(!game) {
        MessageBoxW(nullptr,L"platformer.dll or one of its dependencies could not be loaded. Reinstall the complete game package.",L"Game startup failed",MB_OK|MB_ICONERROR);
        if(directory)RemoveDllDirectory(directory);return 1;
    }
    SandboxUpdates::configureWindows(root);
    using Entry=DFSandboxGameEntry;
    auto entry=reinterpret_cast<Entry>(GetProcAddress(game,"DFGameMain"));
    int result=1;
    if(entry) {
        SandboxPointers::Handle handle(SandboxPointers::api().register_game(entry));
        result=SandboxPointers::api().call_game(handle.get(),argc,argv);
    }
    if(!entry)MessageBoxW(nullptr,L"The game DLL has an incompatible entry point.",L"Game startup failed",MB_OK|MB_ICONERROR);
    SandboxUpdates::shutdown();
    FreeLibrary(game);if(directory)RemoveDllDirectory(directory);return result;
}
static int launch(int argc,char** argv) {
    // Only the broker owns the single-instance lock. The restricted child uses
    // its verified AppContainer token, never a command-line bypass flag.
    if(!GameSandbox::saveRootOverride().empty())return loadGame(argc,argv);
    HANDLE lock=CreateMutexW(nullptr,TRUE,Edition::lite?L"Local\\DFLiteGameSingleInstance":L"Local\\DFNewGameSingleInstance");
    if(!lock||GetLastError()==ERROR_ALREADY_EXISTS){if(lock)CloseHandle(lock);return 0;}
    int result=GameSandbox::supervise(argc,argv,loadGame);
    ReleaseMutex(lock);CloseHandle(lock);return result;
}
int main(int argc,char** argv){return launch(argc,argv);}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){return launch(__argc,__argv);}
