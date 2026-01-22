@echo off
setlocal enabledelayedexpansion

echo ========================================
echo CS2 Offset Files Updater
echo ========================================
echo.

REM Check if curl is available
where curl >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] curl command not found, please ensure Windows 10/11 has curl installed
    pause
    exit /b 1
)

REM Set file paths
set "GENERATED_DIR=%~dp0generated"
set "CLIENT_DLL=%GENERATED_DIR%\client_dll.hpp"
set "OFFSETS=%GENERATED_DIR%\offsets.hpp"
set "BUTTONS=%GENERATED_DIR%\buttons.hpp"
set "LAST_COMMIT=%~dp0..\last_commit.txt"

REM Create generated directory if not exists
if not exist "%GENERATED_DIR%" (
    echo [INFO] Creating generated directory...
    mkdir "%GENERATED_DIR%"
)

echo [Step 1/4] Checking cs2-dumper repository updates...
echo.

REM Try to get latest commit SHA using GitHub API
echo Fetching latest commit info from GitHub API...
powershell -Command "$ProgressPreference = 'SilentlyContinue'; try { $response = Invoke-RestMethod -Uri 'https://api.github.com/repos/a2x/cs2-dumper/commits/main' -Headers @{'User-Agent'='CS2-Offset-Updater'}; Write-Output $response.sha } catch { Write-Output 'ERROR' }" > temp_commit.txt

set /p LATEST_COMMIT=<temp_commit.txt
del temp_commit.txt

if "%LATEST_COMMIT%"=="ERROR" (
    echo [WARNING] GitHub API rate limit exceeded or network error
    echo [INFO] Skipping version check, will download latest files directly
    echo.
    set "SKIP_VERSION_CHECK=true"
    goto :download_files
)

if "%LATEST_COMMIT%"=="" (
    echo [WARNING] Failed to get commit SHA
    echo [INFO] Skipping version check, will download latest files directly
    echo.
    set "SKIP_VERSION_CHECK=true"
    goto :download_files
)

echo Latest commit: %LATEST_COMMIT%

REM Read last commit
set "LAST_COMMIT_SHA=none"
if exist "%LAST_COMMIT%" (
    set /p LAST_COMMIT_SHA=<"%LAST_COMMIT%"
)

echo Last commit: %LAST_COMMIT_SHA%
echo.

if "%LATEST_COMMIT%"=="%LAST_COMMIT_SHA%" (
    echo [INFO] Already up to date, no update needed
    echo.
    choice /C YN /M "Force re-download anyway"
    if errorlevel 2 (
        echo Operation cancelled
        pause
        exit /b 0
    )
    echo.
)

:download_files

echo [Step 2/4] Downloading offset files...
echo.

REM Download client_dll.hpp
echo Downloading client_dll.hpp...
curl -o "%CLIENT_DLL%" https://raw.githubusercontent.com/a2x/cs2-dumper/main/output/client_dll.hpp
if %errorlevel% neq 0 (
    echo [ERROR] Failed to download client_dll.hpp
    pause
    exit /b 1
)
echo [SUCCESS] client_dll.hpp

REM Download offsets.hpp
echo Downloading offsets.hpp...
curl -o "%OFFSETS%" https://raw.githubusercontent.com/a2x/cs2-dumper/main/output/offsets.hpp
if %errorlevel% neq 0 (
    echo [ERROR] Failed to download offsets.hpp
    pause
    exit /b 1
)
echo [SUCCESS] offsets.hpp

REM Download buttons.hpp
echo Downloading buttons.hpp...
curl -o "%BUTTONS%" https://raw.githubusercontent.com/a2x/cs2-dumper/main/output/buttons.hpp
if %errorlevel% neq 0 (
    echo [ERROR] Failed to download buttons.hpp
    pause
    exit /b 1
)
echo [SUCCESS] buttons.hpp
echo.

echo [Step 3/4] Updating commit record...
if not "%SKIP_VERSION_CHECK%"=="true" (
    echo %LATEST_COMMIT% > "%LAST_COMMIT%"
    echo [SUCCESS] Updated last_commit.txt
) else (
    echo [SKIP] Version check was skipped, not updating last_commit.txt
)
echo.

echo [Step 4/4] Git commit (optional)...
echo.
choice /C YN /M "Commit changes to Git repository"
if errorlevel 2 (
    echo [SKIP] Git commit skipped
    goto :finish
)

REM Check if git is available
where git >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNING] git command not found, skipping commit
    goto :finish
)

REM Change to repository root directory
cd /d "%~dp0.."

REM Add files to git
git add external-cheat-base/generated/client_dll.hpp external-cheat-base/generated/offsets.hpp external-cheat-base/generated/buttons.hpp last_commit.txt

REM Commit changes
git commit -m "Update CS2 offset files from cs2-dumper"
if %errorlevel% equ 0 (
    echo [SUCCESS] Changes committed
    echo.
    choice /C YN /M "Push to remote repository"
    if errorlevel 1 (
        if not errorlevel 2 (
            git push
            if %errorlevel% equ 0 (
                echo [SUCCESS] Pushed to remote repository
            ) else (
                echo [WARNING] Push failed, please push manually
            )
        )
    )
) else (
    echo [INFO] No changes to commit or commit failed
)

:finish
echo.
echo ========================================
echo Update completed!
echo ========================================
pause

