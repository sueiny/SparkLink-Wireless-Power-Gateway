@echo off
setlocal
cd /d "%~dp0"
python start_pc_dijkstra_ui.py %*
if errorlevel 1 pause
