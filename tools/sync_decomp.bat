@echo off
rem Apply decomp\config\renames.csv and splits.csv to the Ghidra project (writes; back up first).
set S=E:\AVP2Source\scripts
set C=E:\AVP2Source\decomp\config
call %S%\ghidra_headless.bat E:\AVP2Source\ghidra AVP2VT/named -process lithtech.exe -noanalysis -scriptPath %S% -postScript SyncDecomp.java %C%\renames.csv %C%\splits.csv E:\AVP2Source\logs\decomp_sync_revert.csv > E:\AVP2Source\logs\decomp_sync.log 2>&1
