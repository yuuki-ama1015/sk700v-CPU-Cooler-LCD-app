@echo off
rem sk700lcd installer: installs PawnIO if missing, then registers the SK700LCD service.
setlocal
cd /d "%~dp0"

rem --- elevate once (sk700lcd.exe would ask for every call otherwise) ---
net session >nul 2>&1
if errorlevel 1 (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

echo ============================================
echo  sk700lcd install
echo ============================================
echo.

rem --- PawnIO driver (required) ---
sc query PawnIO >nul 2>&1
if errorlevel 1 (
  echo PawnIO driver is not installed. Installing it with winget...
  where winget >nul 2>&1
  if errorlevel 1 (
    echo [ERROR] winget was not found.
    echo Please install PawnIO from https://pawnio.eu/ and run install.bat again.
    goto :end
  )
  winget install --id namazso.PawnIO -e --accept-source-agreements --accept-package-agreements
  sc query PawnIO >nul 2>&1
  if errorlevel 1 (
    echo [ERROR] PawnIO could not be installed.
    echo Please install PawnIO from https://pawnio.eu/ and run install.bat again.
    goto :end
  )
) else (
  echo PawnIO driver: OK
)
echo.

rem --- MasterCraft would send to the LCD at the same time ---
tasklist /FI "IMAGENAME eq MasterCraft.exe" /NH 2>nul | find /I "MasterCraft.exe" >nul
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
echo Done. The LCD should show the CPU temperature within a few seconds.
echo You can delete this folder now. To uninstall later, run uninstall.bat
echo (or "C:\Program Files\SK700LCD\sk700lcd.exe" uninstall^).

:end
echo.
pause
