r"""Naming-pass scanner for d3d.ren (read-only).  Builds a cache with strings, per-function data refs, call graph.

  python tools/d3dren_names_scan.py [--cache PATH]     # (re)build the cache (default: scratchpad pickle)

The cache is a pickle with:
  funcs   {addr: (end, name)}                 from config/d3dren/symbols.csv (kind=func)
  strings {addr: text}                        printable runs in .rdata/.data (>= 4 chars, NUL terminated)
  refs    {func: {target_va: [insn_va,...]}}  every absolute immediate / displacement operand that lands in the image
  calls   {func: [target,...]}                direct calls / jumps to other function entries
  callers {func: set(callers)}
  icalls  {func: [(insn_va, mem_va_or_None, disp, base_reg)]}   indirect calls
Other tools import this module (load()).
"""
import bisect
import csv
import os
import pickle
import re
import sys

import capstone
import pefile

IMAGE = r'E:\AVP2Source\bin\talon\d3d.ren'
ROOT = r'E:\AVP2Source\decomp_d3dren'
SYMBOLS = os.path.join(ROOT, 'config', 'd3dren', 'symbols.csv')
import tempfile  # noqa: E402
CACHE = os.path.join(tempfile.gettempdir(), 'd3dren_scan.pkl')


def read_symbols():
    rows = []
    for r in csv.DictReader(open(SYMBOLS, encoding='utf-8')):
        rows.append((int(r['addr'], 16), int(r['end'], 16), r['kind'], r['name'], r['extra']))
    return rows


def build(cache=CACHE):
    pe = pefile.PE(IMAGE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    size = pe.OPTIONAL_HEADER.SizeOfImage
    raw = open(IMAGE, 'rb').read()
    secs = [(s.Name.rstrip(b'\0').decode(), base + s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData)
            for s in pe.sections]
    syms = read_symbols()
    funcs = {a: (e, n) for a, e, k, n, x in syms if k == 'func'}
    # strings
    strings = {}
    for name, va, vs, po, ps in secs:
        if name not in ('.rdata', '.data'):
            continue
        blob = raw[po:po + ps]
        for m in re.finditer(rb'[\x20-\x7e\t\r\n]{4,}\x00', blob):
            strings[va + m.start()] = m.group(0)[:-1].decode('latin1')
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    X86 = capstone.x86
    refs, calls, icalls = {}, {}, {}
    for fa, (fe, fn) in sorted(funcs.items()):
        try:
            code = pe.get_data(fa - base, fe - fa)
        except Exception:
            continue
        r = refs.setdefault(fa, {})
        c = calls.setdefault(fa, [])
        ic = icalls.setdefault(fa, [])
        for i in md.disasm(code, fa):
            for op in i.operands:
                if op.type == X86.X86_OP_IMM:
                    v = op.imm & 0xffffffff
                    if i.mnemonic in ('call', 'jmp') or i.mnemonic.startswith('j'):
                        if v in funcs and v != fa:
                            c.append(v)
                        continue
                    if base <= v < base + size:
                        r.setdefault(v, []).append(i.address)
                elif op.type == X86.X86_OP_MEM:
                    d = op.mem.disp & 0xffffffff
                    if op.mem.base == 0 and op.mem.index == 0 and base <= d < base + size:
                        r.setdefault(d, []).append(i.address)
                        if i.mnemonic in ('call', 'jmp'):
                            ic.append((i.address, d, 0, None))
                    elif op.mem.base != 0 and i.mnemonic in ('call', 'jmp'):
                        ic.append((i.address, None, op.mem.disp, i.reg_name(op.mem.base)))
                    elif op.mem.index != 0 and base <= d < base + size and op.mem.base == 0:
                        r.setdefault(d, []).append(i.address)
                    elif base <= d < base + size and d >= 0x10046000:
                        # base+disp where disp is a data address (array access with register base)
                        r.setdefault(d, []).append(i.address)
    callers = {}
    for f, cl in calls.items():
        for t in cl:
            callers.setdefault(t, set()).add(f)
    obj = dict(funcs=funcs, strings=strings, refs=refs, calls=calls, callers=callers, icalls=icalls, secs=secs,
               syms=syms, base=base)
    pickle.dump(obj, open(cache, 'wb'))
    return obj


def load(cache=CACHE):
    if os.path.exists(cache):
        return pickle.load(open(cache, 'rb'))
    return build(cache)


if __name__ == '__main__':
    c = CACHE
    if '--cache' in sys.argv:
        c = sys.argv[sys.argv.index('--cache') + 1]
    o = build(c)
    print('funcs', len(o['funcs']), 'strings', len(o['strings']), 'refs-funcs', len(o['refs']))
