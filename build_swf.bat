@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem === Caminhos dos executáveis ===
set "SWFMILL=C:\msys64\mingw64\bin\swfmill.exe"
set "MTASC=C:\Users\igora\Downloads\mtasc\mtasc.exe"

rem === Pastas/arquivos do projeto ===

set "XML=art\ui\flash\template.xml"
set "SRC=art\ui\flash\src"
set "OUT=build"
set "BASE=%OUT%\smsogauge_base.swf"
set "OUTSWF=%OUT%\smsogauge.swf"
set "DIST=Interface\"
set "MO2=D:\MO2\mods\SMSODestruction_Reactions\Interface"

if not exist "%OUT%" mkdir "%OUT%"
del /q "%BASE%" "%OUTSWF%" 2>nul


"%SWFMILL%" simple "%XML%" "%BASE%" || goto :err


"%MTASC%" -version 8 -cp "%SRC%" -swf "%BASE%" -out "%OUTSWF%" -main "Register.as" || goto :err

echo OK: "%OUTSWF%"
if not exist "%DIST%" mkdir "%DIST%"
copy /Y "%OUTSWF%" "%DIST%\smsogauge.swf" >nul

if exist "D:\MO2\mods" (
  if not exist "%MO2%" mkdir "%MO2%"
  copy /Y "%OUTSWF%" "%MO2%\smsogauge.swf" >nul
)

popd
exit /b 0

:err
echo.
echo FALHA na geração do SWF
popd
exit /b 1
