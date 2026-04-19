@echo off
title Build Heartbeat
echo.
echo  This window prints a heartbeat every 30 seconds.
echo  Close it when the build finishes, or leave it open.
echo.
set N=0
:loop
set /a N=N+30
timeout /t 30 /nobreak >nul
echo  [heartbeat %N%s] build still running (check main build window for progress)
goto loop
