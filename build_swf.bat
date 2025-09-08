@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem === Caminhos dos executáveis ===
set "SWFMILL=C:\msys64\mingw64\bin\swfmill.exe"
set "MTASC=C:\Users\igora\Downloads\mtasc\mtasc.exe"

rem === Pastas/arquivos do projeto ===
set "ROOT=%~dp0"
set "XML=%ROOT%art\ui\flash\template.xml"
set "SRC=%ROOT%art\ui\flash\src"
set "OUT=%ROOT%build"
set "BASE=%OUT%\smsogauge_base.swf"
set "OUTSWF=%OUT%\smsogauge.swf"
set "DIST=%ROOT%Interface\SMSO"
set "MO2=D:\MO2\mods\SMSODestruction\Interface\SMSO"

rem === Arquivos .as (ajuste os nomes se forem diferentes) ===
set "AS_MAIN=Main.as"
set "AS_FILES=Main.as SMSO_Gauge.as"

rem --- Checagens ---
if not exist "%SWFMILL%" echo ERRO: swfmill nao encontrado em "%SWFMILL%" & exit /b 1
if not exist "%MTASC%"   echo ERRO: mtasc nao encontrado em "%MTASC%"     & exit /b 1
if not exist "%XML%"     echo ERRO: Nao encontrei "%XML%"                  & exit /b 1
if not exist "%SRC%\%AS_MAIN%" echo ERRO: Nao encontrei "%SRC%\%AS_MAIN%"  & exit /b 1
if not exist "%SRC%\SMSO_Gauge.as" echo ERRO: Nao encontrei "%SRC%\SMSO_Gauge.as" & exit /b 1

if not exist "%OUT%"  mkdir "%OUT%" 2>nul
if not exist "%DIST%" mkdir "%DIST%" 2>nul

echo [1/2] swfmill -> "%BASE%"
"%SWFMILL%" simple "%XML%" "%BASE%"
if errorlevel 1 goto :err

echo [2/2] mtasc -> "%OUTSWF%"
rem IMPORTANTE: -main para executar Main.main(...) quando o SWF for carregado
"%MTASC%" -version 8 -cp "%SRC%" -swf "%BASE%" -out "%OUTSWF%" -header 256:256:30 -main %AS_FILES%
if errorlevel 1 goto :err

echo OK: "%OUTSWF%"
copy /Y "%OUTSWF%" "%DIST%\smsogauge.swf" >nul
copy /Y "%OUTSWF%" "%MO2%\smsogauge.swf" >nul
echo Copiado para %DIST%\smsogauge.swf
echo Copiado para %MO2%\smsogauge.swf
exit /b 0

:err
echo FALHA na geracao do SWF
exit /b 1
