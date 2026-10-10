@echo off
rem SC6103 demo launcher for Windows: double-click, then choose a number.
rem Open one window per program (server, web client, C++ client).
setlocal
cd /d "%~dp0"
title SC6103 demo launcher

:menu
cls
echo ================= SC6103 demo launcher =================
echo   Server  (computer 1)
echo     1  Server - main flow          at-most-once, no loss
echo     2  Server - semantics round 1  at-least-once, reply loss 0.5, seed 6103
echo     3  Server - semantics round 2  at-most-once,  reply loss 0.5, seed 6103
echo     4  Server - request-loss demo  at-least-once, no reply loss
echo.
echo   Clients (computers 2 and 3)
echo     5  Web client  (bridge + browser)
echo     6  C++ client  at-most-once     main flow / round 2
echo     7  C++ client  at-least-once    round 1
echo     8  C++ client  request loss 0.5, seed 7   (with server 4)
echo.
echo     0  Exit
echo ========================================================
set "CHOICE="
set /p "CHOICE=Choose: "
if "%CHOICE%"=="1" (set "ARGS=--semantics=at-most-once --loss-rate=0" & goto server)
if "%CHOICE%"=="2" (set "ARGS=--semantics=at-least-once --loss-rate=0.5 --seed=6103" & goto server)
if "%CHOICE%"=="3" (set "ARGS=--semantics=at-most-once --loss-rate=0.5 --seed=6103" & goto server)
if "%CHOICE%"=="4" (set "ARGS=--semantics=at-least-once --loss-rate=0" & goto server)
if "%CHOICE%"=="5" goto web
if "%CHOICE%"=="6" (set "ARGS=--semantics=at-most-once" & goto client)
if "%CHOICE%"=="7" (set "ARGS=--semantics=at-least-once" & goto client)
if "%CHOICE%"=="8" (set "ARGS=--semantics=at-least-once --loss-rate=0.5 --seed=7" & goto client)
if "%CHOICE%"=="0" exit /b 0
if "%CHOICE%"=="" exit /b 0
goto menu

:server
if not exist server.exe (
  echo server.exe is missing. Put server.exe and client.exe in:
  echo   %CD%
  pause
  goto menu
)
echo.
echo ^> server.exe --port=8080 %ARGS%
echo   Stop with Ctrl+C, then answer N to come back to this menu.
echo.
.\server.exe --port=8080 %ARGS%
echo.
pause
goto menu

:client
if not exist client.exe (
  echo client.exe is missing. Put server.exe and client.exe in:
  echo   %CD%
  pause
  goto menu
)
call :askip
echo.
echo ^> client.exe --server=%IP% --port=8080 %ARGS%
echo.
.\client.exe --server=%IP% --port=8080 %ARGS%
echo.
pause
goto menu

:web
call :askip
call :findpy
if not defined PY (
  echo Python 3 was not found. Install it from python.org and tick
  echo "Add python.exe to PATH", then try again.
  pause
  goto menu
)
echo.
echo ^> %PY% web\bridge.py --server=%IP% --port=8080 --open
echo   The browser opens http://localhost:8000/ . Keep this window open.
echo   Stop with Ctrl+C, then answer N to come back to this menu.
echo.
%PY% web\bridge.py --server=%IP% --port=8080 --open
echo.
pause
goto menu

rem ---- ask for the server IP, remembering the last one in server_ip.txt
:askip
set "IP=127.0.0.1"
if exist server_ip.txt set /p IP=<server_ip.txt
set "NEWIP="
echo.
echo Server IP: 127.0.0.1 if the server runs on this computer,
echo            otherwise the IP that ipconfig shows on computer 1.
set /p "NEWIP=Server IP [%IP%]: "
if not "%NEWIP%"=="" set "IP=%NEWIP%"
> server_ip.txt echo %IP%
exit /b 0

rem ---- find a working Python 3 (the Microsoft Store "python" stub does not count)
:findpy
set "PY="
py -3 -c "import sys" >nul 2>&1 && (set "PY=py -3" & exit /b 0)
python -c "import sys; sys.exit(sys.version_info < (3, 8))" >nul 2>&1 && (set "PY=python" & exit /b 0)
for %%P in ("%USERPROFILE%\anaconda3\python.exe" "%USERPROFILE%\miniconda3\python.exe" "C:\ProgramData\anaconda3\python.exe" "D:\anaconda3\python.exe" "D:\Anaconda3\python.exe" "C:\anaconda3\python.exe" "%LOCALAPPDATA%\Programs\Python\Python313\python.exe" "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" "%LOCALAPPDATA%\Programs\Python\Python311\python.exe" "%LOCALAPPDATA%\Programs\Python\Python310\python.exe") do (
  if exist %%P (set PY="%%~P" & exit /b 0)
)
exit /b 0
