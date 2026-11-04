@echo off
setlocal
cd /d "%~dp0"

rem ============================================================================
rem  Builds AutoDraft (debug build, output in build\debug).
rem  The first build takes a long time; later builds only recompile changed files.
rem  When .cpp/.h files are added, AutoDraftGit.pro changes and qmake re-runs by itself.
rem ============================================================================
call "%~dp0scripts\env.bat"
if errorlevel 1 goto no_qt

tasklist /FI "IMAGENAME eq AutoDraftGit.exe" 2>nul | find /I "AutoDraftGit.exe" >nul
if not errorlevel 1 echo [build] AutoDraft is running. The last build step may fail. Close the app and run again.

if not exist build mkdir build
cd build

echo [build] Configuring...
qmake ..\AutoDraftGit.pro -spec win32-g++ "CONFIG+=debug"
if errorlevel 1 goto qmake_failed

echo [build] Compiling... (the first build can take 10-30 minutes)
mingw32-make -j%NUMBER_OF_PROCESSORS% debug
if errorlevel 1 goto make_failed

echo [build] Done: %~dp0build\debug\AutoDraftGit.exe
exit /b 0

:no_qt
echo [build] Qt or the compiler was not found. Run setup.bat first.
exit /b 1

:qmake_failed
echo [build] qmake failed.
exit /b 1

:make_failed
echo [build] Build failed. Check the lines containing "error" above.
exit /b 1
