@echo off
setlocal
cd /d "%~dp0.."

rem ============================================================================
rem  Makes a release ZIP of AutoDraft that anyone can download and run
rem  (no Qt needed): release_out\AutoDraft-v<version>-win64.zip
rem
rem    scripts\make_release.bat 1.0      -> AutoDraft-v1.0-win64.zip
rem    scripts\make_release.bat          -> AutoDraft-<yyyymmdd>-win64.zip
rem
rem  The ZIP is built from this source tree only (not from your own AutoDraft\ run
rem  folder), so it never contains your personal data. For a reproducible release run
rem  it from a freshly extracted copy of the source.
rem  Inside the ZIP, AutoDraft\ holds the program plus README.txt, TERMS.txt and
rem  NOTICE.md (the terms: free to use, redistribution allowed).
rem ============================================================================
call "%~dp0env.bat"
if errorlevel 1 goto no_qt

set "VER=%~1"
if "%VER%"=="" goto date_version
set "VER=v%VER%"
goto have_version
:date_version
for /f %%D in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd"') do set "VER=%%D"
:have_version

set "NAME=AutoDraft-%VER%-win64"
set "OUTDIR=%CD%\release_out"
set "STAGE=%OUTDIR%\stage"
set "ZIP=%OUTDIR%\%NAME%.zip"

rem --- 1) release build -------------------------------------------------------
echo [release] Building (release)...
if not exist build mkdir build
pushd build
qmake ..\AutoDraftGit.pro -spec win32-g++
if errorlevel 1 goto build_failed
mingw32-make -j%NUMBER_OF_PROCESSORS% release
if errorlevel 1 goto build_failed
popd

rem --- 2) clean staging folder: program + Qt libraries + resources -------------
echo [release] Collecting files...
if exist "%STAGE%" rmdir /s /q "%STAGE%"
call "%~dp0deploy.bat" "%STAGE%\AutoDraft"
if errorlevel 1 goto deploy_failed

copy /y "%~dp0release_files\README.txt" "%STAGE%\AutoDraft\" >nul
copy /y "%~dp0release_files\TERMS.txt" "%STAGE%\AutoDraft\" >nul
copy /y "%CD%\NOTICE.md" "%STAGE%\AutoDraft\" >nul

rem --- 3) ZIP (Windows built-in tar writes a standard .zip) ---------------------
echo [release] Creating the ZIP...
if exist "%ZIP%" del "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%STAGE%" AutoDraft
if errorlevel 1 goto zip_failed

powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%ZIP%').Hash" > "%ZIP%.sha256.txt"
for %%F in ("%ZIP%") do echo [release] Done: %%~fF  (%%~zF bytes)
echo [release] SHA256 is in %ZIP%.sha256.txt
echo [release] Upload the ZIP to GitHub: the public releases repository, Releases, Draft a new release.
exit /b 0

:no_qt
echo [release] Qt or the compiler was not found. Run setup.bat first.
exit /b 1

:build_failed
popd
echo [release] Build failed. Check the lines containing "error" above.
exit /b 1

:deploy_failed
echo [release] Collecting the program files failed.
exit /b 1

:zip_failed
echo [release] Creating the ZIP failed.
exit /b 1
