@echo off
rem Regenerate decomp\config\symbols.csv + symbols_summary.txt from the Ghidra project (read-only).
set S=E:\AVP2Source\scripts
set C=E:\AVP2Source\decomp\config
if not exist %C% mkdir %C%
call %S%\ghidra_headless.bat E:\AVP2Source\ghidra AVP2VT/named -process lithtech.exe -noanalysis -readOnly -scriptPath %S% -postScript ExportSymbols.java %C%\symbols.csv %C%\symbols_summary.txt > E:\AVP2Source\logs\export_symbols.log 2>&1
