@echo off
setlocal enabledelayedexpansion
title Merlis Patcher
color 0B

set "REPO_BASE=https://raw.githubusercontent.com/karamelammm-cmyk/rubi-bot/main/merlis"
set "SCRIPT_DIR=%~dp0"
set "CLIENT_DIR=%SCRIPT_DIR%"
set "LOCAL_VER_FILE=%SCRIPT_DIR%merlis_version_local.txt"
set "GAME_EXE=merlis_v49.exe"
set "SOLVER_PY=%SCRIPT_DIR%captcha_solver.py"
set "KEY_FILE=C:\ProgramData\mdata\gemini_key.txt"
set "SELF=%~f0"

:: ==========================================
:: SELF-UPDATE: patcher updates itself
:: ==========================================
if "%1"=="--updated" goto :SKIP_SELF_UPDATE
curl -s -L -o "%TEMP%\merlis_patcher_new.bat" "%REPO_BASE%/merlis_patcher.bat" 2>nul
if exist "%TEMP%\merlis_patcher_new.bat" (
    fc /b "%SELF%" "%TEMP%\merlis_patcher_new.bat" >nul 2>&1
    if errorlevel 1 (
        copy /Y "%TEMP%\merlis_patcher_new.bat" "%SELF%" >nul 2>&1
        del /F "%TEMP%\merlis_patcher_new.bat" >nul 2>&1
        echo [*] Patcher updated. Restarting...
        start "" cmd /c ""%SELF%" --updated"
        exit /b 0
    )
    del /F "%TEMP%\merlis_patcher_new.bat" >nul 2>&1
)
:SKIP_SELF_UPDATE

echo ===================================
echo        MERLIS PATCHER v1.1
echo ===================================
echo.

:: ==========================================
:: PHASE 1: Version Check
:: ==========================================
set LOCAL_VER=0
if exist "%LOCAL_VER_FILE%" (
    set /p LOCAL_VER=<"%LOCAL_VER_FILE%"
)

echo [*] Checking for updates...
curl -s -L -o "%TEMP%\merlis_remote_ver.txt" "%REPO_BASE%/merlis_version.txt" 2>nul
if not exist "%TEMP%\merlis_remote_ver.txt" (
    echo [!] Could not reach update server.
    goto :SOLVER_START
)

set /p REMOTE_VER=<"%TEMP%\merlis_remote_ver.txt"
del /F "%TEMP%\merlis_remote_ver.txt" >nul 2>&1

echo     Local version:  v%LOCAL_VER%
echo     Remote version: v%REMOTE_VER%

if "%LOCAL_VER%"=="%REMOTE_VER%" (
    echo.
    echo [OK] Already up to date.
    goto :SOLVER_START
)

echo.
echo [*] Update available: v%LOCAL_VER% -^> v%REMOTE_VER%
echo.

:: ==========================================
:: PHASE 2: Show Changelog
:: ==========================================
echo [*] Downloading changelog...
curl -s -L -o "%TEMP%\merlis_changelog.txt" "%REPO_BASE%/changelog.txt" 2>nul
if exist "%TEMP%\merlis_changelog.txt" (
    echo.
    echo ------- CHANGELOG -------
    set "SHOW=0"
    for /f "usebackq delims=" %%L in ("%TEMP%\merlis_changelog.txt") do (
        set "LINE=%%L"
        echo %%L | findstr /r "\[v[0-9]*\]" >nul 2>&1
        if !errorlevel!==0 (
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
    del /F "%TEMP%\merlis_changelog.txt" >nul 2>&1
)
echo.

:: ==========================================
:: PHASE 3: Kill Game if Running
:: Note: captcha solver is NOT killed here, it's per-user and long-lived
:: ==========================================
tasklist /FI "IMAGENAME eq %GAME_EXE%" 2>nul | findstr /i "%GAME_EXE%" >nul
if %errorlevel%==0 (
    echo [!] %GAME_EXE% is running. Closing...
    taskkill /F /IM "%GAME_EXE%" >nul 2>&1
    timeout /t 3 /nobreak >nul
)

:: ==========================================
:: PHASE 4: Download Files
:: ==========================================
echo [*] Downloading updates...

:: VERSION.dll
echo     [1/2] VERSION.dll...
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
curl -s -L -o "%CLIENT_DIR%VERSION.dll" "%REPO_BASE%/VERSION.dll"
for %%A in ("%CLIENT_DIR%VERSION.dll") do if %%~zA LSS 1024 (
    echo     [FAIL] Download failed or file too small!
    pause
    exit /b 1
)
echo     [OK]

:: captcha_solver.py
echo     [2/2] captcha_solver.py...
curl -s -L -o "%SOLVER_PY%" "%REPO_BASE%/captcha_solver.py"
echo     [OK]

:: Write local version
echo %REMOTE_VER%> "%LOCAL_VER_FILE%"

echo.
echo ===================================
echo     Update complete! v%REMOTE_VER%
echo ===================================

:: ==========================================
:: PHASE 5: Captcha Solver Setup + Launch
:: ==========================================
:SOLVER_START
echo.
echo [*] Captcha solver setup...

:: Ensure mdata dir exists
if not exist "C:\ProgramData\mdata" mkdir "C:\ProgramData\mdata" 2>nul

:: Check for Gemini API key
if not exist "%KEY_FILE%" (
    echo.
    echo [!] No Gemini API key found at %KEY_FILE%
    echo     Captcha auto-solve will be DISABLED until you add a key.
    echo     Get a free key from: https://aistudio.google.com/apikey
    echo     Then paste it into: %KEY_FILE%
    echo.
    goto :LAUNCH
)

:: Install dependencies (quiet, once)
where python >nul 2>&1
if errorlevel 1 (
    echo [!] Python not found in PATH. Install Python 3.10+ to enable captcha solver.
    goto :LAUNCH
)
python -c "import google.genai, PIL" >nul 2>&1
if errorlevel 1 (
    echo [*] Installing solver dependencies (first run only)...
    python -m pip install --user --quiet google-genai pillow
)

:: Start solver in background if not already running
tasklist /FI "IMAGENAME eq pythonw.exe" /FO CSV 2>nul | findstr /i "captcha_solver" >nul
if %errorlevel% neq 0 (
    if exist "%SOLVER_PY%" (
        echo [*] Starting captcha solver in background...
        start "" /B pythonw "%SOLVER_PY%"
    ) else (
        echo [!] captcha_solver.py missing, skipping solver launch.
    )
) else (
    echo [*] Captcha solver already running.
)

:: ==========================================
:: PHASE 6: Launch Game
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
    pause
)

:END
echo.
timeout /t 3 /nobreak >nul
exit /b 0
