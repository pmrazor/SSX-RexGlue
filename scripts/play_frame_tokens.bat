@echo off
setlocal
rem Same 3x DLAA and original local profile. No gameplay Reflex or FG activation.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -FrameTokens -ReflexTimingDirectory "%~dp0..\out\reflex-timing" %*
exit /b %errorlevel%
