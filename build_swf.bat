@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem === Ferramentas ===
set "SWFMILL=D:\msys2\mingw64\bin\swfmill.exe"
set "MTASC=C:\Users\igora\Downloads\mtasc\mtasc.exe"

rem === Pastas/arquivos ===
set "SRC=art\ui\flash\src"
set "OUT=build"
set "DIST=Interface\erfgauge"
set "MO2=D:\MO2\mods\ElementalReactionsFramework\Interface\erfgauge"

rem Templates
set "GAUGE_XML=art\ui\flash\gauge_template.xml"

rem Saídas
set "GAUGE_BASE=%OUT%\erfgauge_base.swf"
set "GAUGE_OUT=%OUT%\erfgauge.swf"

if not exist "%OUT%" mkdir "%OUT%"
del /q "%GAUGE_BASE%" "%GAUGE_OUT%" 2>nul

rem 1) Base do gauge (simbolo exportado ERF_Gauge, sem assets)
"%SWFMILL%" simple "%GAUGE_XML%" "%GAUGE_BASE%" || goto :err

rem 2) Compila a logica (AS2) em cima do base
"%MTASC%" -version 8 -cp "%SRC%" -swf "%GAUGE_BASE%" -frame 1 -main "Register.as" -out "%GAUGE_OUT%" "%SRC%\ERF_Gauge.as" || goto :err

rem 4) Copia DDS para MO2 (se existir)
if exist "D:\MO2\mods" (
  if not exist "%MO2%" mkdir "%MO2%"
  copy /Y "%GAUGE_OUT%" "%MO2%\erfgauge.swf" >nul
)

echo OK
exit /b 0

:err
echo.
echo FALHA na geracao (SWF/DDS)
exit /b 1
