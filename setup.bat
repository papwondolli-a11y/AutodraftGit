@echo off
setlocal
cd /d "%~dp0"

rem ============================================================================
rem  Run once. Installs Qt and the MinGW compiler if they are missing.
rem  Does nothing if they are already installed.
rem    - Install location : C:\Qt  (override with environment variable AUTODRAFT_QT_BASE)
rem    - Qt version       : SETUP_QT_VERSION below (must be a version aqtinstall supports)
rem ============================================================================
set "SETUP_QT_VERSION=6.10.3"
set "SETUP_MINGW_TOOL=tools_mingw1310"
set "SETUP_MINGW_ID=qt.tools.win64_mingw1310"

call "%~dp0scripts\env.bat"
if not errorlevel 1 goto already_installed

echo [setup] Qt was not found. Starting installation. Download is several hundred MB and may take a while.

rem --- Python ---------------------------------------------------------------
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if defined PY goto have_python

echo [setup] Python was not found. Trying to install it with winget.
where winget >nul 2>nul
if errorlevel 1 goto no_winget
winget install -e --id Python.Python.3.12 --silent --accept-package-agreements --accept-source-agreements
echo [setup] Python installed. Open a NEW terminal and run setup.bat again.
exit /b 2

:no_winget
echo [setup] winget is not available. Install Python 3 from https://www.python.org/downloads/ and run setup.bat again.
exit /b 1

:have_python
rem --- aqtinstall (command line Qt installer) in its own virtual environment ---
set "AQT_VENV=%LOCALAPPDATA%\AutoDraft\aqt-venv"
if exist "%AQT_VENV%\Scripts\aqt.exe" goto have_aqt
echo [setup] Preparing the installer tool (aqtinstall)...
%PY% -m venv "%AQT_VENV%"
if errorlevel 1 goto venv_failed
"%AQT_VENV%\Scripts\python.exe" -m pip install --quiet aqtinstall
if errorlevel 1 goto pip_failed

:have_aqt
if "%AUTODRAFT_QT_BASE%"=="" (set "INSTALL_BASE=C:\Qt") else (set "INSTALL_BASE=%AUTODRAFT_QT_BASE%")

rem --- Qt itself (only the parts this app uses: qtbase, qtdeclarative) --------
echo [setup] Installing Qt %SETUP_QT_VERSION% ...
"%AQT_VENV%\Scripts\aqt.exe" install-qt windows desktop %SETUP_QT_VERSION% win64_mingw -O "%INSTALL_BASE%" --archives qtbase qtdeclarative
if errorlevel 1 goto qt_failed

rem --- MinGW compiler ---------------------------------------------------------
echo [setup] Installing the compiler ...
"%AQT_VENV%\Scripts\aqt.exe" install-tool windows desktop %SETUP_MINGW_TOOL% %SETUP_MINGW_ID% -O "%INSTALL_BASE%"
if errorlevel 1 goto tool_failed

call "%~dp0scripts\env.bat"
if errorlevel 1 goto not_found_after
echo [setup] Done: %AD_QT_DIR%
exit /b 0

:already_installed
echo [setup] Qt is already installed: %AD_QT_DIR%
echo [setup] Compiler: %AD_MINGW_DIR%
echo [setup] Nothing to do.
exit /b 0

:venv_failed
echo [setup] Could not create the Python virtual environment.
exit /b 1

:pip_failed
echo [setup] Could not install aqtinstall. Check the internet connection and run again.
exit /b 1

:qt_failed
echo [setup] Qt installation failed. Check the internet connection and run again.
exit /b 1

:tool_failed
echo [setup] Compiler installation failed.
exit /b 1

:not_found_after
echo [setup] Installation finished but Qt or the compiler was not found in %INSTALL_BASE%
exit /b 1
