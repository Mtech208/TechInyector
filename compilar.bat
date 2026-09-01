@echo off
setlocal enabledelayedexpansion

rem ============================================================
rem  Compilador automatico de TechInyector
rem  Compila TechInyector.cpp + resources.rc -> TechInyector.exe
rem ============================================================

set "WORKDIR=%~dp0"
cd /d "%WORKDIR%"

rem ------------------------------------------------------------
rem 1) Buscar los Build Tools / Visual Studio (vcvars64.bat)
rem ------------------------------------------------------------
set "VCVARS="
for %%V in (
  "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do (
  if exist "%%~V" set "VCVARS=%%~V"
)

if not defined VCVARS (
  echo [ERROR] No se encontro vcvars64.bat de Visual Studio / Build Tools.
  echo         Instala "Desarrollo para escritorio con C++" y vuelve a intentar.
  pause
  exit /b 1
)

echo [OK] Encontrando Visual Studio: %VCVARS%
call "%VCVARS%" >nul 2>&1

rem ------------------------------------------------------------
rem 2) Buscar el rc.exe MODERNO del Windows SDK (acepta icono PNG)
rem ------------------------------------------------------------
set "RC="
for /d %%K in ("C:\Program Files (x86)\Windows Kits\10\bin\*" "C:\Program Files\Windows Kits\10\bin\*") do (
  if exist "%%~K\x64\rc.exe" set "RC=%%~K\x64\rc.exe"
)

if not defined RC (
  echo [ERROR] No se encontro rc.exe del Windows SDK.
  pause
  exit /b 1
)

echo [OK] Encontrando rc.exe: %RC%

rem ------------------------------------------------------------
rem 3) Compilar recursos (icono) y codigo fuente
rem ------------------------------------------------------------
echo.
echo [1/2] Compilando recursos...
"%RC%" resources.rc
if errorlevel 1 (
  echo [ERROR] Fallo la compilacion de recursos.^n
  pause
  exit /b 1
)

    echo [2/2] Compilando codigo fuente...
cl /nologo /EHsc TechInyector.cpp resources.res /Fe:TechInyector.exe user32.lib gdi32.lib comdlg32.lib shell32.lib advapi32.lib gdiplus.lib
if errorlevel 1 (
  echo [ERROR] Fallo la compilacion del codigo fuente.^n
  pause
  exit /b 1
)

echo.
echo ============================================
echo  Compilacion exitosa: TechInyector.exe
echo ============================================
pause
exit /b 0
