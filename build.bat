@echo off
setlocal
cd /d "%~dp0"

rem ============================================================================
rem  build.bat          : release build + stand-alone run folder (AutoDraft\)
rem  build.bat debug    : debug build only (build\debug), for debugging
rem  The first build takes a while; later builds only recompile changed files.
rem  When .cpp/.h files are added, AutoDraftGit.pro changes and qmake re-runs by itself.
rem ============================================================================
call "%~dp0scripts\env.bat"
if errorlevel 1 goto no_qt

if not exist build mkdir build
cd build

if /i "%~1"=="debug" goto build_debug

echo [build] Configuring...
qmake ..\AutoDraftGit.pro -spec win32-g++
if errorlevel 1 goto qmake_failed

echo [build] Compiling (release)...
mingw32-make -j%NUMBER_OF_PROCESSORS% release
if errorlevel 1 goto make_failed

cd /d "%~dp0"
call "%~dp0scripts\deploy.bat"
set "DEPLOY_RC=%errorlevel%"
if "%DEPLOY_RC%"=="1" goto deploy_failed

rem Create a double-click shortcut "AutoDraft" in the project folder (points to the run folder).
if exist "%~dp0AutoDraft\AutoDraft.exe" (
    powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%~dp0AutoDraft.lnk'); $s.TargetPath='%~dp0AutoDraft\AutoDraft.exe'; $s.WorkingDirectory='%~dp0AutoDraft'; $s.Description='AutoDraft (your latest build)'; $s.Save()" >nul 2>nul
)
echo [build] Done. Run: %~dp0AutoDraft\AutoDraft.exe  (or the AutoDraft shortcut in this folder)
exit /b %DEPLOY_RC%

:build_debug
echo [build] Configuring (debug)...
qmake ..\AutoDraftGit.pro -spec win32-g++ "CONFIG+=debug"
if errorlevel 1 goto qmake_failed
echo [build] Compiling (debug)...
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

:deploy_failed
echo [build] Creating the run folder failed.
exit /b 1
