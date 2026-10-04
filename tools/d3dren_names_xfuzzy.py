r"""Fuzzy cross-binary match: d3d.ren vs lithtech.exe by mnemonic sequence + size (read-only).
   python tools/d3dren_names_xfuzzy.py [minbytes]
Prints unique 1:1 pairs whose mnemonic sequences are identical (operands/registers may differ), excluding CRT and exact matches."""
import hashlib, json, os, sys, pickle
import capstone, pefile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_xmatch as X
import d3dren_names_scan as S

def msigs(img, symcsv):
    pe = pefile.PE(img, fast_load=True); base = pe.OPTIONAL_HEADER.ImageBase
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    funcs = X.load_funcs(symcsv); out = {}
    for fa, (fe, fn) in funcs.items():
        try: code = pe.get_data(fa - base, fe - fa)
        except Exception: continue
        mn = [i.mnemonic for i in md.disasm(code, fa) if i.mnemonic != 'nop']
        out[fa] = (hashlib.md5(' '.join(mn).encode()).hexdigest(), fe - fa, len(mn), fn)
    return out

if __name__ == '__main__':
    minb = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    ren = msigs(r'E:\AVP2Source\bin\talon\d3d.ren', os.path.join(X.ROOT, 'config', 'd3dren', 'symbols.csv'))
    exe = msigs(r'E:\AVP2Source\bin\lithtech.exe', os.path.join(X.ROOT, 'config', 'symbols.csv'))
    names = X.exe_names()
    lib = json.load(open(os.path.join(X.ROOT, 'config', 'd3dren', 'libraries.json')))
    libf = {int(a, 16) for u in lib['units'] for a in u['functions']}
    byh = {}
    for a, (h, sz, n, fn) in exe.items(): byh.setdefault(h, []).append(a)
    cnt = {}
    for a, (h, sz, n, fn) in ren.items(): cnt.setdefault(h, []).append(a)
    for a, (h, sz, n, fn) in sorted(ren.items()):
        if a in libf or sz < minb or h not in byh: continue
        ms = byh[h]
        if len(ms) != 1 or len(cnt[h]) != 1: continue
        m = ms[0]
        nm = names.get(m, exe[m][3])
        print('%08x %4d  %08x %4d  %s' % (a, sz, m, exe[m][1], nm[:90]))
