@echo off
echo Starting Dystopia MUD via WSL...
echo Connect with: telnet localhost 8000
echo Press Ctrl+C to stop.
echo.
wsl --cd "%~dp0src" bash -c "chmod +x startup.sh && ./startup.sh"
pause
