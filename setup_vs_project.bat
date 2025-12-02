@echo off
echo ==========================================
echo Petrichor VST - Visual Studio Setup Script
echo ==========================================

REM 1. Check for Node.js
where node >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: Node.js is not installed or not in PATH.
    echo Please install Node.js to build the frontend.
    pause
    exit /b 1
)

REM 2. Build Frontend
echo.
echo [1/4] Building React Frontend...
cd frontend
call npm install
if %errorlevel% neq 0 (
    echo Error: npm install failed.
    cd ..
    pause
    exit /b 1
)

call npm run build
if %errorlevel% neq 0 (
    echo Error: npm run build failed.
    cd ..
    pause
    exit /b 1
)
cd ..

REM 3. Embed Frontend
echo.
echo [2/4] Embedding Frontend Assets...
powershell -ExecutionPolicy Bypass -File scripts/embed_frontend.ps1
if %errorlevel% neq 0 (
    echo Error: Failed to embed frontend assets.
    echo Ensure PowerShell is enabled.
    pause
    exit /b 1
)

REM 4. Download WebView2 SDK
echo.
echo [3/4] Checking for WebView2 SDK...
powershell -ExecutionPolicy Bypass -File scripts/fetch_webview2.ps1
if %errorlevel% neq 0 (
    echo Error: Failed to download WebView2 SDK.
    pause
    exit /b 1
)

REM 5. Check for CMake
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: CMake is not installed or not in PATH.
    echo Please install CMake to generate the Visual Studio project.
    pause
    exit /b 1
)

REM 6. Generate Visual Studio Solution
echo.
echo [4/4] Generating Visual Studio Solution...
echo (Letting CMake auto-detect the installed Visual Studio version...)

REM Clean previous build to prevent stale paths/cache
if exist Build (
    echo Cleaning previous Build directory...
    rmdir /s /q Build
)
mkdir Build
cd Build

REM Get absolute path to packages folder for CMake
set PACKAGES_DIR=%~dp0packages\Microsoft.Web.WebView2
REM Replace backslashes with forward slashes for CMake
set PACKAGES_DIR=%PACKAGES_DIR:\=/%

cmake .. -DJUCE_WEBVIEW2_PACKAGE_LOCATION="%PACKAGES_DIR%"

if %errorlevel% neq 0 (
    echo.
    echo ================================================================
    echo ERROR: CMake generation failed.
    echo.
    echo Possible causes:
    echo 1. Visual Studio is not installed.
    echo 2. The "Desktop development with C++" workload is missing.
    echo.
    echo SOLUTION:
    echo Open "Visual Studio Installer", click "Modify" on your installation,
    echo and ensure "Desktop development with C++" is checked.
    echo ================================================================
    cd ..
    pause
    exit /b 1
)
cd ..

echo.
echo ==========================================
echo Success!
echo Open the generated '.sln' file in the 'Build' directory.
echo ==========================================
pause
