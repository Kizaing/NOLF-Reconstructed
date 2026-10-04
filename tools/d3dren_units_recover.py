r"""d3d.ren unit recovery, step 3: build the unit map.

  python tools/d3dren_units_scan.py        # once (writes build/d3dren/units/scan.json)
  python tools/d3dren_units_recover.py     # writes config/d3dren/units_proposal.csv, units.csv, units_proposal.md
                                           #        build/d3dren/units/units.json  (consumed by d3dren_units_wave.py)

What it does (adapted from the lithtech module's units_recover.py; there are no function names yet, so the
evidence is structural):

  1. Non-library renderer functions: config/d3dren/libraries.json library ranges are excluded (the two false
     `_srand`/`_localeconv` placements are ignored, see d3dren_units_scan.py).
  2. Hard boundaries from section alignment/padding (d3dren_units_regions.py): 13 aligned /Gy-style runs (A) and
     12 packed regions (P) alternate.  A P region is one object, an A run is one or more objects.
  3. Library objects found by byte match that libraries.json does not know: StdLith struct_bank / l_allocator /
     dynarray at the end of the renderer region (verified here against libs_objs/LT_StdLith).
  4. Import-library thunks (DDRAW, KERNEL32 RtlUnwind) become `imp/` units (not source).
  5. Per-unit measurements (functions, bytes, C++ markers, x87, COM calls, strings, ConVars) and a list of
     candidate internal cut points for the large A runs (unapplied, low confidence).
  6. Names: `sys/d3d/<jupiter base>` where a distinctive Jupiter file match exists (see NAMED), else `unk/<hex>`.
"""
import bisect
import collections
import csv
import json
import os
import re
import struct
import sys

import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import d3dren_units_regions as R  # noqa: E402

REPO = os.path.dirname(HERE)
CFG = os.path.join(REPO, 'config', 'd3dren')
OUT = os.path.join(REPO, 'build', 'd3dren', 'units')
LIBS = r'E:\AVP2Source\libs_objs'

NEW_ADDR, DELETE_ADDR = 0x1003c824, 0x1003b710
CONVAR_INIT = 0x100111b5          # ConVar::ConVar(name, default, ...) (see tools/d3dren_names_*)
A_MEDIUM_MAX = 0x2800             # an A run up to 10 KB is plausibly one object (medium)
RHO_SINGLE = 0.20                 # bigger A runs are medium too when the private .bss variables are in hash order (|rho| <= this, >= RHO_MIN_N vars)
RHO_MIN_N = 15

# Distinctive Jupiter-file matches for region starts: (name, how good the match is, why)
NAMED = {
    0x10038830: ('sys/d3d/tagnodes', 'strong', 'VS_* list names (VS_MODELS, VS_WORLDMODELS_TRANSLUCENT, ...) and "GetVisBSP returned LTNULL" '
                 'are in Jupiter tagnodes.cpp (VisibleSet); the region ends with CMoArray template copies of the objects it uses'),
    0x1001e5a0: ('sys/d3d/d3d_texture', 'medium', 'S3TCEnable, FORMAT_NORMAL/INTERFACE/4444/FULLBRITE texture format messages, r_TransferTexture '
                 'texture upload strings; Jupiter d3d_texture.cpp has S3TCEnable and the texture-format code'),
    0x1001bf10: ('sys/d3d/d3d_surface', 'medium', 'ScreenShot: ... strings (4), LockOnFlip and "drawing a nonoptimized surface while in 3D mode" '
                 'are in Jupiter d3d_surface.cpp (the last also in d3d_optimizedsurface.cpp)'),
    0x10019b10: ('sys/d3d/d3d_init', 'medium', 'LISTDEVICECAPS / LISTTEXTUREFORMATS / LISTDEVICES console commands and the device/caps dump '
                 'are in Jupiter d3d_init.cpp; ForceMode and the DDERR/D3DERR text tables belong to the same device-creation code '
                 '(Jupiter d3d_device.cpp has ForceMode), so this run may also hold a d3d_device object'),
    0x1002afd0: ('sys/d3d/drawpolygrid', 'weak', 'EnvMapPolyGrids console variable (Jupiter drawpolygrid.cpp); 14 functions, x87 heavy'),
    0x1000f3a0: ('sys/d3d/common_stuff', 'medium', 'dalloc/dalloc_z/dfree + "d3drender.dll: out of memory" + g_pStruct/ConsolePrint wrappers + '
                 'console-variable creation = Jupiter common_stuff.cpp (the seed unit src/d3dren/seed/common_stuff.cpp); the same object '
                 'also defines ~100 ConVars (rendererconsolevars), ConVar::ConVar (0x100111b5), RenderDLLSetup/GetSupportedModes/FreeModeList '
                 'and the init code with the "VisibleSet::Init failed" strings (Jupiter common_init.cpp / d3d_shell.cpp)'),
}

# one-line topic hints for the unnamed regions (evidence: strings, ConVar names, call neighbourhood)
HINTS = {
    0x10001000: 'model vertex-buffer pools (4 global pool objects with vtables, ConVars ModelMinTri..ModelVBCacheDelay), TL vertex fillers '
                '(0x10001370-0x10001520), 3D polygon clipper in 8 plane variants (0x10001530-0x10007670, Jupiter polyclip.h), '
                'view/vertex transform loops (0x10002050, 0x10002bc0, 0x100036d0), giant x87 model transform 0x10004660 (872 x87 insns)',
    0x10007930: 'node pool + vertex-buffer flush ("Error: vertex buffer overflow"), clip-plane edge helpers (0x10008b58..0x10008c6e)',
    0x10008cd0: 'one static initialiser then x87 draw/transform code (0x10009020, 0x10009370: 192/222 x87 insns)',
    0x100098d0: 'model/world ConVars (DetailTextures.., LightModelSprites..ModelUseTnL), Direct3D7 state wrappers (0x10009ea5-0x1000a521), '
                'model draw (0x1000a538..0x1000d3a7), out-of-line LTMatrix/LTVector inlines, a family of ~30 pixel-format converter stubs '
                '(0x1000e0c1-0x1000f148) with 3 switch tables',
    0x1000f160: 'NearZ/ReallyCloseNearZ ConVars + two projection/viewport helpers (Jupiter 3d_ops.cpp d3d_SetReallyClose family)',
    0x100132a0: 'ConVars VFogMinY..RenderToFront, world draw (DrawWorldTree), texture stats ("ShowTexInfo"/"Overdraw:"/"Triangles:" lines in '
                '0x10017aa0), x87 world/light loops (0x10015820, 0x100161e0), list/pool helpers (0x100184f0-0x10019220); Jupiter d3d_draw.cpp',
    0x10019350: 'two empty static initialisers, hash/list helpers around ConVar-cached values',
    0x10021d70: 'DrawCanvases / MultipassGouraud ConVars, vertex-buffer "Set overflowed" helper, canvas/state-block drawing '
                '(Jupiter draw_canvas.cpp neighbourhood)',
    0x10023860: 'x87 draw helpers (0x100238b0, 0x10023b20, 0x10023d80: 181 x87 insns), no strings',
    0x100241e0: 'ModelSpecular/ModelTexture/ModelShadow* ConVars and model shadow projection ("Invalid texture size for shadows"); '
                'Jupiter drawmodel*/modelshadow neighbourhood',
    0x100285a0: 'SortProfile/DrawSorted ConVars, CMoArray<> template copies (vtables 0x100463ec/0x1004641c)',
    0x10029660: 'DrawPolyMgr (PSSrcBlend/PSDestBlend, TestGouraud/TestLightmap, "DrawPolyMgr::DrawPolyAdditionalPass: nQVerts != nVertices", '
                '"Material ..., pass %d, stage %d: TextureSrcInitFn failed")',
    0x1002d000: 'portal ConVars only (PortalFlow, PortalGraph, PortalsOnly, BlockersOnly)',
    0x1002d080: 'sky/portal/world-model draw ("AllSkyPortals", "DrawWorldModels"), x87 portal clipping, ConVars; Jupiter drawsky/drawworldmodel',
    0x10030bb0: 'DirectDrawEnumerateExA fallback (GetProcAddress) + dynamic lightmap ConVars (LMDynamic/LMDynamicScale/LMDynamicSize)',
    0x100329b0: 'LMAnimStatic ConVar + lightmap animation/light-table x87 code',
    0x10034000: 'lightmap pages ("Unable to create (%dx%d) lightmap page.", "Lightmaps paged in %.1f seconds.", "LightAnim_BASE"), '
                'lightmap build/upload, 119 functions',
    0x1003a680: 'vertex-buffer pool base class (constructor 0x1003a680, destructor 0x1003a6d6, virtuals 0x1003a6fb..0x1003a8b1: the 4 pool '
                'objects of unk/10001000 derive from it and override two slots) plus CMoArray-style template member copies (vtables '
                '0x100463ec/0x1004641c/0x100464ec), list helpers using g_DefAlloc (0x10093b84) and new/delete',
}

LIBRARY_OBJS = [   # (unit name, obj path, base name)
    ('lib/LT_StdLith/struct_bank', LIBS + r'\LT_StdLith\struct_bank.obj'),
    ('lib/LT_StdLith/l_allocator', LIBS + r'\LT_StdLith\l_allocator.obj'),
    ('lib/LT_StdLith/dynarray', LIBS + r'\LT_StdLith\dynarray.obj'),
]


def hexs(v):
    return '%08x' % v


# ---------------------------------------------------------------------------------------------------- library objects
def match_library_objects(tb, tlo, lo, hi):
    """Byte-match the code sections of libs_objs/LT_StdLith/{struct_bank,l_allocator,dynarray}.obj inside [lo,hi)
    (relocation fields wildcarded).  -> {unit name: [(symbol, va, nbytes)]}"""
    sys.path.insert(0, HERE)
    from coffobj import CoffObj
    region = tb[lo - tlo:hi - tlo]
    out = {}
    for name, path in LIBRARY_OBJS:
        if not os.path.exists(path):
            continue
        o = CoffObj(path)
        hits = []
        for s in o.sections:
            if not s.data or 'text' not in s.name or s.name.startswith('.text$x'):
                continue
            d = bytearray(s.data)
            mask = [1] * len(d)
            for r in s.relocs:
                off = r[0] if isinstance(r, tuple) else r.offset
                for k in range(4):
                    if off + k < len(mask):
                        mask[off + k] = 0
            n = len(d)
            while n > 0 and d[n - 1] in (0x90, 0xcc):
                n -= 1
            if n < 12:
                continue
            pat = b''.join((re.escape(bytes([d[i]])) if mask[i] else b'.') for i in range(n))
            m = [x.start() for x in re.finditer(pat, region, re.S)]
            if len(m) == 1:
                hits.append((s.syms[0].name if s.syms else '?', lo + m[0], n, len(s.data)))
        out[name] = hits
    return out


# ---------------------------------------------------------------------------------------------------- measurements
class Image:
    def __init__(self):
        pe = pefile.PE(R.IMAGE)
        base = pe.OPTIONAL_HEADER.ImageBase
        self.sec = {s.Name.rstrip(b'\0').decode(): (base + s.VirtualAddress, s.get_data()) for s in pe.sections}
        self.dlo, self.dd = self.sec['.data']
        self.rlo, self.rd = self.sec['.rdata']
        xcu = [struct.unpack_from('<I', self.dd, o)[0] for o in range(4, 0x368, 4)]
        self.xcu = [x for x in xcu if x]
        self.fstarts = None

    def cstr(self, v, minlen=3):
        o = v - self.dlo
        if o < 0 or o >= len(self.dd):
            return None
        e = self.dd.find(b'\0', o)
        s = self.dd[o:e]
        if len(s) >= minlen and all(32 <= c < 127 for c in s):
            return s.decode()
        return None

    def rword(self, v):
        return struct.unpack_from('<I', self.rd, v - self.rlo)[0]


def measure(funcs_all, fs, lo, hi, img, fstarts):
    xcu = [a for a in img.xcu if lo <= a < hi]
    vts = set()
    jts = set()
    strs = []
    seen = set()
    for f in fs:
        for v in f['rrefs']:
            if v < 0x10046666:
                w = img.rword(v)
                if 0x10001000 <= w < 0x10046000:
                    w2 = img.rword(v + 4) if v + 4 < 0x10046666 else 0
                    (vts if (w in fstarts and w2 in fstarts) else jts).add(v)
        for v in f['drefs']:
            if 0x100483a0 <= v < 0x1004c250 and v not in seen:
                s = img.cstr(v)
                if s:
                    seen.add(v)
                    strs.append(s)
    convars = []
    for f in fs:
        if CONVAR_INIT in f['calls'] and f['end'] - f['addr'] <= 0x30:
            for v in f['drefs']:
                s = img.cstr(v)
                if s:
                    convars.append(s)
    nnd = sum(1 for f in fs for c in f['calls'] if c in (NEW_ADDR, DELETE_ADDR))
    trivial = sum(1 for f in fs if f['end'] - f['addr'] <= 0x20 and not f['nfp'])
    return dict(
        nf=len(fs), bytes=hi - lo, xcu=len(xcu), vtables=len(vts), jumptables=len(jts), newdel=nnd,
        fpfuncs=sum(1 for f in fs if f['nfp']), x87insns=sum(f['nfp'] for f in fs),
        x87cmpfuncs=sum(1 for f in fs if f['nx87cmp']), com=sum(f['ncom'] for f in fs),
        comfuncs=sum(1 for f in fs if f['ncom']), switches=sum(f['nswitch'] for f in fs),
        strings=len(strs), strsample=strs[:4], convars=len(convars), convarsample=convars[:3],
        big=sum(1 for f in fs if f['end'] - f['addr'] >= 0x400), trivial=trivial,
        biggest=max((f['end'] - f['addr'], f['addr']) for f in fs) if fs else (0, 0))


def lang_of(m, kind):
    c = []
    if m['xcu']:
        c.append('static init x%d' % m['xcu'])
    if m['vtables']:
        c.append('vtables x%d' % m['vtables'])
    if m['newdel']:
        c.append('new/delete x%d' % m['newdel'])
    if c:
        return 'C++ (' + ', '.join(c) + ')'
    return 'C? (no C++ marker: no static init, vtable, new/delete)'


def bss_users(funcs):
    users = collections.defaultdict(list)
    for i, f in enumerate(funcs):
        for v in f['brefs'] + f['drefs']:
            if 0x1004d5a0 <= v < 0x10093b00:
                users[v].append(i)
    return users


def bss_rho(users, first, last):
    """Spearman rho between the address order of the PRIVATE .bss variables (<= 6 users, all inside funcs[first..last])
    and the order of their first-user function, and the number of variables.  Extern globals of ONE C++ object are laid
    out in name-hash order (rho ~ 0); k objects with separate .bss sections give rho > 0 (variables follow link order).
    File-statics are in definition order (rho > 0 even for one object), so rho ~ 0 supports a single object, rho > 0
    proves nothing."""
    pv = sorted((v, min(u)) for v, u in users.items() if len(u) <= 6 and all(first <= x <= last for x in u))
    if len(pv) < 8:
        return None, len(pv)
    xs = [a for a, _ in pv]
    ys = [p for _, p in pv]

    def ranks(v):
        r = [0] * len(v)
        for k, i in enumerate(sorted(range(len(v)), key=lambda t: v[t])):
            r[i] = k
        return r
    rx, ry = ranks(xs), ranks(ys)
    n = len(xs)
    mx, my = sum(rx) / n, sum(ry) / n
    num = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    den = (sum((a - mx) ** 2 for a in rx) * sum((b - my) ** 2 for b in ry)) ** 0.5
    return (num / den if den else 0.0), n


# ---------------------------------------------------------------------------------------------------- candidate cuts
def candidate_cuts(funcs, first, last, fs_edges, topn=5):
    """windowed call/shared-var coupling minima inside an A run: positions where a /Gy object could start.
    Heuristic only (validated: at the 12 hard A/P boundaries the same score ranks in the lowest quintile in only ~60%
    of cases), so these are reported, never applied."""
    n = last - first + 1
    w = 6
    out = []
    for b in range(first + 3, last - 2):
        left = range(max(first, b - w), b)
        right = range(b, min(last + 1, b + w))
        L, Rr = set(left), set(right)
        c = 0
        for (u, v), wt in fs_edges.items():
            if (u in L and v in Rr) or (v in L and u in Rr):
                c += wt
        out.append((c, b))
    out.sort()
    picked = []
    for c, b in out:
        a = funcs[b]['addr']
        if funcs[b - 1]['addr'] and all(abs(a - funcs[p]['addr']) >= 0x600 for _, p in picked):
            if a - funcs[first]['addr'] >= 0x400 and funcs[last]['end'] - a >= 0x400:
                picked.append((c, b))
        if len(picked) >= topn:
            break
    return sorted(picked, key=lambda x: x[1])


def build_edges(funcs):
    idx = {f['addr']: i for i, f in enumerate(funcs)}
    E = collections.defaultdict(float)
    users = collections.defaultdict(set)
    for i, f in enumerate(funcs):
        for c in f['calls']:
            j = idx.get(c)
            if j is not None and j != i:
                E[(min(i, j), max(i, j))] += 1.0
        for v in f['drefs'] + f['brefs'] + [x for x in f['rrefs'] if x < 0x10046666]:
            users[v].add(i)
    for v, u in users.items():
        u = sorted(u)
        if 2 <= len(u) <= 6:
            for a, b in zip(u, u[1:]):
                E[(a, b)] += 1.0
    return E


def layout_facts(funcs, img, regs):
    """numbers quoted in units_proposal.md"""
    ren_x = [x for x in img.xcu if 0x10001000 <= x < 0x1003b710]
    first = {}
    for f in funcs:
        for v in f['drefs']:
            if 0x100483a0 <= v < 0x1004c250 and img.cstr(v):
                first.setdefault(v, f['addr'])
    items = sorted(first.items())
    str_inv = sum(1 for (a, fa), (b, fb) in zip(items, items[1:]) if fb < fa)
    fr = {}
    for f in funcs:
        for v in f['rrefs']:
            if v < 0x100465bc:
                fr.setdefault(v, f['addr'])
    it = sorted(fr.items())
    r_inv = sum(1 for (a, fa), (b, fb) in zip(it, it[1:]) if fb < fa)
    xb = 0
    nb = 0
    for r in regs:
        if r['kind'] != 'P':
            continue
        for i in range(r['first'], r['last']):
            nb += 1
            if funcs[i]['end'] % 16 == 0:
                xb += 1
    return dict(xcu=len(ren_x), xcu_ascending=all(a < b for a, b in zip(ren_x, ren_x[1:])), strings=len(items), str_inv=str_inv,
                rdata=len(it), rdata_inv=r_inv, p_aligned_ends=xb, p_boundaries=nb, p_expected=nb / 16.0)


# ---------------------------------------------------------------------------------------------------- main
def main():
    os.makedirs(OUT, exist_ok=True)
    scan = R.load_scan()
    tb, tlo = R.text_bytes()
    funcs = R.renderer_functions(scan)
    all_funcs = scan['funcs']
    fstarts = set(f['addr'] for f in all_funcs)
    thunks = [(a['end'], b['addr']) for a, b in zip(funcs, funcs[1:]) if b['addr'] > a['end']]
    regs = R.compute_regions(funcs, tb, tlo, thunks)
    img = Image()
    meta = scan['meta']
    libs = [tuple(x) for x in meta['libs']]
    text_lo = meta['text'][0]
    pe = pefile.PE(R.IMAGE, fast_load=True)
    tsec = [x for x in pe.sections if x.Name.startswith(b'.text')][0]
    text_hi = pe.OPTIONAL_HEADER.ImageBase + tsec.VirtualAddress + tsec.Misc_VirtualSize     # 0x10045890

    BSSU = bss_users(funcs)
    facts = layout_facts(funcs, img, regs)
    units = []

    # --- library objects at the end of the renderer region (the last A run)
    last = regs[-1]
    lib_hits = match_library_objects(tb, tlo, last['lo'], last['hi'])
    lib_units = []
    order = sorted(((min(h[1] for h in hits), name, hits) for name, hits in lib_hits.items() if hits))
    prev_hi = None
    for k, (lo0, name, hits) in enumerate(order):
        # an object starts where the previous one's last matched section (padded to its section length) ends
        lo = lo0 if prev_hi is None else prev_hi
        hi = max(h[1] + h[3] for h in hits)
        if k + 1 == len(order):
            hi = last['hi']
        lib_units.append(dict(name=name, lo=lo, hi=hi, hits=hits))
        prev_hi = hi
    lib_lo = lib_units[0]['lo'] if lib_units else last['hi']
    if last['lo'] < lib_lo or True:
        # anything in the last run before the first library object would be a renderer unit; there is none
        assert lib_lo == last['lo'], 'unexpected: renderer code before the StdLith objects'
    regs = regs[:-1]

    # --- renderer regions
    for i, r in enumerate(regs):
        fs = funcs[r['first']:r['last'] + 1]
        m = measure(all_funcs, fs, r['lo'], r['hi'], img, fstarts)
        size = r['hi'] - r['lo']
        named = NAMED.get(r['lo'])
        name = named[0] if named else 'unk/%08x' % r['lo']
        rho, nrho = bss_rho(BSSU, r['first'], r['last'])
        m['rho'], m['rho_n'] = rho, nrho
        if r['kind'] == 'P':
            conf = 'high'
            kind_txt = 'P region (packed .text, no /Gy; one object): extent exact; flags /O1 /Ob2 (seed evidence)'
        else:
            single = rho is not None and abs(rho) <= RHO_SINGLE and nrho >= RHO_MIN_N
            if size <= A_MEDIUM_MAX or single:
                conf = 'medium'
            else:
                conf = 'low'
            kind_txt = ('A run (16-aligned COMDAT functions, 0x90 pad = /O2 /Gy): extent exact, one or more objects%s'
                        % ('; bss hash order (rho %+.2f over %d private vars) supports ONE object' % (rho, nrho) if single and size > A_MEDIUM_MAX
                           else ('' if conf == 'medium' else '; %d KB, probably several' % (size // 1024))))
        ev = [kind_txt, '%d fn, %d bytes' % (m['nf'], size), lang_of(m, r['kind'])]
        if rho is not None:
            ev.append('bss order rho=%+.2f (n=%d)' % (rho, nrho))
        if m['strsample']:
            ev.append('strings: %d (%s)' % (m['strings'], '; '.join(s[:24] for s in m['strsample'][:3])))
        if m['convars']:
            ev.append('ConVars: %d (%s..)' % (m['convars'], ','.join(m['convarsample'])))
        if r['lo'] in HINTS:
            ev.append('content: ' + HINTS[r['lo']])
        if named:
            ev.append('name (%s): %s' % (named[1], named[2]))
        units.append(dict(unit=name, lo=r['lo'], hi=r['hi'], conf=conf, kind=r['kind'], first=r['first'], last=r['last'],
                          starts_init=any(r['lo'] <= x < r['lo'] + 0x40 for x in img.xcu),
                          m=m, evidence=ev, lang=lang_of(m, r['kind']), src=True,
                          flags='/O2 /Ob2' if r['kind'] == 'A' else '/O1 /Ob2'))

    # --- library units
    for lu in lib_units:
        names = ', '.join(sorted(set(h[0].split('@')[0].lstrip('?') for h in lu['hits'])))
        fs = [f for f in funcs_in(all_funcs, lu['lo'], lu['hi'])]
        units.append(dict(unit=lu['name'], lo=lu['lo'], hi=lu['hi'], conf='high', kind='L', first=None, last=None,
                          m=dict(nf=len(fs), bytes=lu['hi'] - lu['lo']), src=False, lang='C/C++ (prebuilt library object)',
                          flags='(prebuilt)',
                          evidence=['PREBUILT LIBRARY OBJECT, not renderer source: %d code sections byte-identical (outside relocations) to %s'
                                    % (len(lu['hits']), os.path.relpath(dict(LIBRARY_OBJS)[lu['name']], LIBS)),
                                    'matched: ' + names,
                                    'unreferenced sections of the object were dropped by /OPT:REF; libraries.json does not list it (libmatch skipped '
                                    'lithshared libs): add it so the build treats it as library code']))
    # --- import thunks
    for lo, hi in thunks:
        units.append(dict(unit='imp/ddraw_thunks', lo=lo, hi=hi, conf='high', kind='I', first=None, last=None,
                          m=dict(nf=0, bytes=hi - lo), src=False, lang='n/a', flags='(import library)',
                          evidence=['IMPORT-LIBRARY THUNKS, not source: 3 x `jmp [iat]` for DirectDrawCreate, DirectDrawCreateEx, DirectDrawEnumerateA '
                                    '(symbols.csv labels at 0x1003b502/08/0e) plus alignment padding']))
    # KERNEL32 RtlUnwind thunk after the last library section
    units.append(dict(unit='imp/kernel32_rtlunwind', lo=0x10045880, hi=text_hi, conf='high', kind='I', first=None, last=None,
                      m=dict(nf=0, bytes=text_hi - 0x10045880), src=False, lang='n/a', flags='(import library)',
                      evidence=['IMPORT-LIBRARY THUNK, not source: `jmp [RtlUnwind]` (symbols.csv label 0x10045880, called by the CRT exception code) to the end of .text']))
    units.sort(key=lambda u: u['lo'])

    # --- check the partition: every non-library .text byte is in exactly one unit
    covered = []
    for u in units:
        covered.append((u['lo'], u['hi'], u['unit']))
    covered.sort()
    for (a0, a1, an), (b0, b1, bn) in zip(covered, covered[1:]):
        assert a1 <= b0, 'overlap %s %s' % (an, bn)
    libr = sorted(libs)
    # library ranges (CRT) plus pad gaps in the CRT region
    crt_lo = libr[0][0]
    unit_bytes = sum(u['hi'] - u['lo'] for u in units)
    lib_bytes = sum(hi - lo for lo, hi, _ in libr)
    # uncovered = .text bytes in no unit and in no library range
    uncovered = []
    cur = text_lo
    bounds = sorted([(u['lo'], u['hi']) for u in units] + [(lo, hi) for lo, hi, _ in libr])
    for lo, hi in bounds:
        if lo > cur:
            uncovered.append((cur, lo))
        cur = max(cur, hi)
    if cur < text_hi:
        uncovered.append((cur, text_hi))
    uncovered_pad = []
    for lo, hi in uncovered:
        d = tb[lo - tlo:hi - tlo]
        uncovered_pad.append((lo, hi, set(d) <= {0x90, 0xcc, 0x00}))

    # --- candidate cuts inside A runs (never applied)
    #   * interior static-initialiser blocks: 8 of the 12 packed objects and 10 of the 13 A runs start with their static
    #     initialisers (globals/ConVars are defined first), so an XCU block far from the start of an A run is a
    #     candidate object start (the second one is probably a mid-file definition about 30% of the time);
    #   * call/shared-data coupling minima (weak: lowest quintile for ~58% of the hard boundaries).
    E = build_edges(funcs)
    cands = {}
    for u in units:
        if u.get('kind') != 'A':
            continue
        xs = sorted(a for a in img.xcu if u['lo'] <= a < u['hi'])
        blocks = []
        for a in xs:
            if blocks and a - blocks[-1][1] <= 0x100:
                blocks[-1][1] = a
                blocks[-1][2] += 1
            else:
                blocks.append([a, a, 1])
        # the first block is the one at the top of the object (not a candidate) when it is within 0x400 of the start
        if blocks and blocks[0][0] - u['lo'] <= 0x400:
            blocks = blocks[1:]
        u['init_blocks'] = [(b[0], b[2]) for b in blocks]
        if u['conf'] == 'low':
            cs = candidate_cuts(funcs, u['first'], u['last'], E)
            u['cuts'] = [(c, funcs[b]['addr']) for c, b in cs]
            cands[u['unit']] = u['cuts']

    # --- write outputs
    with open(os.path.join(CFG, 'units_proposal.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(['unit', 'start', 'end', 'confidence', 'evidence'])
        for u in units:
            w.writerow([u['unit'], hexs(u['lo']), hexs(u['hi']), u['conf'], ' | '.join(u['evidence'])])
    with open(os.path.join(CFG, 'units.csv'), 'w', newline='', encoding='utf-8') as f:
        f.write('unit,start,end,confidence\n')
        for u in units:
            f.write('%s,%s,%s,%s\n' % (u['unit'], hexs(u['lo']), hexs(u['hi']), u['conf']))
    json.dump(dict(units=[{k: v for k, v in u.items() if k not in ('evidence',)} | {'evidence': u['evidence']} for u in units],
                   uncovered=uncovered_pad, lib_bytes=lib_bytes, unit_bytes=unit_bytes, cands=cands, facts=facts,
                   text=[text_lo, text_hi]),
              open(os.path.join(OUT, 'units.json'), 'w'), default=list)
    print('units: %d  (renderer source units %d)  unit bytes 0x%x  library bytes 0x%x' %
          (len(units), sum(1 for u in units if u['src']), unit_bytes, lib_bytes))
    by = collections.Counter()
    for u in units:
        by[u['conf']] += u['hi'] - u['lo']
    print('bytes by confidence:', dict(by))
    print('uncovered .text ranges (CRT padding between library sections): n=%d, bytes=%d, non-padding ranges: %s' %
          (len(uncovered_pad), sum(b - a for a, b, _ in uncovered_pad), [('%x-%x' % (a, b)) for a, b, p in uncovered_pad if not p]))
    return units


def funcs_in(all_funcs, lo, hi):
    return [f for f in all_funcs if lo <= f['addr'] < hi]


if __name__ == '__main__':
    main()
