@echo off
setlocal EnableExtensions
set "DesktopArch=%~1"
if not defined DesktopArch set "DesktopArch=x64"

where msbuild.exe >nul 2>&1
if errorlevel 1 (
    for /f "usebackq tokens=*" %%V in (`
        "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    `) do call "%%V\Common7\Tools\VsDevCmd.bat"
)
where msbuild.exe >nul 2>&1
if errorlevel 1 (
    echo MSBuild was not found. Install the Visual Studio C++ desktop build tools.
    exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup\Build-Desktop.ps1" -Architecture "%DesktopArch%"
exit /b %errorlevel%
