@echo off
rem Builds SocialSandbox.exe and sl_run.exe into cpp\build\ with MSVC + Ninja.
rem Picks the native toolchain for this machine (arm64 or x64) and the newest
rem Visual Studio 2022 install via vswhere.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio 2022 with the C++ workload is required. & exit /b 1)
set "ARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ARCH=arm64"
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul || exit /b 1
cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
echo Built build\SocialSandbox.exe and build\sl_run.exe
