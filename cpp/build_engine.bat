@echo off
rem Engine + headless runner only (no editor) -- quick iteration on the interpreter.
setlocal enabledelayedexpansion
rem vswhere is invoked by explicit path: this shell may not search the
rem current directory for executables (NoDefaultCurrentDirectoryInExePath).
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%%VSW%%" -latest -prerelease -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio 2022 with the C++ workload is required. & exit /b 1)
set "ARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ARCH=arm64"
call "!VSDIR!\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul || exit /b 1
cd /d "%~dp0"
cmake -S . -B build-engine -G Ninja -DCMAKE_BUILD_TYPE=Release -DSL_BUILD_APP=OFF || exit /b 1
cmake --build build-engine || exit /b 1
