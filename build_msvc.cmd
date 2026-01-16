@echo off
setlocal EnableExtensions

REM Builds LANSCR.exe with icon/version/manifest from lanscr.rc
REM Requires: Visual Studio Build Tools + Windows SDK

set "ROOT=%~dp0"
pushd "%ROOT%" >nul

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] vswhere.exe not found. Install Visual Studio Build Tools.
  popd >nul
  exit /b 1
)

set "VSINSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if "%VSINSTALL%"=="" (
  echo [ERROR] No Visual Studio installation with C++ tools found.
  popd >nul
  exit /b 1
)

set "VSDEVCMD=%VSINSTALL%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
  echo [ERROR] VsDevCmd.bat not found: %VSDEVCMD%
  popd >nul
  exit /b 1
)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul

echo.
echo [1/2] Resources (icon + version + manifest)
set "RCX64=rc"
if defined WindowsSdkDir if defined WindowsSDKVersion set "RCX64=%WindowsSdkDir%bin\%WindowsSDKVersion%x64\rc.exe"
if defined RCX64 if not exist "%RCX64%" set "RCX64=rc"

"%RCX64%" /nologo /fo "LANSCR.res" "lanscr.rc"
if errorlevel 1 (
  echo [ERROR] rc failed.
  popd >nul
  exit /b 1
)

echo.
echo [2/2] EXE
cl /nologo /EHsc /std:c++17 /O2 /MT /DUNICODE /D_UNICODE ^
  "lanscr.cpp" "LANSCR.res" /Fe:"LANSCR.exe" ^
  /link /SUBSYSTEM:CONSOLE ^
  ws2_32.lib winhttp.lib ole32.lib oleaut32.lib windowscodecs.lib shlwapi.lib ^
  user32.lib gdi32.lib shell32.lib advapi32.lib mmdevapi.lib winmm.lib uuid.lib
if errorlevel 1 (
  echo [ERROR] cl/link failed.
  popd >nul
  exit /b 1
)

echo.
echo OK: %CD%\LANSCR.exe
popd >nul
exit /b 0
