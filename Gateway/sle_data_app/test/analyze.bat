@echo off
REM SLE网关测试 - Windows日志分析脚本
REM 用法: analyze.bat [sle_app.log]

echo ========================================
echo SLE网关日志分析工具
echo ========================================
echo.

REM 检查 Windows Python Launcher 是否安装
py -3 --version >nul 2>&1
if errorlevel 1 (
    echo 错误: 未找到 py -3，请安装Python 3.6+并启用 Python Launcher
    pause
    exit /b 1
)

REM 检查日志文件
if "%~1"=="" (
    echo 用法: analyze.bat ^<日志文件路径^>
    echo.
    echo 示例:
    echo   analyze.bat sle_app.log
    echo   analyze.bat C:\logs\sle_app.log
    echo.
    echo 或者从网关获取日志:
    echo   adb pull /tmp/sle_app.log .
    echo   analyze.bat sle_app.log
    pause
    exit /b 1
)

if not exist "%~1" (
    echo 错误: 文件不存在: %~1
    pause
    exit /b 1
)

REM 运行分析脚本
py -3 analyze_log.py "%~1"

echo.
pause
