@echo off
setlocal
cd /d "%~dp0"
if not exist "build\ArknightsPet.exe" (
  echo Building native pet...
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
  if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
  )
)
start "" "%~dp0build\ArknightsPet.exe"
endlocal
