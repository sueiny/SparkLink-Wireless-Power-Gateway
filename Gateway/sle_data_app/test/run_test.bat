@echo off
REM SLE网关测试 - Windows启动脚本
REM 用法: run_test.bat [COM22] [COM26] [COM28]

echo ========================================
echo SLE网关性能测试工具
echo ========================================
echo.

REM 检查Python是否安装
python --version >nul 2>&1
if errorlevel 1 (
    echo 错误: 未找到Python，请安装Python 3.6+
    echo 下载地址: https://www.python.org/downloads/
    pause
    exit /b 1
)

REM 检查pyserial是否安装
python -c "import serial" >nul 2>&1
if errorlevel 1 (
    echo 正在安装pyserial...
    pip install pyserial
    if errorlevel 1 (
        echo 错误: pyserial安装失败
        pause
        exit /b 1
    )
)

REM 运行测试脚本
if "%~1"=="" (
    echo 使用默认串口: COM22 COM26 COM28
    python send_test.py COM22 COM26 COM28
) else (
    python send_test.py %*
)

echo.
echo 测试完成！
pause
