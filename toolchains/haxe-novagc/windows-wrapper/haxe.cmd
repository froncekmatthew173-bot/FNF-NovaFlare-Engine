@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Invoke-HaxeNova.ps1" %*
exit /b %ERRORLEVEL%
