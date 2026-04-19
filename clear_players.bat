@echo off
setlocal

set "PLAYER_DIR=%~dp0player"
set "BOT_DIR=%~dp0bot"

for %%F in ("%PLAYER_DIR%\*") do (
    if /i not "%%~nxF"=="Kast" if /i not "%%~nxF"=="Claude" if /i not "%%~nxF"=="Vict" if /i not "%%~nxF"=="Gemma" (
        echo %%~nxF | findstr /r "^\." >nul 2>&1
        if errorlevel 1 del "%%F"
    )
)

del /q "%PLAYER_DIR%\backup\*" >nul 2>&1
del /q "%PLAYER_DIR%\store\*" >nul 2>&1

del /q "%BOT_DIR%\*" >nul 2>&1
del /q "%BOT_DIR%\backup\*" >nul 2>&1
del /q "%BOT_DIR%\store\*" >nul 2>&1

if exist "%~dp0txt\bots.txt" (
    del "%~dp0txt\bots.txt"
    echo Deleted txt\bots.txt.
)

echo Done. Kept Kast, Claude, Vict, and Gemma.
endlocal
