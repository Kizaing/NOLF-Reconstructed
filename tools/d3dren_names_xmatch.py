r"""Cross-binary byte match: d3d.ren functions vs lithtech.exe functions (relocation-masked), read-only.

  python tools/d3dren_names_xmatch.py [--min N]      # writes the cache and prints a summary

For every d3d.ren function (size >= N, default 12) compute a hash of its bytes with absolute-address immediates /
displacements inside the image and rel32 call/jmp displacements masked, and compare with every lithtech.exe
function the same way.  Unique 1:1 matches carry the lithtech.exe name (build/namemap.json, Jupiter map names).
"""
import bisect
import csv
import hashlib
import json
import os
import pickle
import sys

import capstone
import pefile

ROOT = r'E:\AVP2Source\decomp_d3dren'
import tempfile  # noqa: E402
CACHE = os.path.join(tempfile.gettempdir(), 'xmatch.pkl')


def load_funcs(symcsv):
    out = {}
    for r in csv.DictReader(open(symcsv, encoding='utf-8')):
        if r['kind'] == 'func':
            out[int(r['addr'], 16)] = (int(r['end'], 16), r['name'])
    return out


def masked_hash(pe, base, lo, hi, fa, fe, md):
    code = pe.get_data(fa - base, fe - fa)
    buf = bytearray(code)
    ninsn = 0
    mn = []
    for i in md.disasm(code, fa):
        ninsn += 1
        off = i.address - fa
        mn.append(i.mnemonic)
        if i.mnemonic in ('call', 'jmp') and len(i.bytes) == 5 and i.bytes[0] in (0xe8, 0xe9):
            for k in range(1, 5):
                buf[off + k] = 0
            continue
        if i.disp_size == 4:
            d = int.from_bytes(i.bytes[i.disp_offset:i.disp_offset + 4], 'little')
            if lo <= d < hi:
                for k in range(4):
                    buf[off + i.disp_offset + k] = 0
        if i.imm_size == 4:
            d = int.from_bytes(i.bytes[i.imm_offset:i.imm_offset + 4], 'little')
            if lo <= d < hi:
                for k in range(4):
                    buf[off + i.imm_offset + k] = 0
        if i.mnemonic.startswith('j') and len(i.bytes) == 6 and i.bytes[0] == 0x0f:
            for k in range(2, 6):
                buf[off + k] = 0
    return hashlib.md5(bytes(buf)).hexdigest(), ninsn


def build():
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    res = {}
    for tag, img, sym in (('ren', r'E:\AVP2Source\bin\talon\d3d.ren', os.path.join(ROOT, 'config', 'd3dren', 'symbols.csv')),
                          ('exe', r'E:\AVP2Source\bin\lithtech.exe', os.path.join(ROOT, 'config', 'symbols.csv'))):
        pe = pefile.PE(img, fast_load=True)
        base = pe.OPTIONAL_HEADER.ImageBase
        lo, hi = base, base + pe.OPTIONAL_HEADER.SizeOfImage
        funcs = load_funcs(sym)
        d = {}
        for fa, (fe, fn) in funcs.items():
            try:
                h, n = masked_hash(pe, base, lo, hi, fa, fe, md)
            except Exception:
                continue
            d[fa] = (h, fe - fa, n, fn)
        res[tag] = d
    pickle.dump(res, open(CACHE, 'wb'))
    return res


def load():
    if os.path.exists(CACHE):
        return pickle.load(open(CACHE, 'rb'))
    return build()


def exe_names():
    j = json.load(open(os.path.join(ROOT, 'build', 'namemap.json')))
    return {int(a, 16): n for a, n in j.items()}


if __name__ == '__main__':
    r = build()
    ren, exe = r['ren'], r['exe']
    byhash = {}
    for a, (h, sz, n, fn) in exe.items():
        byhash.setdefault(h, []).append(a)
    m1 = 0
    for a, (h, sz, n, fn) in sorted(ren.items()):
        if h in byhash:
            m1 += 1
    print('ren funcs', len(ren), 'exe funcs', len(exe), 'ren funcs with any exe hash match', m1)
