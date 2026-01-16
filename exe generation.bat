REM ================================================================
REM COMPLETE EXE GENERATION FROM CPP SOURCE (3 STEPS)
REM ================================================================

REM STEP 1: Set up the Visual Studio build environment
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64

REM STEP 2: Compile resources (lanscr.rc → LANSCR.res)
REM This generates the icon, version info, and manifest
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe" /nologo /fo LANSCR.res lanscr.rc

REM STEP 3: Compile C++ source and link with resources (lanscr.cpp + LANSCR.res → LANSCR.exe)
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe" /nologo /EHsc /std:c++17 /O2 /MT /DUNICODE /D_UNICODE lanscr.cpp LANSCR.res /Fe:LANSCR.exe /link /SUBSYSTEM:CONSOLE ws2_32.lib winhttp.lib ole32.lib oleaut32.lib windowscodecs.lib shlwapi.lib user32.lib gdi32.lib shell32.lib advapi32.lib mmdevapi.lib winmm.lib uuid.lib

REM ================================================================
REM BUILD ARTIFACTS GENERATED:
REM ================================================================
REM lanscr.obj          - Compiled C++ object file
REM LANSCR.res          - Compiled resource file (icon + version + manifest)
REM LANSCR.exe          - Final executable (end-user file)
REM ================================================================

REM OPTIONAL: Clean up build artifacts (keep only LANSCR.exe)
REM del lanscr.obj LANSCR.res