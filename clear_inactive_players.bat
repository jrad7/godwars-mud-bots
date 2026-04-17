@echo off
setlocal

set "PLAYER_DIR=%~dp0player"

del /q "%PLAYER_DIR%\retired\*" >nul 2>&1
del /q "%PLAYER_DIR%\backup\*" >nul 2>&1
del /q "%PLAYER_DIR%\store\*" >nul 2>&1

echo Done clearing retired, backup, and store directories. Active players were left intact.
endlocal
