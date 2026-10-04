@echo off
rem Decompile every function of the D3DREN project to a C dump (read-only). usage: decompile_d3dren.bat [out.c]
set S=E:\AVP2Source\scripts
set O=E:\AVP2Source\out\decomp\d3d_ren_v2.c
if not "%~1"=="" set O=%~1
call %S%\ghidra_headless.bat E:\AVP2Source\ghidra_ren D3DREN -process d3d.ren -noanalysis -readOnly -scriptPath %S% -postScript DecompileAll.java "%O%" 60 > E:\AVP2Source\logs\d3dren\decompile_v2.log 2>&1
