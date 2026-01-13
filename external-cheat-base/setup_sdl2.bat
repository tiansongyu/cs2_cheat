@echo off
setlocal enabledelayedexpansion

echo ========================================
echo   SDL2 Setup Script for CS2 ESP
echo ========================================
echo.

set SDL2_VERSION=2.30.0
set SDL2_URL=https://github.com/libsdl-org/SDL/releases/download/release-%SDL2_VERSION%/SDL2-devel-%SDL2_VERSION%-VC.zip
set SDL2_ZIP=SDL2-devel-%SDL2_VERSION%-VC.zip
set SDL2_DIR=SDL2-%SDL2_VERSION%

:: Check if SDL2 folder already exists
if exist "SDL2" (
    echo SDL2 folder already exists.
    echo If you want to reinstall, please delete the SDL2 folder first.
    goto :done
)

:: Download SDL2
echo Downloading SDL2 %SDL2_VERSION%...
curl -L -o "%SDL2_ZIP%" "%SDL2_URL%"
if errorlevel 1 (
    echo ERROR: Failed to download SDL2!
    echo Please download manually from:
    echo %SDL2_URL%
    pause
    exit /b 1
)

:: Extract SDL2
echo Extracting SDL2...
powershell -command "Expand-Archive -Path '%SDL2_ZIP%' -DestinationPath '.' -Force"
if errorlevel 1 (
    echo ERROR: Failed to extract SDL2!
    pause
    exit /b 1
)

:: Rename folder
echo Setting up SDL2 folder...
if exist "%SDL2_DIR%" (
    rename "%SDL2_DIR%" SDL2
)

:: Clean up
echo Cleaning up...
del "%SDL2_ZIP%"

:: Copy DLL to output directory
echo.
echo ========================================
echo   SDL2 Setup Complete!
echo ========================================
echo.
echo IMPORTANT: After building, you need to copy SDL2.dll to the output folder.
echo SDL2.dll is located at: SDL2\lib\x64\SDL2.dll
echo.
echo Or add this post-build event to your project:
echo copy "$(SolutionDir)external-cheat-base\SDL2\lib\x64\SDL2.dll" "$(OutDir)"
echo.

:done
pause

