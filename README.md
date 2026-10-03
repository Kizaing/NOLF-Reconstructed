# AVP2 Reconstructed

A byte-matching decompilation of `lithtech.exe` from *Aliens versus Predator 2* (v1.0.9.6): the LithTech 2.2
"Talon" engine, rewritten as C++ that compiles with the original toolchain into the same machine code, function by
function.

## Status

| | Code matched | Functions |
|---|---|---|
| Whole binary (objdiff) | 91.2% | 4,517 of 4,772 |
| Engine code | 89.3% | |
| lithshared, WONAPI, VC6 CRT | 100% | |

- 3,648 annotated engine functions compile byte-for-byte identical to the original, relocation targets included.
- 51 functions are written but not yet matching (`// STUB:`); most differ only in register allocation,
  instruction scheduling or inlining decisions, and behave the same as the original (checked by a behaviour
  audit).
- 307 of 341 units are complete. Most fully matched units also relink byte-identically into the original layout.

Live progress: [decomp.dev](https://decomp.dev) (version `lithtech_1.0.9.6`), from the report in
`progress/lithtech_1.0.9.6/report.json`.

## How it works

- **Toolchain.** The original was built with Visual C++ 6.0 SP5 plus the Processor Pack, `/MT /O2`. Every
  function here is compiled with that exact compiler; no newer compiler produces the same code.
- **Annotations.** Each function in `src/` carries a [reccmp](https://github.com/isledecomp/reccmp)-style marker
  with its address in the original binary:
  ```cpp
  // FUNCTION: LITHTECH 0x0044cc80
  void SomeFunction(...)
  ```
  `// STUB:` marks a function that is written but doesn't match yet, `// GLOBAL:` a data symbol.
- **Checking.** `tools/build.py` compiles every unit, compares each annotated function's bytes and relocation
  targets with the original, and reports `MATCH`, `DIFF` or `SIZE`.
- **Progress.** The original binary is cut into per-unit target objects (`tools/mktarget.py`) and compared with
  the compiled objects by [objdiff](https://github.com/encounter/objdiff). Prebuilt library code (the VC6 CRT
  and WONAPI) is identified by `tools/libmatch.py` and counts as matched.
- **Layout.** `tools/relink.py` links the matched objects back into an executable to check that function order,
  data and literals land where the original has them.
- **Reference.** The source follows the structure and names of LithTech's later engine (Jupiter) and the Talon
  SDK where the binary agrees with them.

## Repository layout

```
src/        decompiled engine source, one .cpp per original translation unit (client, server, world, model, ...)
include/    reconstructed engine headers
config/     unit boundaries, symbol table exported from Ghidra, renames, library matches
tools/      build driver, checker, target-object writer, relinker, behaviour audit, matching aids
progress/   the committed objdiff progress report
```

## Building

This repository contains source code only. It does **not** include the game binary or any of the proprietary
inputs the build needs, and none are provided:

- `lithtech.exe` from your own copy of Aliens versus Predator 2 (v1.0.9.6);
- Visual C++ 6.0 SP5 with the Processor Pack;
- the Talon SDK and lithshared headers, and the LithTech Jupiter source used as a reference for some headers;
- the prebuilt VC6 CRT and WONAPI objects (for library matching).

The tools currently expect these at fixed paths on the author's machine (see the constants at the top of
`tools/build.py` and `scripts\vc6cl.bat`). With them in place:

```bash
python tools/build.py              # compile, check, write target objects, objdiff.json and the report
python tools/build.py check <unit> # compile and check one unit
python tools/build.py diff <func>  # side-by-side disassembly against the original
python tools/build.py report       # refresh progress/lithtech_1.0.9.6/report.json
```

Requires Python 3 with `capstone` and `pefile`, and `objdiff-cli`.

## Legal

This is an independent preservation and research project. It contains no game assets and no part of the original
binary; you need a legally obtained copy of the game to build or compare anything. Aliens
versus Predator 2 and LithTech are trademarks of their respective owners.
