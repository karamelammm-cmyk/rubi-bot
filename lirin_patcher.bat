@echo off
setlocal enabledelayedexpansion
title Lirin Patcher
color 0A

set "REPO_BASE=https://raw.githubusercontent.com/karamelammm-cmyk/rubi-bot/main"
set "SCRIPT_DIR=%~dp0"
set "CLIENT_DIR=%SCRIPT_DIR%"
set "UI_DIR=%SCRIPT_DIR%ui"
set "LOCAL_VER_FILE=%SCRIPT_DIR%lirin_version_local.txt"
set "GAME_EXE=Lirin.exe"

echo ===================================
echo        LIRIN PATCHER v1.0
echo ===================================
echo.

:: Create directories if missing
if not exist "%UI_DIR%" mkdir "%UI_DIR%"

:: ==========================================
:: PHASE 1: Version Check
:: ==========================================
set LOCAL_VER=0
if exist "%LOCAL_VER_FILE%" (
    set /p LOCAL_VER=<"%LOCAL_VER_FILE%"
)

echo [*] Checking for updates...
curl -s -L -o "%TEMP%\lirin_remote_ver.txt" "%REPO_BASE%/lirin_version.txt" 2>nul
if not exist "%TEMP%\lirin_remote_ver.txt" (
    echo [!] Could not reach update server.
    goto :LAUNCH
)

set /p REMOTE_VER=<"%TEMP%\lirin_remote_ver.txt"
del /F "%TEMP%\lirin_remote_ver.txt" >nul 2>&1

echo     Local version:  v%LOCAL_VER%
echo     Remote version: v%REMOTE_VER%

if "%LOCAL_VER%"=="%REMOTE_VER%" (
    echo.
    echo [OK] Already up to date.
    goto :LAUNCH
)

echo.
echo [*] Update available: v%LOCAL_VER% -^> v%REMOTE_VER%
echo.

:: ==========================================
:: PHASE 2: Show Changelog
:: ==========================================
echo [*] Downloading changelog...
curl -s -L -o "%TEMP%\lirin_changelog.txt" "%REPO_BASE%/changelog.txt" 2>nul
if exist "%TEMP%\lirin_changelog.txt" (
    echo.
    echo ------- CHANGELOG -------
    :: Show entries newer than local version
    set "SHOW=0"
    for /f "usebackq delims=" %%L in ("%TEMP%\lirin_changelog.txt") do (
        set "LINE=%%L"
        :: Check if line is a version header [vN]
        echo %%L | findstr /r "\[v[0-9]*\]" >nul 2>&1
        if !errorlevel!==0 (
            :: Extract version number
            for /f "tokens=1 delims=]" %%V in ("%%L") do (
                set "HEADER_VER=%%V"
                set "HEADER_VER=!HEADER_VER:[v=!"
                if !HEADER_VER! GTR !LOCAL_VER! (
                    set "SHOW=1"
                ) else (
                    set "SHOW=0"
                )
            )
        )
        if !SHOW!==1 echo   %%L
    )
    echo -------------------------
    del /F "%TEMP%\lirin_changelog.txt" >nul 2>&1
)
echo.

:: ==========================================
:: PHASE 3: Kill Game if Running
:: ==========================================
tasklist /FI "IMAGENAME eq %GAME_EXE%" 2>nul | findstr /i "%GAME_EXE%" >nul
if %errorlevel%==0 (
    echo [!] %GAME_EXE% is running. Closing...
    taskkill /F /IM "%GAME_EXE%" >nul 2>&1
    timeout /t 3 /nobreak >nul
)

:: Kill UI server if running
taskkill /F /FI "WINDOWTITLE eq LirinUI*" >nul 2>&1

:: ==========================================
:: PHASE 4: Download Files
:: ==========================================
echo [*] Downloading updates...

:: VERSION.dll
echo     [1/3] VERSION.dll...
if exist "%CLIENT_DIR%VERSION.dll" (
    del /F "%CLIENT_DIR%VERSION.dll" >nul 2>&1
    if exist "%CLIENT_DIR%VERSION.dll" (
        timeout /t 2 /nobreak >nul
        del /F "%CLIENT_DIR%VERSION.dll" >nul 2>&1
    )
    if exist "%CLIENT_DIR%VERSION.dll" (
        echo     [FAIL] Could not delete VERSION.dll - file is locked!
        echo     Close the game and try again.
        pause
        exit /b 1
    )
)
curl -s -L -o "%CLIENT_DIR%VERSION.dll" "%REPO_BASE%/dist/VERSION.dll"
for %%A in ("%CLIENT_DIR%VERSION.dll") do if %%~zA LSS 1024 (
    echo     [FAIL] Download failed or file too small!
    pause
    exit /b 1
)
echo     [OK]

:: lirin_ui.py
echo     [2/3] lirin_ui.py...
if exist "%UI_DIR%\lirin_ui.py" del /F "%UI_DIR%\lirin_ui.py" >nul 2>&1
curl -s -L -o "%UI_DIR%\lirin_ui.py" "%REPO_BASE%/dist/lirin_ui.py"
echo     [OK]

:: index.html
echo     [3/3] index.html...
if exist "%UI_DIR%\index.html" del /F "%UI_DIR%\index.html" >nul 2>&1
curl -s -L -o "%UI_DIR%\index.html" "%REPO_BASE%/dist/index.html"
echo     [OK]

:: Write local version
echo %REMOTE_VER%> "%LOCAL_VER_FILE%"

echo.
echo ===================================
echo     Update complete! v%REMOTE_VER%
echo ===================================

:: ==========================================
:: PHASE 5: Launch
:: ==========================================
:LAUNCH
echo.
choice /M "Launch game?" /C YN /D Y /T 5
if errorlevel 2 goto :END
if exist "%CLIENT_DIR%%GAME_EXE%" (
    echo [*] Starting %GAME_EXE%...
    cd /d "%CLIENT_DIR%"
    start "" "%GAME_EXE%"
) else (
    echo [!] %GAME_EXE% not found in %CLIENT_DIR%
)

:END
echo.
timeout /t 3 /nobreak >nul
exit /b 0
