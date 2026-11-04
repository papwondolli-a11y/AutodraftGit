@echo off
rem ============================================================================
rem  Shared environment. Called by setup.bat / build.bat / run.bat (do not run directly).
rem  Finds Qt and the MinGW compiler and puts them on PATH.
rem
rem  Search order for the base folder:
rem    1) environment variable AUTODRAFT_QT_BASE (e.g. D:\Qt)
rem    2) C:\Qt
rem  Uses the first 6.x\mingw_64 and Tools\mingw*_64 found (highest folder name first).
rem  Result: sets AD_QT_DIR and AD_MINGW_DIR. Exits with errorlevel 1 if not found.
rem ============================================================================

if "%AUTODRAFT_QT_BASE%"=="" (set "AD_QT_BASE=C:\Qt") else (set "AD_QT_BASE=%AUTODRAFT_QT_BASE%")

set "AD_QT_DIR="
set "AD_MINGW_DIR="

for /f "delims=" %%D in ('dir /b /ad /o-n "%AD_QT_BASE%\6.*" 2^>nul') do (
    if not defined AD_QT_DIR if exist "%AD_QT_BASE%\%%D\mingw_64\bin\qmake.exe" set "AD_QT_DIR=%AD_QT_BASE%\%%D\mingw_64"
)
for /f "delims=" %%D in ('dir /b /ad /o-n "%AD_QT_BASE%\Tools\mingw*_64" 2^>nul') do (
    if not defined AD_MINGW_DIR if exist "%AD_QT_BASE%\Tools\%%D\bin\g++.exe" set "AD_MINGW_DIR=%AD_QT_BASE%\Tools\%%D"
)

if not defined AD_QT_DIR exit /b 1
if not defined AD_MINGW_DIR exit /b 1

set "PATH=%AD_QT_DIR%\bin;%AD_MINGW_DIR%\bin;%PATH%"
exit /b 0
