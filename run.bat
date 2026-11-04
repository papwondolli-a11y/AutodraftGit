@echo off
setlocal
cd /d "%~dp0"

rem ============================================================================
rem  Starts the built AutoDraft. Does nothing if it is already running
rem  (a second instance cannot take over the AI bridge pipe).
rem ============================================================================
call "%~dp0scripts\env.bat"
if errorlevel 1 goto no_qt

set "EXE=%~dp0build\debug\AutoDraftGit.exe"
if not exist "%EXE%" goto no_exe

tasklist /FI "IMAGENAME eq AutoDraftGit.exe" 2>nul | find /I "AutoDraftGit.exe" >nul
if not errorlevel 1 goto already_running

start "" /D "%~dp0" "%EXE%"
echo [run] AutoDraft started.
exit /b 0

:no_qt
echo [run] Qt was not found. Run setup.bat first.
exit /b 1

:no_exe
echo [run] The program is not built yet. Run build.bat first.
exit /b 1

:already_running
echo [run] AutoDraft is already running.
exit /b 0
