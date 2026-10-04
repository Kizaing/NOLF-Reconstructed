r"""Vtable / class recovery for d3d.ren (read-only).

A vtable = address in .rdata (0x10046000-0x100474bc) whose dword is a function entry and which is stored into an
object by `mov [reg+disp], imm32` (or `mov [abs], imm32` for static objects) in some function (a constructor,
destructor or other function that (re)sets the vptr).

  python tools/d3dren_names_vtables.py     # vtable list: addr, #slots, slot targets, functions storing it
"""
import os
import struct
import sys

import capstone
import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_scan as S  # noqa: E402

PURECALL = 0x1003ca0a


def analyse():
    o = S.load()
    funcs = o['funcs']
    pe = pefile.PE(S.IMAGE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    rd = [s for s in o['secs'] if s[0] == '.rdata'][0]
    rd_lo, rd_hi = rd[1], rd[1] + rd[2]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    stores = {}      # vtable va -> list of (func, insn va, dest operand text)
    for fa, (fe, fn) in funcs.items():
        code = pe.get_data(fa - base, fe - fa)
        for i in md.disasm(code, fa):
            if i.mnemonic == 'mov' and i.imm_size == 4 and len(i.operands) == 2 and i.operands[0].type == capstone.x86.X86_OP_MEM:
                v = int.from_bytes(i.bytes[i.imm_offset:i.imm_offset + 4], 'little')
                if rd_lo <= v < rd_hi:
                    d = struct.unpack('<I', pe.get_data(v - base, 4))[0]
                    if d in funcs:
                        stores.setdefault(v, []).append((fa, i.address, i.op_str.split(',')[0]))
    vt = {}
    for v, st in sorted(stores.items()):
        slots = []
        a = v
        while a < rd_hi:
            d = struct.unpack('<I', pe.get_data(a - base, 4))[0]
            if d in funcs:
                slots.append(d)
                a += 4
            else:
                break
            # stop at the next vtable start (another store target)
            if a in stores:
                break
        vt[v] = dict(slots=slots, stores=st)
    return o, vt


if __name__ == '__main__':
    o, vt = analyse()
    for v, d in sorted(vt.items()):
        sl = d['slots']
        print('%08x n=%-2d [%s]' % (v, len(sl), ' '.join('%08x' % s if s != PURECALL else 'PURE' for s in sl)))
        for f, ia, dst in d['stores']:
            print('        %08x @%08x %s' % (f, ia, dst))
