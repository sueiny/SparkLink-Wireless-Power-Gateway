@echo off
REM SLE网关测试 - Windows启动脚本
REM 用法: run_test.bat [COM22] [COM26] [COM28]

echo ========================================
echo SLE网关性能测试工具
echo ========================================
echo.

REM 检查 Windows Python Launcher 是否安装
py -3 --version >nul 2>&1
if errorlevel 1 (
    echo 错误: 未找到 py -3，请安装Python 3.6+并启用 Python Launcher
    echo 下载地址: https://www.python.org/downloads/
    pause
    exit /b 1
)

REM 检查pyserial是否安装
py -3 -c "import serial" >nul 2>&1
if errorlevel 1 (
    echo 正在安装pyserial...
    py -3 -m pip install pyserial
    if errorlevel 1 (
        echo 错误: pyserial安装失败
        pause
        exit /b 1
    )
)

REM 运行测试脚本
if "%~1"=="" (
    echo 使用默认串口: COM22 COM26 COM28
    py -3 send_test.py COM22 COM26 COM28
) else (
    py -3 send_test.py %*
)

echo.
echo 测试完成！
pause
