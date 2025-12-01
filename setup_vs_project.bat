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
echo [1/3] Building React Frontend...
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

REM 3. Check for CMake
echo.
echo [2/3] Checking for CMake...
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: CMake is not installed or not in PATH.
    echo Please install CMake to generate the Visual Studio project.
    pause
    exit /b 1
)

REM 4. Generate Visual Studio Solution
echo.
echo [3/3] Generating Visual Studio Solution...
echo (Letting CMake auto-detect the installed Visual Studio version...)

if not exist Build mkdir Build
cd Build
cmake ..
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
