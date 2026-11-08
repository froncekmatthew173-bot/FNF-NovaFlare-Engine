@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

color 0a

:: 获取当前bat文件所在目录的上一级目录
set "PARENT_DIR=%~dp0.."
cd /d "%PARENT_DIR%"

echo ========================================
echo Step 1: Installing Haxe 4.3.7
echo ========================================

haxe -version >nul 2>&1
if %errorlevel% equ 0 (
    echo Haxe is already installed, skipping download.
) else (
    echo Downloading Haxe 4.3.7 installer...
    curl -L -# -O https://github.com/HaxeFoundation/haxe/releases/download/4.3.7/haxe-4.3.7-win64.exe
    
    echo Please complete the Haxe installation manually...
    echo IMPORTANT: Remember to check "Add Haxe to PATH" during installation!
    start /wait haxe-4.3.7-win64.exe
    
    echo.
    echo Press any key after completing Haxe installation...
    pause >nul
)

echo Refreshing environment variables...
for /f "tokens=2*" %%a in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v PATH 2^>nul') do set "SYSTEM_PATH=%%b"
for /f "tokens=2*" %%a in ('reg query "HKCU\Environment" /v PATH 2^>nul') do set "USER_PATH=%%b"
set "PATH=%SYSTEM_PATH%;%USER_PATH%;%PATH%"

echo Verifying Haxe installation...
haxe -version
if %errorlevel% neq 0 (
    echo ERROR: Haxe not found in PATH. Please ensure it's installed correctly.
    pause
    exit /b 1
)

haxelib version
if %errorlevel% neq 0 (
    echo ERROR: Haxelib not found. Please ensure Haxe is installed correctly.
    pause
    exit /b 1
)

echo.
echo ========================================
echo Step 2: Setting up haxelib and installing hmm
echo ========================================

for /f "tokens=*" %%i in ('where haxe') do set "HAXE_PATH=%%i"
set "HAXE_DIR=!HAXE_PATH:\haxe.exe=!"

echo Setting haxelib path to: !HAXE_DIR!\lib
haxelib setup "!HAXE_DIR!\lib"

echo Installing hmm...
haxelib git hmm https://github.com/NovaFlare-Engine-haxelib/hmm-3.1.0 70aca779a9d374a6e6b83c18715a3991a147ca37 --skip-dependencies

echo.
echo ========================================
echo Step 3: Running hmm install
echo ========================================
haxelib run hmm install

echo.
echo ========================================
echo Step 4: Installing Visual Studio Community (Dependency)
echo ========================================

echo Downloading Visual Studio Community installer...
curl -# -O https://download.visualstudio.microsoft.com/download/pr/3105fcfe-e771-41d6-9a1c-fc971e7d03a7/8eb13958dc429a6e6f7e0d6704d43a55f18d02a253608351b6bf6723ffdaf24e/vs_Community.exe

echo Installing Visual Studio components (this may take a while)...
vs_Community.exe --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows10SDK.19041 -p --wait --quiet

del vs_Community.exe
echo Visual Studio installation finished.

echo.
echo ========================================
echo Step 5: Running lime setup
echo ========================================
haxelib run lime setup

echo.
echo ========================================
echo Step 6: Running lime test windows
echo ========================================
haxelib run lime test windows

echo.
echo ========================================
echo All steps completed!
echo ========================================
pause