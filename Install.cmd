@echo off
rem Double-click to install (or re-run to re-flash / reconfigure) the MSFS CYD HUD.
rem Any arguments are passed on, e.g.  Install.cmd -Uninstall
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\install.ps1" %*
echo.
pause
