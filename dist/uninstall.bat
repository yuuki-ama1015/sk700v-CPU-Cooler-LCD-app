@echo off
rem sk700lcd uninstaller: removes the SK700LCD service, the installed exe and its settings.
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

echo ============================================
echo  sk700lcd uninstall
echo ============================================
echo.
if exist "%~dp0sk700lcd.exe" (
  "%~dp0sk700lcd.exe" uninstall
) else if exist "%ProgramFiles%\SK700LCD\sk700lcd.exe" (
  "%ProgramFiles%\SK700LCD\sk700lcd.exe" uninstall
) else (
  echo sk700lcd is not installed.
)
echo.
pause
