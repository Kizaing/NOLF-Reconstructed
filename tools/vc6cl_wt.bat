@echo off
rem vc6cl.bat for a git worktree: headers in include/ say "../../build/proj/..." and "../../jupiter/...",
rem which only resolve when the include dir sits two levels below E:\AVP2Source. The last INCLUDE entry
rem (a harmless dir two levels below it) makes them resolve from a worktree too.
set PATH=E:\MSVC6\VC98\BIN;E:\MSVC6\COMMON\MSDev98\Bin;%PATH%
set INCLUDE=E:\AVP2Source\build\proj\LT2\sdk\inc;E:\MSVC6\VC98\INCLUDE;E:\MSVC6\VC98\MFC\INCLUDE;E:\AVP2Source\tools\objdiff;E:\AVP2Source\build\proj\LT2
set LIB=E:\MSVC6\VC98\LIB
cl /nologo %*
