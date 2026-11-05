@echo off
setlocal
rem ============================================================================
rem  Builds the stand-alone run folder  AutoDraft\  from the release build.
rem  Double-click AutoDraft\AutoDraft.exe to run - no Qt installation needed.
rem  Called by build.bat. If AutoDraft.exe is running (file locked) the deploy is
rem  postponed: build\.deploy_pending is created and the next build finishes it.
rem ============================================================================
set "ROOT=%~dp0.."
set "SRC=%ROOT%\build\release\AutoDraftGit.exe"
set "OUT=%ROOT%\AutoDraft"
rem Optional argument: a different output folder (used by make_release.bat).
if not "%~1"=="" set "OUT=%~1"
set "PENDING=%ROOT%\build\.deploy_pending"

call "%~dp0env.bat"
if errorlevel 1 ( echo [deploy] Qt or the compiler was not found. Run setup.bat first. & exit /b 1 )

if not exist "%SRC%" ( echo [deploy] Release build not found: %SRC% & exit /b 1 )
if not exist "%OUT%" mkdir "%OUT%"

rem --- program (fails if the app is running because the file is locked) ----------
copy /y "%SRC%" "%OUT%\AutoDraft.exe" >nul 2>nul
if errorlevel 1 goto locked

rem --- Qt libraries / plugins / compiler runtime -------------------------------
windeployqt --release --compiler-runtime --no-translations --no-quick-import --no-system-d3d-compiler --no-opengl-sw "%OUT%\AutoDraft.exe" >nul
if errorlevel 1 ( echo [deploy] windeployqt failed. & exit /b 1 )

rem --- Qt dialog translation (Korean) ------------------------------------------
if exist "%AD_QT_DIR%\translations\qt_ko.qm" (
    if not exist "%OUT%\translations" mkdir "%OUT%\translations"
    copy /y "%AD_QT_DIR%\translations\qt_ko.qm" "%OUT%\translations\qt_ko.qm" >nul
)

rem --- program resources --------------------------------------------------------
robocopy "%ROOT%\blocks" "%OUT%\blocks" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto copy_failed
robocopy "%ROOT%\frames" "%OUT%\frames" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto copy_failed
robocopy "%ROOT%\tools" "%OUT%\tools" *.exe /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto copy_failed
rem Data holds user-made presets: only add files that are missing, never overwrite.
robocopy "%ROOT%\Data" "%OUT%\Data" /E /XC /XN /XO /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto copy_failed

if exist "%PENDING%" del "%PENDING%"
echo [deploy] Ready: %OUT%\AutoDraft.exe
exit /b 0

:locked
if not exist "%ROOT%\build" mkdir "%ROOT%\build"
echo pending> "%PENDING%"
echo [deploy] AUTODRAFT_RUNNING: AutoDraft.exe is running, so the new version was not applied yet. Close it and build again.
exit /b 3

:copy_failed
echo [deploy] Copying resources failed.
exit /b 1
