@echo off
rem Apply the high-confidence rows of a names CSV to the D3DREN Ghidra project (MODIFIES the project: back it up first,
rem E:\AVP2Source\backup\ghidra_ren_before_d3dren_*.zip).
rem usage: apply_names_d3dren.bat <names.csv> <revert.csv> <bookmark category> [confidences, default high]
set S=E:\AVP2Source\scripts
set H=%~dp0
set H=%H:~0,-1%
call %S%\ghidra_headless.bat E:\AVP2Source\ghidra_ren D3DREN -process d3d.ren -noanalysis -scriptPath "%H%" -postScript ApplyNamesPE.java "%~1" "%~2" "%~3" %4 > E:\AVP2Source\logs\d3dren\apply_names_%~3.log 2>&1
