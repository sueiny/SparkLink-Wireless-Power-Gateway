@echo off
setlocal
set APP_DIR=%~dp0
set VENV_DIR=%APP_DIR%.venv-d3qn

python -m venv "%VENV_DIR%"
"%VENV_DIR%\Scripts\python.exe" -m pip install --upgrade pip
"%VENV_DIR%\Scripts\python.exe" -m pip install torch --index-url https://download.pytorch.org/whl/cpu
"%VENV_DIR%\Scripts\python.exe" -m pip install gym==0.25.2 "numpy<2" networkx tqdm tensorboard openpyxl pyserial matplotlib

echo D3QN environment ready: %VENV_DIR%
echo Use: set PYTHONPATH=app\广播组网上位机\app && "%VENV_DIR%\Scripts\python.exe" -m pc_d3qn_cli.main train
