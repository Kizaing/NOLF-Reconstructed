r"""d3d.ren unit recovery, step 1: scan every renderer function.

Adapted from the lithtech module's units_scan.py.  Reads config/d3dren/symbols.csv and libraries.json, disassembles
each non-library function with capstone and records, per function:

  calls   direct call/jmp targets outside the function (text VAs), `tcalls` the subset that are `jmp` (tail calls)
  icalls  imported functions called through the IAT (call/jmp dword ptr [iat])
  refs    every 32-bit operand (immediate or memory displacement) that points into the image outside .text:
          split into rdata / data (initialised .data) / bss ; text immediates (function pointers) in `trefs`
  eh      none (VC6 without /GX has no EH tables) - kept for symmetry

Also writes the section layout and the library address ranges (these ranges are excluded from every unit).

Output: build/d3dren/units/scan.json   (the build/ directory is generated and ignored by git)

  python tools/d3dren_units_scan.py
"""
import csv
import json
import os
import struct

import capstone
import pefile

ROOT = r'E:\AVP2Source\decomp_d3dren'
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, 'build', 'd3dren', 'units')
IMAGE = r'E:\AVP2Source\bin\talon\d3d.ren'
SYMS = os.path.join(REPO, 'config', 'd3dren', 'symbols.csv')
LIBS = os.path.join(REPO, 'config', 'd3dren', 'libraries.json')

# libraries.json "unverified" single-function matches that are demonstrably renderer code (see units_proposal.md):
# _srand/_localeconv at 100099a9/100099b3 are a getter/setter pair for DAT_100528d8 sitting between two ConVar
# initialisers of the first renderer object, not CRT code (rand.obj's _holdrand lives in initialised .data).
LIB_FALSE_POSITIVES = {'100099a9', '100099b3'}


def load_image():
    pe = pefile.PE(IMAGE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    secs = {}
    for s in pe.sections:
        nm = s.Name.rstrip(b'\0').decode()
        lo = base + s.VirtualAddress
        secs[nm] = dict(lo=lo, hi=lo + max(s.Misc_VirtualSize, s.SizeOfRawData),
                        vend=lo + s.Misc_VirtualSize, raw=s.get_data())
    return pe, base, secs


def lib_ranges():
    """sorted [(lo, hi, unitname)] of library .text sections from libraries.json (false positives removed)"""
    j = json.load(open(LIBS))
    out = []
    for u in j['units']:
        for a, s in u['sections'].items():
            if a in LIB_FALSE_POSITIVES:
                continue
            lo = int(a, 16)
            out.append((lo, lo + s[1], u['name']))
    out.sort()
    return out


def load_symbols():
    rows = []
    with open(SYMS, newline='', encoding='utf-8') as f:
        for r in csv.DictReader(f):
            rows.append(dict(addr=int(r['addr'], 16), end=int(r['end'], 16), kind=r['kind'], name=r['name'],
                             extra=r['extra']))
    return rows


def main():
    os.makedirs(OUT, exist_ok=True)
    pe, base, secs = load_image()
    text = secs['.text']
    rd, da = secs['.rdata'], secs['.data']
    # .data spans initialised part (raw) and .bss tail (virtual size beyond raw); keep both views
    data_raw_end = da['lo'] + len(da['raw'])
    iat_lo = base + pe.OPTIONAL_HEADER.DATA_DIRECTORY[12].VirtualAddress
    iat_hi = iat_lo + pe.OPTIONAL_HEADER.DATA_DIRECTORY[12].Size
    libs = lib_ranges()
    rows = load_symbols()
    funcs = sorted([r for r in rows if r['kind'] == 'func'], key=lambda r: r['addr'])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    X = capstone.x86
    out = []
    for f in funcs:
        a, e = f['addr'], f['end']
        code = text['raw'][a - text['lo']:e - text['lo']]
        calls, tcalls, icalls, trefs = set(), set(), set(), set()
        rrefs, drefs, brefs = set(), set(), set()
        ninsn = 0
        nfp = 0
        ncom = 0       # call [reg+disp]: COM / C++ virtual calls
        nswitch = 0    # jmp [reg*4+table]: switch jump tables
        nx87cmp = 0    # fcom/fcomp/fucom/ftst + fnstsw: x87 compares
        for ins in md.disasm(code, a):
            ninsn += 1
            if ins.id in (X.X86_INS_CALL, X.X86_INS_JMP):
                op = ins.operands[0] if ins.operands else None
                if op is not None and op.type == X.X86_OP_IMM:
                    t = op.imm & 0xffffffff
                    if t < a or t >= e:
                        calls.add(t)
                        if ins.id == X.X86_INS_JMP:
                            tcalls.add(t)
                    continue
                if op is not None and op.type == X.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                    d = op.mem.disp & 0xffffffff
                    if iat_lo <= d < iat_hi:
                        icalls.add(d)
                        continue
                if op is not None and op.type == X.X86_OP_MEM and op.mem.base != 0 and ins.id == X.X86_INS_CALL:
                    ncom += 1
                if op is not None and op.type == X.X86_OP_MEM and op.mem.index != 0 and ins.id == X.X86_INS_JMP:
                    nswitch += 1
            if ins.mnemonic.startswith('f'):
                nfp += 1
                if ins.mnemonic in ('fcom', 'fcomp', 'fcompp', 'fucom', 'fucomp', 'fucompp', 'ftst', 'fcomi', 'fcomip'):
                    nx87cmp += 1
            for op in ins.operands:
                v = None
                if op.type == X.X86_OP_IMM:
                    v = op.imm & 0xffffffff
                elif op.type == X.X86_OP_MEM:
                    d = op.mem.disp & 0xffffffff
                    if op.mem.base == 0 or d > 0x10000000:
                        v = d
                if v is None:
                    continue
                if rd['lo'] <= v < rd['hi']:
                    if iat_lo <= v < iat_hi:
                        icalls.add(v)
                    else:
                        rrefs.add(v)
                elif da['lo'] <= v < da['hi']:
                    (drefs if v < data_raw_end else brefs).add(v)
                elif text['lo'] <= v < text['hi'] and not (a <= v < e):
                    trefs.add(v)
        out.append(dict(addr=a, end=e, name=f['name'], ninsn=ninsn, nfp=nfp, ncom=ncom, nswitch=nswitch, nx87cmp=nx87cmp,
                        calls=sorted(calls), tcalls=sorted(tcalls), icalls=sorted(icalls),
                        rrefs=sorted(rrefs), drefs=sorted(drefs), brefs=sorted(brefs), trefs=sorted(trefs)))
    meta = dict(base=base, text=[text['lo'], text['hi']], rdata=[rd['lo'], rd['hi']],
                data=[da['lo'], da['hi']], data_raw_end=data_raw_end, iat=[iat_lo, iat_hi],
                libs=libs)
    json.dump(dict(meta=meta, funcs=out), open(os.path.join(OUT, 'scan.json'), 'w'))
    print('scanned %d functions -> %s' % (len(out), os.path.join(OUT, 'scan.json')))


if __name__ == '__main__':
    main()
