// Windows broker: this binary does not link the game or its mod loader.
#include "GameSandbox.h"
#include "SandboxPointers.h"
#include "EditionPolicy.h"
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

static int loadGame(int argc, char** argv) {
    wchar_t executable[32768];
    DWORD length=GetModuleFileNameW(nullptr,executable,32768);
    if(!length||length>=32768)return 1;
    auto root=std::filesystem::path(executable).parent_path();
    SetCurrentDirectoryW(root.c_str());
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_USER_DIRS);
    auto directory=AddDllDirectory(root.c_str());
    auto dll=root/L"platformer.dll";
    HMODULE game=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_USER_DIRS);
    if(!game) {
        MessageBoxW(nullptr,L"platformer.dll or one of its dependencies could not be loaded. Reinstall the complete game package.",L"Game startup failed",MB_OK|MB_ICONERROR);
        if(directory)RemoveDllDirectory(directory);return 1;
    }
    using Entry=DFSandboxGameEntry;
    auto entry=reinterpret_cast<Entry>(GetProcAddress(game,"DFGameMain"));
    int result=1;
    if(entry) {
        SandboxPointers::Handle handle(SandboxPointers::api().register_game(entry));
        result=SandboxPointers::api().call_game(handle.get(),argc,argv);
    }
    if(!entry)MessageBoxW(nullptr,L"The game DLL has an incompatible entry point.",L"Game startup failed",MB_OK|MB_ICONERROR);
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
