@echo off
rem Pico W 状态屏桌面仿真 - 双击启动（T=切主题 F=断流演示 ESC=退出）
cd /d "%~dp0.."
python desktop-sim\pico_sim.py
if errorlevel 1 pause
