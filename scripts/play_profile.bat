@echo off
setlocal
rem Same 3x DLAA / Reflex rendering, with GPU timestamps for local profiling.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -ReflexSyncMode on -GpuProfile -ReflexTimingDirectory "%~dp0..\out\reflex-timing" %*
exit /b %errorlevel%
