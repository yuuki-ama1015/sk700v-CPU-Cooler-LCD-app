@echo off
rem Build and run the decode unit tests (uses the normal CRT).
setlocal
set PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%
if not defined VCINSTALLDIR (
  for /f "usebackq delims=" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
)
if not defined VCINSTALLDIR (
  if not defined VSDIR ( echo MSVC not found & exit /b 1 )
  call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
cd /d "%~dp0"
if not exist ..\build mkdir ..\build
cl /nologo /utf-8 /W4 /D UNICODE /D _UNICODE /D _CRT_SECURE_NO_WARNINGS test_decode.c /Fe:..\build\test_decode.exe /Fo:..\build\test_decode.obj ^
  kernel32.lib advapi32.lib hid.lib cfgmgr32.lib >nul || ( echo build failed & exit /b 1 )
..\build\test_decode.exe
