@echo off
rem Build sk700lcd.exe with MSVC (no CRT). Run from any prompt.
setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer
set PATH=%VSWHERE%;%PATH%
if not defined VCINSTALLDIR (
  for /f "usebackq delims=" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
)
if not defined VCINSTALLDIR (
  if not defined VSDIR ( echo MSVC not found & exit /b 1 )
  call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
cd /d "%~dp0"
if not exist build mkdir build

cl /nologo /c /utf-8 /O1 /GS- /Gy /W4 /WX /D UNICODE /D _UNICODE src\sk700lcd.c /Fo:build\sk700lcd.obj || exit /b 1
rc /nologo /fo build\sk700lcd.res src\sk700lcd.rc || exit /b 1
link /nologo /NODEFAULTLIB /ENTRY:entry /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF /MANIFEST:NO ^
  /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /CETCOMPAT ^
  build\sk700lcd.obj build\sk700lcd.res kernel32.lib advapi32.lib hid.lib cfgmgr32.lib ^
  /OUT:build\sk700lcd.exe || exit /b 1
echo built build\sk700lcd.exe
