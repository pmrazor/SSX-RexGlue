@echo off
setlocal
if exist "%~dp0SSX.Options.exe" (
  start "" "%~dp0SSX.Options.exe" %*
  exit /b
)
if exist "%~dp0..\out\build\win-amd64-experimental-submit\SSX.Options.exe" (
  start "" "%~dp0..\out\build\win-amd64-experimental-submit\SSX.Options.exe" %*
  exit /b
)
set "OPTIONS=%~dp0launcher\Options.ps1"
if not exist "%OPTIONS%" set "OPTIONS=%~dp0..\launcher\Options.ps1"
start "" powershell.exe -NoProfile -STA -WindowStyle Hidden -ExecutionPolicy Bypass -File "%OPTIONS%" %*
