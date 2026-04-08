@echo off
setlocal

set "PLAYER_DIR=%~dp0player"

for %%F in ("%PLAYER_DIR%\*") do (
    if /i not "%%~nxF"=="Kast" if /i not "%%~nxF"=="Claude" (
        echo %%~nxF | findstr /r "^\." >nul 2>&1
        if errorlevel 1 del "%%F"
    )
)

if exist "%~dp0txt\bots.txt" (
    del "%~dp0txt\bots.txt"
    echo Deleted txt\bots.txt.
)

echo Done. Kept Kast and Claude.
endlocal
