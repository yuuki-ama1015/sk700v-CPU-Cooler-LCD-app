@echo off
rem sk700lcd uninstaller: removes the SK700LCD service, the installed exe and its settings.
setlocal
cd /d "%~dp0"

rem fltmc needs administrator rights and, unlike "net session", no running Server service.
fltmc >nul 2>&1
if errorlevel 1 (
  if "%~1"=="elevated" (
    echo [ERROR] Administrator rights are required. Right-click uninstall.bat and choose "Run as administrator".
    echo.
    pause
    exit /b 1
  )
  set "SK700_SELF=%~f0"
  powershell -NoProfile -Command "Start-Process -FilePath $env:SK700_SELF -ArgumentList elevated -Verb RunAs"
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
