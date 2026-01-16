@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ==============================================================
REM LANSCR Setup.bat (single-file automation)
REM - Runs existing LANSCR.exe if present
REM - Optionally downloads a prebuilt EXE (configure URL below)
REM - Or builds from source, installing toolchain automatically:
REM   - Visual Studio 2022 Build Tools (MSVC + Windows SDK)
REM ==============================================================

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR_NOSLASH=%SCRIPT_DIR%"
if "%SCRIPT_DIR_NOSLASH:~-1%"=="\" set "SCRIPT_DIR_NOSLASH=%SCRIPT_DIR_NOSLASH:~0,-1%"
pushd "%SCRIPT_DIR%" >nul 2>nul

REM --------------------------------------------------------------
REM CONFIG (you said you'll provide later)
REM --------------------------------------------------------------
REM Direct download link to a prebuilt EXE (best for non-tech users)
REM Example: https://github.com/pidugulikhil/LANSCR/releases/download/v1.0/LANSCR.exe
if not defined LANSCR_PREBUILT_EXE_URL set "LANSCR_PREBUILT_EXE_URL=https://github.com/pidugulikhil/LANSCR/releases/download/1.0/LANSCR.exe"

REM Optional: releases page URL (fallback)
REM Example: https://github.com/pidugulikhil/LANSCR/releases/latest
if not defined LANSCR_RELEASE_URL set "LANSCR_RELEASE_URL=https://github.com/pidugulikhil/LANSCR/releases/latest"

REM Visual Studio Build Tools bootstrapper (official)
if not defined LANSCR_VS_BUILD_TOOLS_URL set "LANSCR_VS_BUILD_TOOLS_URL=https://aka.ms/vs/17/release/vs_BuildTools.exe"

REM --------------------------------------------------------------
REM TEST MODE (non-interactive)
REM   Setup.bat --test
REM - Skips all GUI prompts
REM - Attempts a local build using whatever toolchain is already installed
REM --------------------------------------------------------------
if /I "%~1"=="--test" goto :TEST_MODE
if /I "%~1"=="/test" goto :TEST_MODE
if /I "%~1"=="--test-verbose" goto :TEST_MODE_VERBOSE

REM --------------------------------------------------------------
REM 0) If EXE exists, offer to run it.
REM --------------------------------------------------------------
if exist "%SCRIPT_DIR%LANSCR.exe" (
	call :ASK_RUN_OR_REBUILD_OR_EXIT "LANSCR.exe is already in this folder.\n\nYES    = Run it now\nNO     = Rebuild from source (refresh icon/version)\nCANCEL = Exit" "LANSCR" HAVE_EXE_ACTION
	if /I "!HAVE_EXE_ACTION!"=="RUN" goto :POST_HAVE_EXE
	if /I "!HAVE_EXE_ACTION!"=="REBUILD" goto :DO_BUILD_FLOW
	goto :END_OK
)

REM --------------------------------------------------------------
REM 1) No EXE found: download prebuilt OR build from source.
REM --------------------------------------------------------------
call :ASK_YESNO "LANSCR.exe was not found.\n\nDo you want me to DOWNLOAD the prebuilt EXE (recommended)?\n\nNOTE: Downloaded EXE will use the icon/metadata already embedded in that release.\nChoose NO to BUILD from source here (uses your local lanscr.cpp + lanscr.rc + lanscr.manifest + lanscr.ico)." "LANSCR Setup" CHOOSE_DOWNLOAD

if /I "!CHOOSE_DOWNLOAD!"=="YES" (
	call :DOWNLOAD_PREBUILT_EXE
	if errorlevel 1 goto :END_FAIL
	goto :POST_HAVE_EXE
)

REM --------------------------------------------------------------
REM 2) Build from source (MSVC).
REM --------------------------------------------------------------
:DO_BUILD_FLOW
REM If we're rebuilding, clear old build artifacts so resources are refreshed.
if exist "%SCRIPT_DIR%LANSCR.exe" del /f /q "%SCRIPT_DIR%LANSCR.exe" >nul 2>nul
if exist "%SCRIPT_DIR%LANSCR.exe" (
	call :POPUP "I could not overwrite LANSCR.exe.\n\nPlease close any running LANSCR.exe (or unpin/close any process using it), then run Setup.bat again." "LANSCR Setup"
	goto :END_FAIL
)
if exist "%SCRIPT_DIR%LANSCR.res" del /f /q "%SCRIPT_DIR%LANSCR.res" >nul 2>nul
if exist "%SCRIPT_DIR%LANSCR_res.o" del /f /q "%SCRIPT_DIR%LANSCR_res.o" >nul 2>nul

call :VERIFY_SOURCE_FILES
if errorlevel 1 goto :END_FAIL

call :TRY_BUILD_WITH_WHATEVER
if %ERRORLEVEL%==0 goto :POST_HAVE_EXE

REM --------------------------------------------------------------
REM 3) No compiler/toolchain detected: auto-install (no extra prompts).
REM --------------------------------------------------------------
call :AUTO_INSTALL_TOOLCHAIN_AND_BUILD
if errorlevel 1 (
	call :SHOW_INSTALL_STEPS_MENU
	goto :END_FAIL
)
goto :POST_HAVE_EXE

:AUTO_INSTALL_TOOLCHAIN_AND_BUILD
echo.
echo No C++ toolchain detected.
echo Auto-installing Visual Studio Build Tools (C++) and building...

echo.
echo [AUTO] Installing Visual Studio Build Tools (MSVC + Windows SDK)...
call :INSTALL_VS_BUILD_TOOLS
if errorlevel 1 exit /b 1

call :TRY_BUILD_WITH_WHATEVER
if errorlevel 1 exit /b 1
exit /b 0

:TEST_MODE_VERBOSE
set "LANSCR_TEST_MODE=1"
set "LANSCR_TEST_VERBOSE=1"
@echo on
goto :TEST_MODE_COMMON

:TEST_MODE
set "LANSCR_TEST_MODE=1"
set "LANSCR_TEST_VERBOSE="

:TEST_MODE_COMMON
echo.
echo ============================================================== 
echo LANSCR Setup.bat --test
echo - Non-interactive build verification
echo ============================================================== 
echo.

call :TRY_BUILD_WITH_WHATEVER
if errorlevel 1 goto :TEST_FAIL
if not exist "%SCRIPT_DIR%LANSCR.exe" goto :TEST_MISSING

echo.
echo [TEST] OK: "%SCRIPT_DIR%LANSCR.exe"
goto :END_OK

:TEST_FAIL
echo.
echo [TEST] Build failed (no toolchain detected or build error).
goto :END_FAIL

:TEST_MISSING
echo.
echo [TEST] Build reported success but LANSCR.exe is missing.
goto :END_FAIL

:POST_HAVE_EXE
call :ASK_YESNO "Add LANSCR shortcut to Desktop?" "LANSCR" DESKTOP_CHOICE
if /I "!DESKTOP_CHOICE!"=="YES" (
	call :CREATE_DESKTOP_SHORTCUT
	call :CREATE_PORTABLE_SHORTCUT_HERE
)

call :ASK_YESNO "Ready. Launch LANSCR now?" "LANSCR" RUN_CHOICE
if /I "!RUN_CHOICE!"=="YES" start "LANSCR" "%SCRIPT_DIR%LANSCR.exe"

goto :END_OK

:POST_BUILD
REM Backward-compatible label (older flows)
goto :POST_HAVE_EXE

REM ==============================================================
REM Core build logic
REM ==============================================================
:TRY_BUILD_WITH_WHATEVER
REM Returns 0 on success, 1 on failure

call :VERIFY_SOURCE_FILES
if errorlevel 1 (exit /b 1)

REM Prefer MSVC if available
where cl.exe >nul 2>nul
if not errorlevel 1 (
	call :BUILD_MSVC
	if not errorlevel 1 goto :eof
)

REM Try to load MSVC env if installed
:TBW_TRY_LOAD_MSVC_ENV
call :TRY_LOAD_MSVC_ENV
if errorlevel 1 (cmd /c exit /b 1 & goto :eof)
call :BUILD_MSVC
if not errorlevel 1 goto :eof
goto :eof

:BUILD_MSVC
echo.
echo Building with Visual Studio (cl.exe)...

pushd "%SCRIPT_DIR%" >nul 2>nul

REM If the environment isn't initialized, rc/cl won't find Windows SDK headers.
if not defined INCLUDE call :TRY_LOAD_MSVC_ENV
if defined LANSCR_TEST_VERBOSE @echo on

echo.
echo [1/2] Building resources...
call :FIND_RC_EXE_X64
"%RC_EXE_X64%" /nologo /fo "%SCRIPT_DIR%LANSCR.res" "%SCRIPT_DIR%lanscr.rc"
if errorlevel 1 (
	echo [ERROR] Resource build failed.
	popd >nul 2>nul
	exit /b 1
)

echo.
echo [2/2] Building EXE...
cl /nologo /EHsc /std:c++17 /O2 /MT /DUNICODE /D_UNICODE ^
  "%SCRIPT_DIR%lanscr.cpp" "%SCRIPT_DIR%LANSCR.res" /Fe:"%SCRIPT_DIR%LANSCR.exe" ^
  /link /SUBSYSTEM:CONSOLE ^
  ws2_32.lib winhttp.lib ole32.lib oleaut32.lib windowscodecs.lib shlwapi.lib ^
  user32.lib gdi32.lib shell32.lib advapi32.lib mmdevapi.lib winmm.lib uuid.lib
if errorlevel 1 (
	echo [ERROR] Build failed.
	popd >nul 2>nul
	exit /b 1
)

popd >nul 2>nul

echo.
echo OK: "%SCRIPT_DIR%LANSCR.exe"
exit /b 0

:FIND_RC_EXE_X64
REM Locates an x64 Windows SDK rc.exe.
REM Prefer the Windows SDK variables set by VsDevCmd (reliable and avoids arm64).
set "RC_EXE_X64="

if defined WindowsSdkDir if defined WindowsSDKVersion set "RC_EXE_X64=%WindowsSdkDir%bin\%WindowsSDKVersion%x64\rc.exe"

if defined RC_EXE_X64 if exist "%RC_EXE_X64%" exit /b 0

REM Fallback: try plain rc.exe (whatever is on PATH)
set "RC_EXE_X64=rc"
exit /b 0

:TRY_LOAD_MSVC_ENV
REM Attempts to load VS build environment into this shell.
REM Returns 0 if loaded, 1 otherwise.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1

set "VSINSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if "%VSINSTALL%"=="" exit /b 1

set "VSDEVCMD=%VSINSTALL%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" exit /b 1

call "%VSDEVCMD%" -arch=x64 >nul
if defined LANSCR_TEST_VERBOSE @echo on

where cl.exe >nul 2>nul
if not %ERRORLEVEL%==0 exit /b 1
exit /b 0

REM ==============================================================
REM Download prebuilt EXE
REM ==============================================================
:DOWNLOAD_PREBUILT_EXE
if not defined LANSCR_PREBUILT_EXE_URL goto :DL_MISSING
if "%LANSCR_PREBUILT_EXE_URL%"=="" goto :DL_MISSING

echo.
echo Downloading prebuilt LANSCR.exe...
set "DL_OK=0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $u='%LANSCR_PREBUILT_EXE_URL%'; $out=Join-Path '%SCRIPT_DIR%' 'LANSCR.exe'; Invoke-WebRequest -UseBasicParsing -Uri $u -OutFile $out; exit 0 } catch { exit 1 }" 
if not errorlevel 1 set "DL_OK=1"
if "%DL_OK%"=="0" (
	where curl.exe >nul 2>nul
	if %ERRORLEVEL%==0 (
		echo PowerShell download failed, trying curl...
		curl.exe -L --fail -o "%SCRIPT_DIR%LANSCR.exe" "%LANSCR_PREBUILT_EXE_URL%" >nul 2>nul
		if not errorlevel 1 set "DL_OK=1"
	)
)
if "%DL_OK%"=="0" (
	call :POPUP "Download failed.\n\nPlease check your internet connection.\n\nIf you are behind a school/company PC policy, ask the admin to allow GitHub downloads, or download LANSCR.exe from the Releases page." "LANSCR Setup"
	exit /b 1
)

if not exist "%SCRIPT_DIR%LANSCR.exe" (
	call :POPUP "Download completed but LANSCR.exe was not found.\n\nCheck the download URL." "LANSCR Setup"
	exit /b 1
)

exit /b 0

:DL_MISSING
call :POPUP "Prebuilt EXE download link is not configured yet.\n\nAsk the developer to set LANSCR_PREBUILT_EXE_URL inside Setup.bat.\n\nYou can also open the GitHub Releases page and download LANSCR.exe manually." "LANSCR Setup"
call :ASK_YESNO "Open the GitHub Releases page now?" "LANSCR" OPEN_RELEASES
if /I "%OPEN_RELEASES%"=="YES" call :OPEN_RELEASES_PAGE
exit /b 1

:INSTALL_VS_BUILD_TOOLS
echo.
echo Installing Visual Studio 2022 Build Tools (C++ workload)...
echo This is a large download and can take a while.

call :ENSURE_ADMIN
if errorlevel 1 exit /b 1

set "VSBT_LAST_ERROR="

where winget.exe >nul 2>nul
if "%ERRORLEVEL%"=="0" (
	winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-package-agreements --accept-source-agreements --override "--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --includeRecommended"
	if errorlevel 1 (
		set "VSBT_LAST_ERROR=%ERRORLEVEL%"
		goto :VSBT_INSTALL_FAIL
	)
	call :WAIT_FOR_MSVC_ENV
	if not errorlevel 1 exit /b 0
	REM Fall through to bootstrapper if env still not detectable.
)

REM winget may be missing on some Windows installs; fall back to the official bootstrapper.
set "TMP_VSBT=%TEMP%\vs_BuildTools.exe"
echo.
echo Downloading Visual Studio Build Tools bootstrapper...
set "DL_OK=0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $u='%LANSCR_VS_BUILD_TOOLS_URL%'; $out='%TMP_VSBT%'; Invoke-WebRequest -UseBasicParsing -Uri $u -OutFile $out; exit 0 } catch { exit 1 }" 
if not errorlevel 1 set "DL_OK=1"
if "%DL_OK%"=="0" (
	where curl.exe >nul 2>nul
	if %ERRORLEVEL%==0 (
		echo PowerShell download failed, trying curl...
		curl.exe -L --fail -o "%TMP_VSBT%" "%LANSCR_VS_BUILD_TOOLS_URL%" >nul 2>nul
		if not errorlevel 1 set "DL_OK=1"
	)
)
if "%DL_OK%"=="0" goto :VSBT_INSTALL_FAIL
if not exist "%TMP_VSBT%" goto :VSBT_INSTALL_FAIL

echo.
echo Running Visual Studio Build Tools installer (silent)...
start /wait "VS Build Tools" "%TMP_VSBT%" --quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --includeRecommended
if errorlevel 1 (
	set "VSBT_LAST_ERROR=%ERRORLEVEL%"
	goto :VSBT_INSTALL_FAIL
)

call :WAIT_FOR_MSVC_ENV
if errorlevel 1 goto :VSBT_INSTALL_FAIL
exit /b 0

:VSBT_INSTALL_FAIL
if not defined VSBT_LAST_ERROR set "VSBT_LAST_ERROR=%ERRORLEVEL%"
call :POPUP "Visual Studio Build Tools installation failed.\n\nExit code: %VSBT_LAST_ERROR%\n\nCommon reasons:\n- You clicked NO on the UAC/admin prompt\n- Company/school policy blocks Microsoft downloads\n- Another Visual Studio Installer is already running\n\nLogs are usually in: %TEMP%\n(files like dd_* or vs_*).\n\nTry running Setup.bat again." "LANSCR Setup"
exit /b 1

:WAIT_FOR_MSVC_ENV
REM After install, VS components may take a moment to register.
for /L %%I in (1,1,12) do (
	call :TRY_LOAD_MSVC_ENV
	if not errorlevel 1 exit /b 0
	call :SLEEP_5
)
exit /b 1

:SLEEP_5
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Sleep -Seconds 5" >nul 2>nul
exit /b 0

:ENSURE_ADMIN
REM Returns 0 if admin. If not admin, triggers UAC relaunch and returns 1.
net session >nul 2>nul
if "%ERRORLEVEL%"=="0" exit /b 0

call :POPUP "Admin permission is required to install Visual Studio Build Tools.\n\nA Windows UAC prompt will appear next.\nClick YES to continue automatic install." "LANSCR Setup"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -Verb RunAs -FilePath 'cmd.exe' -ArgumentList '/c', ('""%~f0"" %*')" >nul 2>nul
exit /b 1

:VERIFY_SOURCE_FILES
REM Ensures required local build inputs exist. Returns 0 if OK, 1 if missing.
set "MISSING_LIST="
set "MISSING_ANY=0"

if not exist "%SCRIPT_DIR%lanscr.cpp" (
	set "MISSING_ANY=1"
	set "MISSING_LIST=!MISSING_LIST!  - lanscr.cpp\n"
)
if not exist "%SCRIPT_DIR%lanscr.rc" (
	set "MISSING_ANY=1"
	set "MISSING_LIST=!MISSING_LIST!  - lanscr.rc\n"
)
if not exist "%SCRIPT_DIR%lanscr.manifest" (
	set "MISSING_ANY=1"
	set "MISSING_LIST=!MISSING_LIST!  - lanscr.manifest\n"
)
if not exist "%SCRIPT_DIR%lanscr.ico" (
	set "MISSING_ANY=1"
	set "MISSING_LIST=!MISSING_LIST!  - lanscr.ico\n"
)

if "%MISSING_ANY%"=="1" (
	if defined LANSCR_TEST_MODE (
		echo.
		echo [ERROR] Missing required build files:
		if not exist "%SCRIPT_DIR%lanscr.cpp" echo   - lanscr.cpp
		if not exist "%SCRIPT_DIR%lanscr.rc" echo   - lanscr.rc
		if not exist "%SCRIPT_DIR%lanscr.manifest" echo   - lanscr.manifest
		if not exist "%SCRIPT_DIR%lanscr.ico" echo   - lanscr.ico
	) else (
		call :POPUP "Cannot BUILD because required files are missing:\n\n%MISSING_LIST%\nPut these files next to Setup.bat, then try again." "LANSCR Setup"
	)
	exit /b 1
)

exit /b 0

REM ==============================================================
REM Helpers
REM ==============================================================
:POPUP
REM Args: %1 message, %2 title
set "MSG=%~1"
set "TTL=%~2"
REM Prefer WinForms MessageBox (works on more Windows installs). Fall back to console echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; try { $m=[Regex]::Unescape('%MSG%'); Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.MessageBox]::Show($m,'%TTL%',[System.Windows.Forms.MessageBoxButtons]::OK,[System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null; exit 0 } catch { exit 1 }" >nul 2>nul
if not errorlevel 1 exit /b 0
echo.
echo ==============================================================
echo %TTL%
REM Render \n sequences as real newlines in console.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$m=[Regex]::Unescape('%MSG%'); $m=$m -replace '\\n',""`n""; Write-Host $m" 2>nul
if errorlevel 1 echo %MSG%
echo ==============================================================
exit /b 0

:ASK_YESNO
REM Args: %1 message, %2 title, %3 outVar (YES/NO)
set "QMSG=%~1"
set "QTTL=%~2"
set "OUTVAR=%~3"
REM Try GUI prompt first; if it fails, fall back to console CHOICE.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; try { $m=[Regex]::Unescape('%QMSG%'); Add-Type -AssemblyName System.Windows.Forms; $r=[System.Windows.Forms.MessageBox]::Show($m,'%QTTL%',[System.Windows.Forms.MessageBoxButtons]::YesNo,[System.Windows.Forms.MessageBoxIcon]::Question); if($r -eq [System.Windows.Forms.DialogResult]::Yes){ exit 0 } else { exit 1 } } catch { exit 3 }" >nul 2>nul
if %ERRORLEVEL%==3 goto :ASK_YESNO_CONSOLE
if errorlevel 1 set "%OUTVAR%=NO"
if not errorlevel 1 set "%OUTVAR%=YES"
exit /b 0

:ASK_YESNO_CONSOLE
echo.
echo %QTTL%
powershell -NoProfile -ExecutionPolicy Bypass -Command "$m=[Regex]::Unescape('%QMSG%'); $m=$m -replace '\\n',""`n""; Write-Host $m" 2>nul
if errorlevel 1 echo %QMSG%
choice /C YN /N /M "Press Y for YES, N for NO: "
if errorlevel 2 set "%OUTVAR%=NO"
if errorlevel 1 set "%OUTVAR%=YES"
exit /b 0

:CREATE_DESKTOP_SHORTCUT
powershell -NoProfile -ExecutionPolicy Bypass -Command "$desktop=[Environment]::GetFolderPath('Desktop'); $lnk=Join-Path $desktop 'LANSCR.lnk'; $ws=New-Object -ComObject WScript.Shell; $s=$ws.CreateShortcut($lnk); $exe=Join-Path '%SCRIPT_DIR%' 'LANSCR.exe'; $ico=Join-Path '%SCRIPT_DIR%' 'lanscr.ico'; $s.TargetPath=$exe; $s.WorkingDirectory='%SCRIPT_DIR%'; if(Test-Path $ico){ $s.IconLocation=$ico + ',0' } else { $s.IconLocation=$exe + ',0' }; $s.WindowStyle=1; $s.Description='LANSCR - LAN Screen Share (MJPEG)'; $s.Save()" >nul 2>nul
echo.
echo Created Desktop shortcut: LANSCR.lnk
exit /b 0

:CREATE_PORTABLE_SHORTCUT_HERE
REM Creates a shortcut next to the EXE that remains valid if the .lnk is copied elsewhere.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$lnk=Join-Path '%SCRIPT_DIR%' 'LANSCR (Shortcut).lnk'; $ws=New-Object -ComObject WScript.Shell; $s=$ws.CreateShortcut($lnk); $exe=Join-Path '%SCRIPT_DIR%' 'LANSCR.exe'; $ico=Join-Path '%SCRIPT_DIR%' 'lanscr.ico'; $s.TargetPath=$exe; $s.WorkingDirectory='%SCRIPT_DIR%'; if(Test-Path $ico){ $s.IconLocation=$ico + ',0' } else { $s.IconLocation=$exe + ',0' }; $s.WindowStyle=1; $s.Description='LANSCR - LAN Screen Share (MJPEG)'; $s.Save()" >nul 2>nul
exit /b 0

:ASK_RUN_OR_REBUILD_OR_EXIT
REM Args: %1 message, %2 title, %3 outVar (RUN/REBUILD/EXIT)
set "QMSG=%~1"
set "QTTL=%~2"
set "OUTVAR=%~3"

REM Try GUI prompt first; if it fails, fall back to console CHOICE.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; try { $m=[Regex]::Unescape('%QMSG%'); Add-Type -AssemblyName System.Windows.Forms; $r=[System.Windows.Forms.MessageBox]::Show($m,'%QTTL%',[System.Windows.Forms.MessageBoxButtons]::YesNoCancel,[System.Windows.Forms.MessageBoxIcon]::Question); if($r -eq [System.Windows.Forms.DialogResult]::Yes){ exit 0 } elseif($r -eq [System.Windows.Forms.DialogResult]::No){ exit 2 } else { exit 1 } } catch { exit 3 }" >nul 2>nul
if %ERRORLEVEL%==3 goto :ASK_RUN_OR_REBUILD_OR_EXIT_CONSOLE
if %ERRORLEVEL%==0 (set "%OUTVAR%=RUN" & exit /b 0)
if %ERRORLEVEL%==2 (set "%OUTVAR%=REBUILD" & exit /b 0)
set "%OUTVAR%=EXIT"
exit /b 0

:ASK_RUN_OR_REBUILD_OR_EXIT_CONSOLE
echo.
echo %QTTL%
powershell -NoProfile -ExecutionPolicy Bypass -Command "$m=[Regex]::Unescape('%QMSG%'); $m=$m -replace '\\n',""`n""; Write-Host $m" 2>nul
if errorlevel 1 echo %QMSG%
echo.
echo   R = Run
echo   B = Rebuild
echo   E = Exit
choice /C RBE /N /M "Press R/B/E: "
if errorlevel 3 (set "%OUTVAR%=EXIT" & exit /b 0)
if errorlevel 2 (set "%OUTVAR%=REBUILD" & exit /b 0)
set "%OUTVAR%=RUN"
exit /b 0

:OPEN_RELEASES_PAGE
if not defined LANSCR_RELEASE_URL goto :OPEN_RELEASES_MISSING
if "%LANSCR_RELEASE_URL%"=="" goto :OPEN_RELEASES_MISSING
start "" "%LANSCR_RELEASE_URL%" >nul 2>nul
exit /b 0

:OPEN_RELEASES_MISSING
call :POPUP "Releases page link is not configured yet.\n\nAsk the developer to set LANSCR_RELEASE_URL inside Setup.bat." "LANSCR Setup"
exit /b 0

:SHOW_INSTALL_STEPS_MENU
call :ASK_YESNO "Show DETAILED installation steps?\n\nChoose NO for a short/simple checklist." "LANSCR Setup" STEPS_DETAILED
if /I "%STEPS_DETAILED%"=="YES" (
	call :SHOW_INSTALL_STEPS_DETAILED
) else (
	call :SHOW_INSTALL_STEPS_SHORT
)
exit /b 0

:SHOW_INSTALL_STEPS_SHORT
call :POPUP "Simple setup:\n\n1) Install Visual Studio 2022 Build Tools\n2) Select: Desktop development with C++\n3) Ensure a Windows SDK is selected\n4) Run Setup.bat again" "LANSCR Setup"
exit /b 0

:SHOW_INSTALL_STEPS_DETAILED
call :POPUP "DETAILED setup\n\n1) Install 'Visual Studio 2022 Build Tools'\n2) Select: 'Desktop development with C++'\n3) Ensure a Windows SDK is selected\n4) Run Setup.bat again" "LANSCR Setup"
exit /b 0

REM ==============================================================
REM End
REM ==============================================================
:END_OK
popd >nul 2>nul
echo.
if defined LANSCR_TEST_MODE exit /b 0
pause
exit /b 0

:END_FAIL
popd >nul 2>nul
echo.
echo [ERROR] Setup failed.
if defined LANSCR_TEST_MODE exit /b 1
pause
exit /b 1
