@echo off
setlocal
cd /d "%~dp0"

if not exist "node_modules\electron\dist\electron.exe" (
  echo Electron runtime is missing.
  echo Run "pnpm install" once in this folder, then try again.
  pause
  exit /b 1
)

start "" "%~dp0node_modules\electron\dist\electron.exe" "%~dp0"
