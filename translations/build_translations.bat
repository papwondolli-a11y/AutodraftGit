@echo off
REM ============================================================================
REM  AutoDraftENG - Korean(source) -> English(.qm) translation build
REM
REM  1) lupdate : scans all tr()/translate() calls in the source and refreshes
REM               translations/autodraft_en.ts (keeps existing translations)
REM  2) lrelease: compiles autodraft_en.ts -> autodraft_en.qm
REM
REM  The .qm is embedded into the executable via translations.qrc (:/i18n/).
REM  Run this whenever UI strings change, then rebuild the app.
REM ============================================================================
setlocal
set QTBIN=C:\Qt\6.11.0\mingw_64\bin
set ROOT=%~dp0..

echo [1/2] lupdate...
"%QTBIN%\lupdate.exe" "%ROOT%\AutoDraft.pro" -ts "%~dp0autodraft_en.ts" -locations relative -no-obsolete
if errorlevel 1 goto :err

echo [2/2] lrelease...
"%QTBIN%\lrelease.exe" "%~dp0autodraft_en.ts" -qm "%~dp0autodraft_en.qm"
if errorlevel 1 goto :err

echo Done. Rebuild the app to embed the updated %~dp0autodraft_en.qm
endlocal
exit /b 0

:err
echo FAILED.
endlocal
exit /b 1
