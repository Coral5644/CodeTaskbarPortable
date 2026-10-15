@echo off
start "" /wait "%~dp0CodeTaskbar.exe" --status
if not errorlevel 1 start "" notepad.exe "%~dp0status.txt"
