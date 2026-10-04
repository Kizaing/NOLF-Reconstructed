@echo off
rem Run FixBoundariesPE.java on the D3DREN project (modifies the project; back it up first:
rem E:\AVP2Source\backup\ghidra_ren_before_d3dren_phase0.zip). Extra script args (labeltable=lo-hi, override=addr=action)
rem may follow. Logs: E:\AVP2Source\logs\d3dren\fix_boundaries*.
set S=E:\AVP2Source\scripts
set H=%~dp0
set H=%H:~0,-1%
set L=E:\AVP2Source\logs\d3dren
call %S%\ghidra_headless.bat E:\AVP2Source\ghidra_ren D3DREN -process d3d.ren -noanalysis -scriptPath "%H%" -postScript FixBoundariesPE.java %L%\fix_boundaries_changes.csv %L%\fix_boundaries_report.txt %* > %L%\fix_boundaries.log 2>&1
