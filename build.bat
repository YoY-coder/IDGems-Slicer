@echo off
REM ============================================================================
REM  IDGems Slicer - compilation automatique (Windows)
REM  Double-cliquez sur ce fichier. Il faut avoir installe Visual Studio
REM  avec la charge de travail "Developpement Desktop en C++".
REM ============================================================================
setlocal
cd /d "%~dp0"

echo.
echo  === IDGems Slicer : compilation ===
echo.

REM --- 1. Trouver Visual Studio et son CMake integre ---------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERREUR] Visual Studio n'est pas installe.
    goto :fail
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" (
    echo [ERREUR] Visual Studio est installe mais SANS "Developpement Desktop en C++".
    echo          Ouvrez "Visual Studio Installer" ^> Modifier ^> cochez cette charge de travail.
    goto :fail
)
set "CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"
echo Visual Studio : %VSPATH%
echo.

REM --- 2. Configuration (telecharge JUCE la 1re fois, ~1-3 min) ---------------
echo [1/3] Configuration du projet...
"%CMAKE%" -S . -B build -A x64
if errorlevel 1 goto :fail

REM --- 3. Compilation ---------------------------------------------------------
echo.
echo [2/3] Compilation (plusieurs minutes la 1re fois)...
"%CMAKE%" --build build --config Release --parallel
if errorlevel 1 goto :fail

REM --- 4. Copie du plugin dans le dossier "Plugin" ----------------------------
echo.
echo [3/3] Copie du plugin...
if exist "Plugin\IDGems Slicer.vst3" rmdir /s /q "Plugin\IDGems Slicer.vst3"
if not exist "Plugin" mkdir "Plugin"
xcopy /e /i /y /q "build\IDGemsSlicer_artefacts\Release\VST3\IDGems Slicer.vst3" "Plugin\IDGems Slicer.vst3" >nul
if errorlevel 1 (
    echo [ERREUR] Copie impossible. Ableton est peut-etre ouvert avec le plugin charge :
    echo          fermez Ableton et relancez build.bat.
    goto :fail
)
copy /y "build\IDGemsSlicer_artefacts\Release\Standalone\IDGems Slicer.exe" "Plugin\" >nul

echo.
echo  === TERMINE ===
echo  Plugin VST3  : %CD%\Plugin\IDGems Slicer.vst3
echo  Standalone   : %CD%\Plugin\IDGems Slicer.exe
echo.
pause
exit /b 0

:fail
echo.
echo  === ECHEC : copiez le texte ci-dessus et envoyez-le a Claude ===
echo.
pause
exit /b 1
