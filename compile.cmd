@echo off
setlocal enabledelayedexpansion

:: -------------------------------------------------------
::  DF-New  --  Interactive Build Menu
::  Supports Windows 10+ (ANSI colour via VT processing).
:: -------------------------------------------------------

:: Enable VT100 / ANSI colour sequences in the console.
reg add HKCU\Console /v VirtualTerminalLevel /t REG_DWORD /d 1 /f >nul 2>&1

:: ANSI colour shortcuts  (ESC = 0x1B)
for /f %%a in ('echo prompt $E^| cmd /q') do set "ESC=%%a"
set "RESET=%ESC%[0m"
set "BOLD=%ESC%[1m"
set "CYAN=%ESC%[96m"
set "YELLOW=%ESC%[93m"
set "GREEN=%ESC%[92m"
set "RED=%ESC%[91m"
set "WHITE=%ESC%[97m"
set "DIM=%ESC%[2m"

:: Default settings
set "CFG=Release"
set "COMPILER=llvm"
set "RUN_AFTER=No"
set "USE_VCPKG=No"

:MAIN_MENU
cls
echo %CYAN%%BOLD%
echo  +=====================================================+
echo  ^|         DF-New  --  Interactive Build Menu         ^|
echo  +=====================================================+%RESET%
echo.
echo  %YELLOW%Current Settings:%RESET%
echo    Config     : %GREEN%!CFG!%RESET%
echo    Compiler   : %GREEN%!COMPILER!%RESET%
echo    Run After  : %GREEN%!RUN_AFTER!%RESET%
echo    Use vcpkg  : %GREEN%!USE_VCPKG!%RESET%
echo.
echo  %CYAN%+-------- Actions ---------------------------------+%RESET%
echo    %BOLD%[1]%RESET%  Quick Build         %DIM%(cmake --build only)%RESET%
echo    %BOLD%[2]%RESET%  Full Build + Setup  %DIM%(build-local.ps1)%RESET%
echo    %BOLD%[3]%RESET%  Init Dev Env        %DIM%(build\init-windows-dev.ps1)%RESET%
echo  %CYAN%+-------- Settings --------------------------------+%RESET%
echo    %BOLD%[4]%RESET%  Change Config       %DIM%(currently: !CFG!)%RESET%
echo    %BOLD%[5]%RESET%  Change Compiler     %DIM%(currently: !COMPILER!)%RESET%
echo    %BOLD%[6]%RESET%  Toggle Run After    %DIM%(currently: !RUN_AFTER!)%RESET%
echo    %BOLD%[7]%RESET%  Toggle vcpkg        %DIM%(currently: !USE_VCPKG!)%RESET%
echo  %CYAN%+--------------------------------------------------+%RESET%
echo    %BOLD%[8]%RESET%  Exit
echo  %CYAN%+--------------------------------------------------+%RESET%
echo.
choice /c 12345678 /n /m "  Select an option [1-8]: "
set "CHOICE=%errorlevel%"

if "!CHOICE!"=="1" goto ACTION_QUICK_BUILD
if "!CHOICE!"=="2" goto ACTION_FULL_BUILD
if "!CHOICE!"=="3" goto ACTION_INIT_ENV
if "!CHOICE!"=="4" goto MENU_CONFIG
if "!CHOICE!"=="5" goto MENU_COMPILER
if "!CHOICE!"=="6" goto TOGGLE_RUN
if "!CHOICE!"=="7" goto TOGGLE_VCPKG
if "!CHOICE!"=="8" goto EXIT_MENU
goto MAIN_MENU

:: -------------------------------------------------------
:MENU_CONFIG
cls
echo %CYAN%%BOLD%
echo  +===============================================+
echo  ^|            Select Build Configuration         ^|
echo  +===============================================+%RESET%
echo.
echo    %BOLD%[1]%RESET%  Release          %DIM%(optimized, no debug info)%RESET%
echo    %BOLD%[2]%RESET%  Debug            %DIM%(full debug symbols)%RESET%
echo    %BOLD%[3]%RESET%  RelWithDebInfo   %DIM%(optimized + debug info)%RESET%
echo    %BOLD%[4]%RESET%  MinSizeRel       %DIM%(smallest binary)%RESET%
echo    %BOLD%[5]%RESET%  Back
echo.
choice /c 12345 /n /m "  Select [1-5]: "
set "CHOICE=%errorlevel%"
if "!CHOICE!"=="1" set "CFG=Release"
if "!CHOICE!"=="2" set "CFG=Debug"
if "!CHOICE!"=="3" set "CFG=RelWithDebInfo"
if "!CHOICE!"=="4" set "CFG=MinSizeRel"
goto MAIN_MENU

:: -------------------------------------------------------
:MENU_COMPILER
cls
echo %CYAN%%BOLD%
echo  +===============================================+
echo  ^|               Select Compiler                 ^|
echo  +===============================================+%RESET%
echo.
echo    %BOLD%[1]%RESET%  llvm   %DIM%(Clang/LLVM via Ninja -- default)%RESET%
echo    %BOLD%[2]%RESET%  msvc   %DIM%(Visual Studio Build Tools)%RESET%
echo    %BOLD%[3]%RESET%  Back
echo.
choice /c 123 /n /m "  Select [1-3]: "
set "CHOICE=%errorlevel%"
if "!CHOICE!"=="1" set "COMPILER=llvm"
if "!CHOICE!"=="2" set "COMPILER=msvc"
goto MAIN_MENU

:: -------------------------------------------------------
:TOGGLE_RUN
if "!RUN_AFTER!"=="No"  (set "RUN_AFTER=Yes" & goto MAIN_MENU)
set "RUN_AFTER=No"
goto MAIN_MENU

:TOGGLE_VCPKG
if "!USE_VCPKG!"=="No"  (set "USE_VCPKG=Yes" & goto MAIN_MENU)
set "USE_VCPKG=No"
goto MAIN_MENU

:: -------------------------------------------------------
:ACTION_QUICK_BUILD
cls
echo %GREEN%%BOLD%[Quick Build] cmake --build .build --config !CFG!%RESET%
echo.
cmake --build .build --config !CFG!
set "BUILD_RC=!errorlevel!"
echo.
if "!BUILD_RC!"=="0" (
    echo %GREEN%Build succeeded.%RESET%
) else (
    echo %RED%Build FAILED (exit code !BUILD_RC!).%RESET%
)
echo.
pause
goto MAIN_MENU

:: -------------------------------------------------------
:ACTION_FULL_BUILD
cls
echo %GREEN%%BOLD%[Full Build] build-local.ps1 -Config !CFG! -Compiler !COMPILER!%RESET%
echo.
set "PS_ARGS=-ExecutionPolicy Bypass -File build-local.ps1 -Config !CFG! -Compiler !COMPILER!"
if "!RUN_AFTER!"=="Yes" set "PS_ARGS=!PS_ARGS! -Run"
if "!USE_VCPKG!"=="Yes" set "PS_ARGS=!PS_ARGS! -UseVcpkg"
powershell !PS_ARGS!
set "BUILD_RC=!errorlevel!"
echo.
if "!BUILD_RC!"=="0" (
    echo %GREEN%Build succeeded.%RESET%
) else (
    echo %RED%Build FAILED (exit code !BUILD_RC!).%RESET%
)
echo.
pause
goto MAIN_MENU

:: -------------------------------------------------------
:ACTION_INIT_ENV
cls
echo %GREEN%%BOLD%[Init Dev Env] build\init-windows-dev.ps1 -Compiler !COMPILER!%RESET%
echo.
powershell -ExecutionPolicy Bypass -File build\init-windows-dev.ps1 -Compiler !COMPILER!
set "BUILD_RC=!errorlevel!"
echo.
if "!BUILD_RC!"=="0" (
    echo %GREEN%Dev environment initialized.%RESET%
) else (
    echo %RED%Initialisation reported errors (exit code !BUILD_RC!).%RESET%
)
echo.
pause
goto MAIN_MENU

:: -------------------------------------------------------
:EXIT_MENU
echo.
echo %CYAN%Goodbye!%RESET%
echo.
endlocal
exit /b 0