@echo off
setlocal
rem Experimental coupled 30 Hz input/simulation/render; original save, 3x DLAA.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -ReflexSyncMode on -ReflexCoupledPhysics -ReflexTimingDirectory "%~dp0..\out\reflex-timing" %*
exit /b %errorlevel%
