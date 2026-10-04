@echo off
rem Recreate E:\AVP2Source\toolchain\vc6_rtm_d3dren (outside git): the d3d.ren compiler, all three passes at the build the Rich header names
rem (Utc12 C/C++ 8168; objects carry comp id 10/8168 and 11/8168).  See config\d3dren\FACTS.md section 4.
rem   C1.DLL, C1XX.DLL  12.00.8168  VC6 RTM front ends, from the user's install E:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin
rem   C2.DLL            12.00.8168  RTM OPTIMISING back end (Professional/Enterprise), reference copy
rem                                 E:\AVP2Source\toolchain\vc6_rtm_c2_optimizing\C2.DLL (sha1 ca6fc8ec9a81dd90252ddf513bab048f2e5c21e2, 737,329 bytes)
rem   CL.EXE            12.00.8804  driver of the SP5 payload (E:\MSVC6\VC98\BIN, byte-identical to the SP6 one).  NOT the RTM install's CL.EXE
rem                                 (49,152 bytes, 11.99.8168): that is the Standard edition driver, it silently drops /O2, /O1 and /Ob2.
rem The driver only expands options (/O2 -> -Gf -Og -Oi -Ot -Oy -Ob1 + C2 -Gy ...) and adds -D_MSC_FULL_VER=12008804 (nothing in the sources reads it);
rem the passes alone decide the code.  MSPDB60.DLL/MSOBJ10.DLL are byte-identical in the RTM and SP5 installs and come from E:\MSVC6\COMMON\MSDev98\Bin (PATH).
rem The Processor Pack C2 (13.0.9044, what lithtech.exe was built with) stays in E:\MSVC6\VC98\BIN and is not touched; vc6_sp5 (SP5 front ends + SP5 C2 12.0.8966)
rem is no longer used by d3dren.
set TC=E:\AVP2Source\toolchain\vc6_rtm_d3dren
set RTM=E:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin
if not exist %TC%\BIN mkdir %TC%\BIN
copy /y "%RTM%\C1.DLL" %TC%\BIN\C1.DLL
copy /y "%RTM%\C1XX.DLL" %TC%\BIN\C1XX.DLL
copy /y E:\AVP2Source\toolchain\vc6_rtm_c2_optimizing\C2.DLL %TC%\BIN\C2.DLL
copy /y E:\MSVC6\VC98\BIN\CL.EXE %TC%\BIN\CL.EXE
