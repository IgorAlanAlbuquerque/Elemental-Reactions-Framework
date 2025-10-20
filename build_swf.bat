@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem === Ferramentas ===
set "SWFMILL=C:\msys64\mingw64\bin\swfmill.exe"
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

rem Texturas (PNG -> DDS)
set "ICON_SRC=art\source\icons"
set "TEX_OUT=Data\textures\erf\icons"
set "MO2_TEX=D:\MO2\mods\ElementalReactionsFramework\textures\erf\icons"

if not exist "%OUT%" mkdir "%OUT%"
del /q "%GAUGE_BASE%" "%GAUGE_OUT%" 2>nul

rem 0) Converte PNG -> DDS (DXT5, sem mipmap) usando texconv do PATH
where texconv >nul 2>nul
if errorlevel 1 (
  echo [AVISO] texconv nao encontrado no PATH. Pulando conversao de DDS...
) else (
  if not exist "%TEX_OUT%" mkdir "%TEX_OUT%"
  for %%F in ("%ICON_SRC%\*.png") do (
    texconv -nologo -y -ft DDS -f DXT5 -dx9 -w 64 -h 64 -m 0 -o "%TEX_OUT%" "%%~fF" || goto :err
  )
)

rem 1) Base do gauge (simbolo exportado ERF_Gauge, sem assets)
"%SWFMILL%" simple "%GAUGE_XML%" "%GAUGE_BASE%" || goto :err

rem 2) Compila a logica (AS2) em cima do base
"%MTASC%" -version 8 -cp "%SRC%" -swf "%GAUGE_BASE%" -frame 1 -main "Register.as" -out "%GAUGE_OUT%" "%SRC%\ERF_Gauge.as" || goto :err

rem 3) Copia gauge para Interface\erfgauge
if not exist "%DIST%" mkdir "%DIST%"
copy /Y "%GAUGE_OUT%" "%DIST%\erfgauge.swf" >nul

rem 4) Copia DDS para MO2 (se existir)
if exist "D:\MO2\mods" (
  if not exist "%MO2%" mkdir "%MO2%"
  copy /Y "%GAUGE_OUT%" "%MO2%\erfgauge.swf" >nul

  if not exist "%MO2_TEX%" mkdir "%MO2_TEX%"
  for %%F in ("%TEX_OUT%\*.dds") do (
    copy /Y "%%~fF" "%MO2_TEX%\" >nul
  )
)

echo OK
exit /b 0

:err
echo.
echo FALHA na geracao (SWF/DDS)
exit /b 1
