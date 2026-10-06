@echo off
setlocal
rem Native timing capture with the original save and working 3x DLAA settings.
rem Capture is diagnostic only; gameplay Reflex pacing and FG remain off.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -DLAA -ReflexTimingDirectory "%~dp0..\out\reflex-timing" %*
exit /b %errorlevel%
