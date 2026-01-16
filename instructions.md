**LANSCR folder – GitHub + Build/Run guide (Windows)**

=================================================



**NOTE**: For GitHub-friendly viewing (without cloning), see README.md in this folder.



**What’s inside this folder right now**

----------------------------------

\- LANSCR.exe          : Ready-to-run build (already compiled)

\- lanscr.cpp          : C++ source used to build LANSCR.exe

\- lanscr.rc           : Resource script (version info + icon + embeds manifest)

\- lanscr.manifest     : Windows manifest (embedded into exe via rc)

\- lanscr.ico          : App icon

\- LANSCR.res          : Compiled resources output (generated file)

\- setup.bat           : Builds LANSCR.exe (rebuilds .res too)





**GitHub repo structure:**

\- Commit (source inputs):

&nbsp; - lanscr.cpp

&nbsp; - lanscr.rc

&nbsp; - lanscr.manifest

&nbsp; - lanscr.ico

&nbsp; - setup.bat

&nbsp; - instructions.md



**- Optional (binary):**

&nbsp; - LANSCR.exe

&nbsp; LANSCR.exe is already uploaded to this **GitHub Repository Releases** or can be downloaded from **info.likhil.42web.io/lanscr**



**- Usually res file (generated build output):**

&nbsp; - LANSCR.res

&nbsp; Because it is generated from lanscr.rc, when this file compiled by rc.exe 





What software do I need to build?

--------------------------------

You need Microsoft’s compiler + Windows SDK.



Option A (recommended): Visual Studio 2022 Community

\- Install workload: "Desktop development with C++"

\- Ensure Windows 10/11 SDK is installed



Option B: Visual Studio Build Tools 2022

\- Install: C++ build tools + Windows SDK



What is cl / rc?

\- rc.exe = Resource Compiler (turns .rc into .res)

\- cl.exe = Microsoft C/C++ compiler (turns .cpp into .exe)





How to build (generating EXE with manifest/icon/version)

-------------------------------------------------------

Method 1 (easiest): run run.bat (checks toolchain + builds)

\- Double-click run.bat

&nbsp; OR

\- Open CMD/PowerShell in this folder and run:

&nbsp;   .\\run.bat



run.bat also:

\- offers to install Visual Studio Build Tools automatically using winget (optional)

\- asks Yes/No to create a Desktop shortcut

\- asks Yes/No to launch the app after building



build.bat will:

\- call rc.exe to rebuild LANSCR.res

\- call cl.exe to build LANSCR.exe with the embedded resources



NOTE:

\- If cl.exe is not found, build.bat tries to auto-load Visual Studio environment.

\- If that fails, open "Developer PowerShell for VS 2022" and run build.bat again.





How to run

----------

Method 1: Double-click LANSCR.exe

Method 2: From terminal:

&nbsp; .\\LANSCR.exe



Or directly:

&nbsp; .\\LANSCR.exe --help





Important note about “running lanscr.cpp without EXE”

-----------------------------------------------------

C++ source code cannot run directly.

You MUST compile it into an EXE first.

build.bat is the setup/build script that creates the EXE.



