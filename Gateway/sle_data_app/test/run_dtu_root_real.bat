@echo off
REM DTU root RUN-mode sender for Windows.
REM Defaults: COM19 COM23 COM36, 115200 8N1, 5s active warmup.

setlocal

py -3 --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: py -3 not found. Install Python 3.6+ with Python Launcher.
    echo Download: https://www.python.org/downloads/
    pause
    exit /b 1
)

py -3 -c "import serial" >nul 2>&1
if errorlevel 1 (
    echo Installing pyserial...
    py -3 -m pip install pyserial
    if errorlevel 1 (
        echo ERROR: pyserial install failed.
        pause
        exit /b 1
    )
)

if "%~1"=="" goto default_ports
goto custom_args

:default_ports
echo Using default DTU root ports: COM19 COM23 COM36
py -3 dtu_root_run_sender.py COM19 COM23 COM36 --duration 60 --interval 5 --warmup-sec 5 --warmup-interval 0.2 --warmup-text 12123213 --post-warmup-delay 8 --hold-open 10
goto done

:custom_args
py -3 dtu_root_run_sender.py %*
goto done

:done

endlocal
