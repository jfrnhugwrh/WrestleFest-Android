@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0convert-audio.ps1" %*
exit /b %ERRORLEVEL%
