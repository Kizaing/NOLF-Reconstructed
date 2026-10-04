@echo off
rem Compile d3d.ren units with the compiler the DLL was built with: VC6 RTM front ends C1/C1XX 12.00.8168 + the RTM OPTIMISING back end
rem C2 12.00.8168 (the build named by d3d.ren's Rich header: Utc12 C/C++ 8168), driven by the SP5 driver CL.EXE 12.00.8804 (the RTM install's own
rem CL.EXE is the Standard edition driver and drops /O2).  That is NOT the compiler that built lithtech.exe (Processor Pack C2 13.0.9044,
rem E:\AVP2Source\scripts\vc6cl.bat).  Evidence and measurements: config\d3dren\FACTS.md ("Toolchain").
rem The toolchain copy outside git is made by tools\mk_d3dren_toolchain.bat.
rem Headers: Talon SDK, then the DX 8.0 SDK tree (read-only reference; %DX8INC% is set by build.py from tools\modcfg.py; it holds the
rem DirectDraw 7 / Direct3D 7 headers the renderer uses), then the SP5 versions of VC98's malloc.h/fpieee.h (the Processor Pack
rem replaced them), then VC98.  The DX tree must precede VC98\INCLUDE, whose d3d.h is an old (DX3) one.
rem The RTM install's VC98\Include (long file names: <algorithm>, <exception>, <streambuf>, <functional> ... which E:\MSVC6\VC98\INCLUDE lacks)
rem is APPENDED to the path: every header that exists in the earlier directories resolves exactly as before, only names missing there fall
rem through.  (Replacing E:\MSVC6\VC98\INCLUDE by the RTM tree changes the code generated for a few functions (a 4-byte mov esi/edi swap:
rem FUN_100088ec / FUN_10008a23 worse at one snapshot, FUN_10026700 / FUN_10026bd2 better at a later one): the header text of oleauto.h differs
rem between the trees and this compiler's scheduling is fragile to such differences.  Measured on all 35 objects: appended path = byte-identical
rem objects and statuses, replaced path = 3-4 objects differ.  See README_D3DREN.md "Standard headers".)
set TC=E:\AVP2Source\toolchain\vc6_rtm_d3dren
set PATH=%TC%\BIN;E:\MSVC6\COMMON\MSDev98\Bin;%PATH%
set INCLUDE=E:\AVP2Source\build\proj\LT2\sdk\inc;%DX8INC%;E:\MSVC6\backup_pre_procpack\INCLUDE;E:\MSVC6\VC98\INCLUDE;E:\MSVC6\VC98\MFC\INCLUDE;E:\Program Files (x86)\Microsoft Visual Studio\VC98\Include;E:\Program Files (x86)\Microsoft Visual Studio\VC98\MFC\Include
set LIB=E:\MSVC6\VC98\LIB
cl /nologo %*
