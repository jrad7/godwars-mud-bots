@echo off
echo Building Dystopia MUD via WSL...
echo.
echo If this fails with missing libraries, run in WSL:
echo   sudo apt install build-essential zlib1g-dev libxcrypt-dev
echo.
wsl --cd "%~dp0src" bash -c "make clean && make"
if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful!
) else (
    echo.
    echo Build failed. See errors above.
)
pause
