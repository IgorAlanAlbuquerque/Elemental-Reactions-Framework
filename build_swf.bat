@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem === Caminhos dos executáveis ===
set "SWFMILL=C:\msys64\mingw64\bin\swfmill.exe"
set "MTASC=C:\Users\igora\Downloads\mtasc\mtasc.exe"

rem === Pastas/arquivos do projeto ===

set "XML=art\ui\flash\template.xml"
set "SRC=art\ui\flash\src"
set "OUT=build"
set "BASE=%OUT%\erfgauge_base.swf"
set "OUTSWF=%OUT%\erfgauge.swf"
set "DIST=Interface\"
set "MO2=D:\MO2\mods\ElementalReactionsFramework\Interface"

if not exist "%OUT%" mkdir "%OUT%"
del /q "%BASE%" "%OUTSWF%" 2>nul

"%SWFMILL%" simple "%XML%" "%BASE%" || goto :err

"%MTASC%" -version 8 -cp "%SRC%" -swf "%BASE%" -frame 1 -main "Register.as" -out "%OUTSWF%" "%SRC%\ERF_Gauge.as" || goto :err

if not exist "%DIST%" mkdir "%DIST%"
copy /Y "%OUTSWF%" "%DIST%\erfgauge.swf" >nul

if exist "D:\MO2\mods" (
  if not exist "%MO2%" mkdir "%MO2%"
  copy /Y "%OUTSWF%" "%MO2%\erfgauge.swf" >nul
)

popd
exit /b 0

:err
echo.
echo FALHA na geração do SWF
popd
exit /b 1
