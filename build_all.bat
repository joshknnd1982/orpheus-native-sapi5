@echo off
setlocal

rem The version lives in src/version.h and is read back here, so the installer
rem filename, the release tag and every binary's file properties all carry the
rem same number and a user can tell which build they have.
rem findstr does a substring match, so the pattern carries the trailing space:
rem without it ORPHEUS_VERSION_STRING_W matches too and wins, leaving the
rem version as L"1.1.0".
for /f "tokens=3 delims= " %%v in ('findstr /c:"#define ORPHEUS_VERSION_STRING " src\version.h') do (
    set "ORPHEUS_VERSION=%%~v"
)
if not defined ORPHEUS_VERSION (
    echo ERROR: could not read the version from src\version.h
    exit /b 1
)

echo Orpheus Native SAPI5 Build %ORPHEUS_VERSION%
echo.

set BUILD_DIR_X86=build_x86
set BUILD_DIR_X64=build_x64
set OUTPUT_DIR=output

if not exist %BUILD_DIR_X86% mkdir %BUILD_DIR_X86%
if not exist %BUILD_DIR_X64% mkdir %BUILD_DIR_X64%
if not exist %OUTPUT_DIR% mkdir %OUTPUT_DIR%
if not exist %OUTPUT_DIR%\x64 mkdir %OUTPUT_DIR%\x64

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Please install Visual Studio 2022 or later.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALLDIR=%%i\"
)

if not defined VSINSTALLDIR (
    echo ERROR: Visual Studio installation not found.
    exit /b 1
)

echo Found Visual Studio at: %VSINSTALLDIR%
echo.
echo Building x86 version...
echo.

cmake -A Win32 -S . -B %BUILD_DIR_X86%
if errorlevel 1 (
    echo ERROR: CMake x86 configuration failed.
    exit /b 1
)

cmake --build %BUILD_DIR_X86% --config Release
if errorlevel 1 (
    echo ERROR: x86 build failed.
    exit /b 1
)

echo Building x64 version...
echo.

cmake -A x64 -S . -B %BUILD_DIR_X64%
if errorlevel 1 (
    echo ERROR: CMake x64 configuration failed.
    exit /b 1
)

cmake --build %BUILD_DIR_X64% --config Release
if errorlevel 1 (
    echo ERROR: x64 build failed.
    exit /b 1
)

echo.
echo Running the registration / uninstall test...
echo.

"%BUILD_DIR_X86%\bin\Release\registry_test.exe"
if errorlevel 1 (
    echo ERROR: the x86 registry test failed. The uninstall path is not safe to ship.
    exit /b 1
)
"%BUILD_DIR_X64%\bin\Release\registry_test.exe"
if errorlevel 1 (
    echo ERROR: the x64 registry test failed. The uninstall path is not safe to ship.
    exit /b 1
)

echo.
echo Staging files in output directory...
echo.

copy /Y "%BUILD_DIR_X86%\bin\Release\OrpheusNativeSAPI.dll" "%OUTPUT_DIR%\"
copy /Y "%BUILD_DIR_X86%\bin\Release\OrpheusNativeConfig.exe" "%OUTPUT_DIR%\"
copy /Y "%BUILD_DIR_X64%\bin\Release\OrpheusNativeSAPI.dll" "%OUTPUT_DIR%\x64\"
copy /Y "bin\orpheus-native-host.exe" "%OUTPUT_DIR%\"

robocopy "bin\orpheus" "%OUTPUT_DIR%\orpheus" /MIR /NFL /NDL /NJH /NJS
if errorlevel 8 (
    echo ERROR: Failed to copy the Orpheus engine data.
    exit /b 1
)

rem The engine records its own directory inside orpheus.sys and rewrites the
rem file on startup, so the shipped copy must carry an empty path rather than
rem whatever path this machine happened to build in.
copy /Y "installer\orpheus.sys.template" "%OUTPUT_DIR%\orpheus\orpheus.sys"

rem The Dolphin Synthesizer Access Manager is not used: every voice renders
rem byte-identically without it. Make sure a stale copy never reaches a build.
if exist "%OUTPUT_DIR%\orpheus\sam" rmdir /s /q "%OUTPUT_DIR%\orpheus\sam"

echo.
echo Building installer...
echo.

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo ERROR: Inno Setup 6 compiler not found.
    exit /b 1
)

"%ISCC%" /O"%OUTPUT_DIR%" "installer\orpheus_native.iss"
if errorlevel 1 (
    echo ERROR: Installer build failed.
    exit /b 1
)

echo.
echo Build completed successfully.
echo Installer: %OUTPUT_DIR%\OrpheusNativeSAPI_Setup_%ORPHEUS_VERSION%.exe
endlocal
