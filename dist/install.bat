@echo off
rem sk700lcd installer: installs PawnIO if missing, then registers the SK700LCD service.
setlocal
cd /d "%~dp0"
rem full path: a GNU find earlier in PATH (Git, MSYS) must not be picked up
set "FINDSTR=%SystemRoot%\System32\findstr.exe"

rem --- run from the extracted folder, not from inside the zip ---
if not exist "%~dp0sk700lcd.exe" (
  echo [ERROR] sk700lcd.exe was not found next to install.bat.
  echo Extract the whole zip first ^(right-click - Extract All^), then run install.bat in the extracted folder.
  echo.
  pause
  exit /b 1
)

rem --- elevate once (sk700lcd.exe would ask for every call otherwise) ---
rem fltmc needs administrator rights and, unlike "net session", no running Server service.
fltmc >nul 2>&1
if errorlevel 1 (
  if "%~1"=="elevated" (
    echo [ERROR] Administrator rights are required. Right-click install.bat and choose "Run as administrator".
    echo.
    pause
    exit /b 1
  )
  set "SK700_SELF=%~f0"
  powershell -NoProfile -Command "Start-Process -FilePath $env:SK700_SELF -ArgumentList elevated -Verb RunAs"
  exit /b
)

echo ============================================
echo  sk700lcd install
echo ============================================
echo.

rem --- PawnIO driver (required): download the official installer unless the driver is running ---
rem "sc query" alone is not enough: an uninstalled PawnIO leaves a stopped service entry behind.
set "REBOOT="
sc query PawnIO 2>nul | "%FINDSTR%" /c:"RUNNING" >nul
if not errorlevel 1 (
  echo PawnIO driver: OK
  goto :pawnio_done
)
echo PawnIO driver is not installed. Installing it from the official release...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_pawnio.ps1"
set "PIO=%errorlevel%"
if "%PIO%"=="3010" goto :pawnio_reboot
if not "%PIO%"=="0" goto :pawnio_failed
sc query PawnIO 2>nul | "%FINDSTR%" /c:"RUNNING" >nul
if not errorlevel 1 (
  echo PawnIO driver: installed
  goto :pawnio_done
)
sc query PawnIO >nul 2>&1
if errorlevel 1 goto :pawnio_failed
:pawnio_reboot
set "REBOOT=1"
echo PawnIO driver: installed ^(it starts after a restart^)
goto :pawnio_done
:pawnio_failed
echo.
echo [ERROR] PawnIO could not be installed. Check the internet connection and run install.bat again,
echo or install PawnIO from https://pawnio.eu/ and run install.bat again.
goto :end
:pawnio_done
echo.

rem --- MasterCraft would send to the LCD at the same time ---
tasklist /FI "IMAGENAME eq MasterCraft.exe" /NH 2>nul | "%FINDSTR%" /i /c:"MasterCraft.exe" >nul
if not errorlevel 1 echo [WARNING] MasterCraft is running. Quit it, or uninstall it (Settings - Apps^), to avoid double sending.
sc query "MasterCraft Helper Service" >nul 2>&1
if not errorlevel 1 echo [NOTE] MasterCraft is installed. It is no longer needed and can be uninstalled from Settings - Apps.

rem --- install the service (copies the exe to Program Files) ---
"%~dp0sk700lcd.exe" install
if errorlevel 1 (
  echo [ERROR] install failed.
  goto :end
)
echo.
if defined REBOOT (
  echo Done. PawnIO asks for a restart: restart Windows, then the LCD will show the CPU temperature.
) else (
  echo Done. The LCD should show the CPU temperature within a few seconds.
)
echo You can delete this folder now. To uninstall later, run uninstall.bat
echo (or "C:\Program Files\SK700LCD\sk700lcd.exe" uninstall^).

:end
echo.
pause
