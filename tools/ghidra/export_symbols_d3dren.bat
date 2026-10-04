@echo off
rem Regenerate config\d3dren\symbols.csv + symbols_summary.txt from the D3DREN Ghidra project (read-only).
rem usage: export_symbols_d3dren.bat [outdir]   (default: config\d3dren of this checkout)
rem (paths are passed without a trailing backslash: "dir\" would escape the closing quote)
set S=E:\AVP2Source\scripts
set H=%~dp0
set H=%H:~0,-1%
set C=%H%\..\..\config\d3dren
if not "%~1"=="" set C=%~1
if not exist "%C%" mkdir "%C%"
call %S%\ghidra_headless.bat E:\AVP2Source\ghidra_ren D3DREN -process d3d.ren -noanalysis -readOnly -scriptPath "%H%" -postScript ExportSymbolsPE.java "%C%\symbols.csv" "%C%\symbols_summary.txt" > E:\AVP2Source\logs\d3dren\export_symbols.log 2>&1
