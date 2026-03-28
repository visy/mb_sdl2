@echo off
setlocal

set ORIG_ZIP=%~dp0mnb311fw.zip
set REL_DIR=%~dp0rel
set MB_C_DIR=%~dp0..\mb_c

:: 1. Clean out rel/ subdirectory
if exist "%REL_DIR%" rd /s /q "%REL_DIR%"
mkdir "%REL_DIR%"

:: 2. Unzip original game as base
echo Extracting base game...
7z x "%ORIG_ZIP%" -o"%REL_DIR%" -y >nul
if errorlevel 1 (
    echo ERROR: Failed to extract %ORIG_ZIP%
    exit /b 1
)

:: 3. Delete original exe files
del /q "%REL_DIR%\*.EXE" 2>nul

:: 4. Copy our files
echo Copying mb_c files...
copy /y "%MB_C_DIR%\mb_c.exe" "%REL_DIR%\MB.EXE" >nul
copy /y "%~dp0INPUT.CFG" "%REL_DIR%\INPUT.CFG" >nul
copy /y "%~dp0OPTIONS.CFG" "%REL_DIR%\OPTIONS.CFG" >nul
copy /y "%~dp0SDL2.dll" "%REL_DIR%\SDL2.dll" >nul
copy /y "%~dp0SDL2_mixer.dll" "%REL_DIR%\SDL2_mixer.dll" >nul

:: 5. Zip it up
echo Creating mb_c_v0.zip...
if exist "%~dp0mb_c_v0.zip" del /q "%~dp0mb_c_v0.zip"
pushd "%REL_DIR%"
7z a -tzip "%~dp0mb_c_v0.zip" * >nul
popd

if errorlevel 1 (
    echo ERROR: Failed to create zip
    exit /b 1
)

echo Done: %~dp0mb_c_v0.zip
