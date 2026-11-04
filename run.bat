@echo off
setlocal
cd /d "%~dp0"

rem ============================================================================
rem  Starts AutoDraft from the stand-alone run folder (AutoDraft\AutoDraft.exe).
rem  No Qt installation is needed to run it. Run build.bat first.
rem ============================================================================
set "EXE=%~dp0AutoDraft\AutoDraft.exe"
if not exist "%EXE%" goto no_exe

start "" /D "%~dp0AutoDraft" "%EXE%"
echo [run] AutoDraft started.
exit /b 0

:no_exe
echo [run] The program is not built yet. Run build.bat first.
exit /b 1
