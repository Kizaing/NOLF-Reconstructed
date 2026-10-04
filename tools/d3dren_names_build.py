r"""Build config/d3dren/names_proposal.csv from the evidence tables (read-only on everything but the output).

  python tools/d3dren_names_build.py [--agents DIR] [--out PATH] [--stats]

Row sources, in priority order (an address named by an earlier source is never overridden by a later one; the
conflict is recorded in the conflict list printed at the end and in NAMING.md):
  1. core      : tables computed here from the binary (CRT library match, exports, RenderStruct slots, console
                 variables, GUIDs, COM globals, static initialisers / atexit destructors)
  2. manual    : tools/d3dren_names_data.py   (hand curated rows; each carries its evidence)
  3. agents    : DIR/agent_*.csv              (region agents, same schema; reviewed by the lead)
"""
import csv
import glob
import json
import os
import re
import struct
import sys
from collections import Counter, OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_convars as CV  # noqa: E402
import d3dren_names_guids as GU  # noqa: E402
import d3dren_names_scan as S  # noqa: E402

ROOT = S.ROOT
OUT = os.path.join(ROOT, 'config', 'd3dren', 'names_proposal.csv')
HDR = ['address', 'name', 'kind', 'source_file', 'evidence', 'confidence', 'provenance']
JUP_CONVARS = r'E:\AVP2Source\jupiter\runtime\render_a\src\sys\d3d\rendererconsolevars.h'


class Rows(OrderedDict):
    """address -> row dict; first writer wins; conflicts remembered."""

    def __init__(self):
        super().__init__()
        self.conflicts = []

    def add(self, addr, name, kind, src, ev, conf, prov, origin='core'):
        a = '%08x' % addr if isinstance(addr, int) else addr.lower()
        r = dict(address=a, name=name, kind=kind, source_file=src, evidence=ev, confidence=conf, provenance=prov,
                 origin=origin)
        if a in self:
            old = self[a]
            if old['name'] != name:
                self.conflicts.append((a, old['origin'], old['name'], origin, name))
            return False
        self[a] = r
        return True


def crt_rows(R, funcs):
    lib = json.load(open(os.path.join(ROOT, 'config', 'd3dren', 'libraries.json')))
    n = 0
    for u in lib['units']:
        unit = u['name']
        for a, nm in sorted(u['functions'].items()):
            va = int(a, 16)
            if va == 0:
                continue
            st = u['sections'].get(a)
            status = st[2] if st else ''
            kind_note = st[3] if st else ''
            inside_renderer = va < 0x1003b710
            conf = 'high'
            ev = 'byte match against LIBCMT object %s (%s)' % (unit, status or 'n/a')
            if nm.startswith('label') or nm.startswith('_rtfor') or nm.startswith('_strncnt'):
                conf = 'medium'
                ev += '; local label inside an assembler CRT routine'
            if status == 'unverified':
                conf = 'medium'
                ev += '; match not byte verified'
            if inside_renderer:
                conf = 'low'
                ev += '; DOUBT: lies inside renderer code (below the CRT block at 0x1003b710), probably not a CRT copy'
            if va in funcs:
                R.add(va, nm, 'func', unit, ev, conf, 'crt-lib')
                n += 1
            else:
                # local label / jump table / entry inside another function of an assembler CRT routine
                R.add(va, nm, 'data', unit, 'local label or jump table of LIBCMT object %s (not a function entry in symbols.csv)' % unit,
                      'medium', 'crt-lib')
        for a, nm in sorted(u.get('data', {}).items()):
            va = int(a, 16)
            R.add(va, nm, 'data', unit, 'data symbol of LIBCMT object %s found by the library matcher' % unit, 'high',
                  'crt-lib')
    # names of CRT imports etc. are left to the import rows
    return n


SLOTS = [  # offset, RenderStruct member (engine renderstruct.h / Jupiter renderstruct.h), address
    (0x70, 'Init', 0x10010b22), (0x74, 'Term', 0x10010b13), (0x78, 'BindTexture', 0x10021b50),
    (0x7c, 'UnbindTexture', 0x10021c60), (0x80, 'RebindLightmaps', 0x1001b790), (0x84, 'CreateContext', 0x1001b700),
    (0x88, 'DeleteContext', 0x1001b770), (0x8c, 'Clear', 0x100142f0), (0x90, 'Start3D', 0x1001bd80),
    (0x94, 'End3D', 0x1001bdb0), (0x98, 'IsIn3D', 0x1001be20), (0x9c, 'StartOptimized2D', 0x1001c627),
    (0xa0, 'EndOptimized2D', 0x1001c725), (0xa4, 'IsInOptimized2D', 0x1001c7eb),
    (0xa8, 'SetOptimized2DBlend', 0x1001c7f1), (0xac, 'GetOptimized2DBlend', 0x1001c8ca),
    (0xb0, 'SetOptimized2DColor', 0x1001c8b8), (0xb4, 'GetOptimized2DColor', 0x1001c8da),
    (0xb8, 'RenderScene', 0x10017aa0), (0xbc, 'RenderCommand', 0x1001b8a0), (0xc0, 'GetHook', 0x100109c6),
    (0xc4, 'SwapBuffers', 0x1001e189), (0xc8, None, 0x1001bd70), (0xcc, 'GetScreenFormat', 0x1001dec3),
    (0xd0, 'CreateSurface', 0x1001da7f), (0xd4, 'DeleteSurface', 0x1001db2d), (0xd8, 'GetSurfaceInfo', 0x1001db4e),
    (0xdc, 'LockSurface', 0x1001db7e), (0xe0, 'UnlockSurface', 0x1001dbb4), (0xe4, 'OptimizeSurface', 0x1001c3e4),
    (0xe8, 'UnoptimizeSurface', 0x1001c3d2), (0xec, 'LockScreen', 0x1001dbca), (0xf0, 'UnlockScreen', 0x1001dc4f),
    (0xf4, 'BlitToScreen', 0x1001de1c), (0xf8, 'WarpToScreen', 0x1001de85), (0xfc, 'MakeScreenShot', 0x1001df19),
    (0x100, 'ReadConsoleVariables', 0x10012e4c), (0x104, 'BlitFromScreen', 0x1001dc81),
]
# source file (Jupiter spelling) each slot implementation most likely lived in
SLOT_SRC = {'Init': 'common_init', 'Term': 'common_init', 'BindTexture': 'd3d_texture', 'UnbindTexture': 'd3d_texture',
            'RebindLightmaps': 'd3d_init', 'CreateContext': 'd3d_init', 'DeleteContext': 'd3d_init',
            'Clear': 'd3d_draw', 'Start3D': 'd3d_draw', 'End3D': 'd3d_draw', 'IsIn3D': 'd3d_draw',
            'StartOptimized2D': 'd3d_optimizedsurface', 'EndOptimized2D': 'd3d_optimizedsurface',
            'IsInOptimized2D': 'd3d_optimizedsurface', 'SetOptimized2DBlend': 'd3d_optimizedsurface',
            'GetOptimized2DBlend': 'd3d_optimizedsurface', 'SetOptimized2DColor': 'd3d_optimizedsurface',
            'GetOptimized2DColor': 'd3d_optimizedsurface', 'RenderScene': 'd3d_draw', 'RenderCommand': 'd3d_init',
            'GetHook': 'common_init', 'ReadConsoleVariables': 'common_stuff', 'OptimizeSurface': 'd3d_optimizedsurface',
            'UnoptimizeSurface': 'd3d_optimizedsurface'}


def slot_rows(R):
    for off, mem, va in SLOTS:
        if mem is None:
            R.add(va, 'guess_d3d_RenderStructSlot_C8_ReturnZero', 'func', 'd3d_surface',
                  'stored at RenderStruct+0xc8 by RenderDLLSetup (between SwapBuffers and GetScreenFormat); body is `return 0`; '
                  'no Talon/Jupiter member name for this slot', 'low', 'invented')
            continue
        ev = ('stored at RenderStruct+0x%x by RenderDLLSetup 0x10010ff1 = member %s of decomp include/renderstruct.h' % (off, mem)
              if off not in (0xa4, 0xac, 0xb4) else
              'stored at RenderStruct+0x%x by RenderDLLSetup (unnamed pad in decomp renderstruct.h); Jupiter renderstruct.h names the member %s '
              'and body agrees' % (off, mem))
        ev += '; d3d_ prefix from Jupiter common_init.cpp rdll_RenderDLLSetup (Talon source name may be r_ / other)'
        R.add(va, 'd3d_' + mem, 'func', SLOT_SRC.get(mem, 'd3d_surface'), ev, 'medium', 'jupiter')


def exports_rows(R):
    R.add(0x10010f44, 'GetSupportedModes', 'func', 'common_init',
          'PE export GetSupportedModes (Jupiter rdll_GetSupportedModes in common_init.cpp)', 'high', 'string')
    R.add(0x10010fd3, 'FreeModeList', 'func', 'common_init',
          'PE export FreeModeList (Jupiter rdll_FreeModeList)', 'high', 'string')
    R.add(0x10010ff1, 'RenderDLLSetup', 'func', 'common_init',
          'PE export RenderDLLSetup; fills the RenderStruct function table (Jupiter rdll_RenderDLLSetup)', 'high', 'string')
    R.add(0x10029d5a, 'DllMain', 'func', 'd3d_main',
          '_DllMain@12 called by __DllMainCRTStartup@12 (0x1003d06f); body returns TRUE', 'high', 'crt-lib')
    R.add(0x10058470, 'g_pStruct', 'data', 'common_stuff',
          'written from the argument of RenderDLLSetup 0x10010ff1; all calls go through slots of the engine RenderStruct; Jupiter common_stuff.cpp `RenderStruct *g_pStruct`',
          'high', 'jupiter')
    R.add(0x100584f4, 'g_pConVars', 'data', 'common_stuff',
          'head of the ConVar list: written by the ConVar ctor 0x100111b5, walked by d3d_CreateConsoleVariables/d3d_ReadConsoleVariables (Jupiter common_stuff.cpp g_pConVars)',
          'high', 'jupiter')
    R.add(0x100111b5, 'ConVar::ConVar', 'func', 'common_stuff',
          'ctor of the 0x20-byte console variable record (name, default, mirror ptrs, g_pConVars link); Jupiter d3d_convar.h BaseConVar ctor has the same fields (no vtable here)',
          'medium', 'jupiter')
    R.add(0x10012ce5, 'dalloc', 'func', 'common_stuff', 'g_pStruct->Alloc (slot 0x34) else malloc; Jupiter shared simple_dalloc.cpp dalloc', 'high', 'jupiter')
    R.add(0x10012cfe, 'dalloc_z', 'func', 'common_stuff',
          'dalloc + memset 0; prints "d3drender.dll: out of memory" on failure; Jupiter dalloc_z', 'high', 'jupiter')
    R.add(0x10012d44, 'dfree', 'func', 'common_stuff', 'g_pStruct->Free (slot 0x38) else free; Jupiter dfree', 'high', 'jupiter')
    R.add(0x10012d5d, 'dsi_ConsolePrint', 'func', 'common_stuff',
          'vsnprintf(255) then g_pStruct->ConsolePrint; called with the exact messages Jupiter d3d_surface.cpp passes to dsi_ConsolePrint ("ScreenShot: Created %s successfully.")',
          'medium', 'jupiter')
    R.add(0x10012d92, 'AddDebugMessage', 'func', 'common_stuff',
          'AddDebugMessage(level, fmt, ...): prints only if level <= RenderDebug cvar (DAT_100584d4); same as Jupiter common_stuff.cpp AddDebugMessage (Talon adds no newline)',
          'high', 'jupiter')
    R.add(0x10012dd2, 'd3d_MaybeCreateCVar', 'func', 'common_stuff',
          'GetParameter(name) else RunConsoleString("%s %f") ; same body as Jupiter d3d_MaybeCreateCVar (Talon returns void)',
          'high', 'jupiter')
    R.add(0x10012e26, 'd3d_CreateConsoleVariables', 'func', 'common_stuff',
          'walks g_pConVars calling d3d_MaybeCreateCVar(name, default) and stores m_hParam; Jupiter d3d_CreateConsoleVariables', 'high', 'jupiter')
    R.add(0x1001a400, 'd3d_ReadExtraConsoleVariables', 'func', 'd3d_init',
          'called at the end of d3d_ReadConsoleVariables; sets D3DRENDERSTATE_FOG* (0x1c,0x22-0x25), DITHERENABLE (0x1a), texture-stage filters from FogR/G/B, FogNearZ/FarZ, Dither, Bilinear, Trilinear, Anisotropic: same as Jupiter d3d_init.cpp d3d_ReadExtraConsoleVariables',
          'high', 'jupiter')
    R.add(0x10010a69, 'guess_d3d_FreeDDraw', 'func', 'd3d_init',
          'releases back/primary/z surfaces and IDirectDraw7 (RestoreDisplayMode 0x4c, SetCooperativeLevel(DDSCL_NORMAL) 0x50) used by d3d_Term and the d3d_Init failure paths', 'low', 'invented')


def guid_rows(R):
    for a, n, h in GU.find():
        if n == 'IID_IDirect3DTnLHalDevice' or True:
            R.add(a, n, 'data', 'd3d_init', 'bytes equal DEFINE_GUID(%s) in %s of the DX SDK headers' % (n, h), 'high', 'guid')


# DirectX COM pointers (types proven by the vtable slots called through them, see tools/d3dren_names_gptr.py)
COM_GLOBALS = [
    (0x10057810, 'g_pDD', 'IDirectDraw7*', 'GetHook("LPDIRECTDRAW") returns it; calls +0x18 CreateSurface, +0x50 SetCooperativeLevel, +0x54 SetDisplayMode, +0x4c RestoreDisplayMode, +0x5c GetAvailableVidMem'),
    (0x10057814, 'g_pPrimary', 'IDirectDrawSurface7*', 'windowed mode: the primary surface (Blt +0x14 / SetClipper +0x70); in fullscreen mode it is NULL (R4: SwapBuffers Flips 10057818)'),
    (0x10057818, 'g_pBackBuffer', 'IDirectDrawSurface7*', 'windowed: offscreen surface equal to g_pOffscreen; fullscreen: the flipping primary (Flip +0x2c); passed to r_GetBufferFormatOfSurface'),
    (0x1005781c, 'g_pOffscreen', 'IDirectDrawSurface7*', 'ScreenShot: "g_pOffscreen->Lock returned %d" is the Lock (+0x64) of this surface; GetHook("BACKBUFFER") returns it; render target of CreateDevice'),
    (0x10057820, 'g_pZBuffer', 'IDirectDrawSurface7*', 'created in 0x1001aa70 ("Failed to make z-buffer."), attached to the render target, Restore (+0x6c) in End3D recovery'),
    (0x1005de30, 'g_pD3DDevice', 'IDirect3DDevice7*', 'BeginScene +0x14, EndScene +0x18, SetRenderState +0x50, SetTexture +0x8c, SetTextureStageState +0x94, DrawPrimitive +0x64: 105 users'),
    (0x1005de34, 'g_pD3D', 'IDirect3D7*', 'QueryInterface(IID_IDirect3D7) result; EnumZBufferFormats +0x18, CreateDevice +0x10'),
    (0x1005de38, 'g_pClipper', 'IDirectDrawClipper*', 'created lazily in SwapBuffers for windowed mode: IDirectDraw7::CreateClipper (+0x10), IDirectDrawClipper::SetHWnd (+0x20); released in d3d_FreeDDraw'),
]


def com_rows(R):
    for a, n, t, ev in COM_GLOBALS:
        conf = 'high' if n == 'g_pOffscreen' else 'low'
        prov = 'string' if n == 'g_pOffscreen' else 'invented'
        nm = n if conf == 'high' else 'guess_' + n
        R.add(a, nm, 'data', 'd3d_init', '%s; %s' % (t, ev), conf, prov)


def convar_rows(R, o):
    refs, funcs = o['refs'], o['funcs']
    jup = {}
    for m in re.finditer(r'RCONVAR\(\s*(\w+)\s*,\s*"([^"]+)"', open(JUP_CONVARS).read()):
        jup[m.group(2)] = m.group(1)
    cv = [r for r in CV.extract() if r['name'] and r['obj']]
    stats = Counter()
    table = []
    for r in cv:
        name = r['name']
        jn = jup.get(name)
        vname = jn or 'g_CV_' + name
        has_mirror = bool(r['intptr'] or r['floatptr'])
        csrc = 'common_stuff' if 46 <= r['idx'] <= 133 else ''
        mirror = r['intptr'] or r['floatptr']
        obj = r['obj']
        readers = sorted(f for f, rr in refs.items()
                         if any((obj <= t < obj + 0x20) or (mirror and mirror <= t < mirror + 4) for t in rr)
                         and f not in (r['entry'], r['body']))
        rd = ('; read by %d function(s): %s' % (len(readers), ' '.join('%08x' % x for x in readers[:6]))) if readers else '; no reader found (unused or read by name only)'
        ev_j = ('Jupiter rendererconsolevars.h RCONVAR(%s, "%s") has the same console variable text' % (jn, name)) if jn else \
            'no Jupiter counterpart; g_CV_<Name> follows the Jupiter/engine convention'
        if has_mirror:
            # value variable = the mirror global ; record = hidden static
            R.add(mirror, vname if jn else 'g_CV_' + name, 'data', csrc,
                  'int/float mirror of console variable "%s": ConVar record %08x stores &it at +0x%x and ReadConsoleVariables copies the value; %s%s' % (
                      name, obj, 0xc if r['intptr'] else 0x10, ev_j, rd), 'medium', 'cvar')
            R.add(obj, 'guess_conrec_' + name, 'data', csrc,
                  'ConVar record (0x20 bytes) of "%s" built by the static initialiser %08x' % (name, r['body']), 'low', 'invented')
        else:
            R.add(obj, vname, 'data', csrc,
                  'ConVar object of console variable "%s" (name string at +0x14), the code reads +0 (int) / +4 (float) directly; %s%s' % (name, ev_j, rd),
                  'medium', 'cvar')
        # static initialiser
        R.add(r['entry'], '_$E_ConVar_' + name, 'func',
              csrc, 'static initialiser (entry #%d of the .CRT$XCU-style table at 0x10048004) that builds the ConVar for "%s"%s' % (
                  r['idx'], name, ' (5-byte jmp thunk to %08x)' % r['body'] if r['thunk'] else ''),
              'medium', 'cvar')
        if r['thunk']:
            R.add(r['body'], '_$E_ConVar_%s_impl' % name, 'func', csrc,
                  'body of the static initialiser thunk %08x for ConVar "%s"' % (r['entry'], name), 'medium', 'cvar')
        table.append((r['idx'], name, obj, mirror, r['default'], r['entry'], r['body'], readers))
        stats['mirror' if has_mirror else 'direct'] += 1
    return table, stats


def init_rows(R, o):
    """Static initialisers / atexit destructors that are not console variables."""
    import struct as st
    import pefile
    pe = pefile.PE(S.IMAGE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    funcs = o['funcs']
    tab = [st.unpack_from('<I', pe.get_data(CV.TABLE - base + 4 * i, 4))[0] for i in range(CV.NTAB)]
    return tab


def agent_sources(agents_dir):
    """(tag, [row dict]) per region agent: from DIR/agent_R*.csv, or the embedded tools/d3dren_names_agents_data.py when DIR is ''."""
    if agents_dir:
        for f in sorted(glob.glob(os.path.join(agents_dir, 'agent_R*.csv'))):
            yield os.path.basename(f)[:-4], list(csv.DictReader(open(f, encoding='utf-8')))
    else:
        import d3dren_names_agents_data as A
        for tag, rows in sorted(A.AGENT_ROWS.items()):
            yield tag, [dict(zip(HDR, r)) for r in rows]


def build(agents_dir=None, quiet=False):
    o = S.load()
    funcs = o['funcs']
    R = Rows()
    n_crt = crt_rows(R, funcs)
    exports_rows(R)
    slot_rows(R)
    guid_rows(R)
    com_rows(R)
    table, stats = convar_rows(R, o)
    # manual / data modules
    try:
        import d3dren_names_data as D
        for row in D.ROWS:
            R.add(row[0], row[1], row[2], row[3], row[4], row[5], row[6], origin='manual')
    except ImportError:
        pass
    # agents: collect all rows first, then resolve same-address conflicts (region owner, then confidence, then non-guess)
    if agents_dir is not None:
        REG = [('agent_R1', 0x10001000, 0x1000b349), ('agent_R2', 0x1000b349, 0x10013215),
               ('agent_R3', 0x10013215, 0x1001a380), ('agent_R4', 0x1001a380, 0x10021d70),
               ('agent_R5', 0x10021d70, 0x1002a0c2), ('agent_R6', 0x1002a0c2, 0x10030bb0),
               ('agent_R7', 0x10030bb0, 0x10038830), ('agent_R8', 0x10038830, 0x1003b710)]
        rank = {'high': 3, 'medium': 2, 'low': 1}
        cand = {}
        for tag, rr in agent_sources(agents_dir):
            for r in rr:
                if 'guess_' in r['name'] and not r['name'].startswith('guess_'):
                    r['name'] = 'guess_' + r['name'].replace('guess_', '').replace('.', '_')
                if r['provenance'] == 'invented' and not r['name'].startswith('guess_'):
                    r['name'] = 'guess_' + r['name']
                if r['name'].startswith('guess_') and (r['provenance'] != 'invented' or r['confidence'] != 'low'):
                    r['evidence'] = '[role evidence: %s/%s] %s' % (r['provenance'], r['confidence'], r['evidence'])
                    r['provenance'], r['confidence'] = 'invented', 'low'
                cand.setdefault(r['address'].lower(), []).append((tag, r))
        for a, lst in sorted(cand.items()):
            va = int(a, 16)

            def key(t):
                tag, r = t
                own = any(n == tag and lo <= va < hi for n, lo, hi in REG)
                return (r['name'].startswith('guess_'), -rank.get(r['confidence'], 0), not own, tag)
            lst.sort(key=key)
            tag, r = lst[0]
            ok = R.add(a, r['name'], r['kind'], r['source_file'], r['evidence'], r['confidence'], r['provenance'], origin=tag)
            for t2, r2 in lst[1:]:
                if r2['name'] != r['name']:
                    R.conflicts.append((a, tag, r['name'], t2, r2['name']))
    return R, table, stats, n_crt


def static_init_rows(R, o):
    """Name the static initialisers / atexit destructors nobody named after the object they build (guess_ names)."""
    import struct as st
    import capstone
    import pefile
    pe = pefile.PE(S.IMAGE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    funcs, refs = o['funcs'], o['refs']
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    tab = [st.unpack_from('<I', pe.get_data(CV.TABLE - base + 4 * i, 4))[0] for i in range(CV.NTAB)]
    # atexit destructors: push imm before call _atexit (0x1003b7e8)
    dtors = []
    for c in sorted(o['callers'].get(0x1003b7e8, ())):
        ins = list(md.disasm(pe.get_data(c - base, funcs[c][0] - c), c))
        for k, i in enumerate(ins):
            if i.mnemonic == 'call' and i.op_str == '0x1003b7e8':
                for j in range(k - 1, -1, -1):
                    if ins[j].mnemonic == 'push' and ins[j].op_str.startswith('0x'):
                        dtors.append((c, int(ins[j].op_str, 16)))
                        break
    names = {int(a, 16): r['name'] for a, r in R.items()}

    def obj_of(fa):
        best = None
        for t in sorted(refs.get(fa, {})):
            if t in names and not names[t].startswith('g_CV_') and 0x10048000 <= t:
                best = names[t]
                break
        return best
    n = 0
    for i, a in enumerate(tab):
        if '%08x' % a in R:
            continue
        ob = obj_of(a)
        size = funcs[a][0] - a
        if ob is None and size <= 16 and len(list(md.disasm(pe.get_data(a - base, size), a))) <= 2:
            nm = 'guess_StaticInit_Empty_%08x' % a
            ev = 'entry #%d of the static-initialiser table 0x10048004: body is only `ret` (global with an empty constructor / folded stub)' % i
        elif ob:
            nm = 'guess_StaticInit_' + ob.replace('guess_', '', 1).replace('::', '_')
            ev = 'entry #%d of the static-initialiser table 0x10048004: initialises %s' % (i, ob)
        else:
            continue
        if R.add(a, nm, 'func', '', ev, 'low', 'invented'):
            n += 1
    for init, d in dtors:
        if '%08x' % d in R:
            continue
        if init in tab:
            ob = obj_of(init) or obj_of(d)
            nm = ('guess_StaticDtor_' + ob.replace('guess_', '', 1).replace('::', '_')) if ob else 'guess_StaticDtor_%08x' % d
        else:      # function-local static object: the registering function is a normal function
            fn = names.get(init, '%08x' % init).replace('guess_', '', 1).replace('::', '_')
            nm = 'guess_StaticDtor_local_static_of_%s' % fn
        if R.add(d, nm, 'func', '', 'atexit() destructor registered by %08x (call 0x1003b7e8)' % init, 'low', 'invented'):
            n += 1
    return n, len(dtors)


def cap_tiny(R, funcs):
    """A <= 8 byte function (getter / return stub) can be merged with identical functions by the linker (ICF):
    its identity from the body alone is ambiguous, so high -> medium."""
    n = 0
    for a, r in R.items():
        va = int(a, 16)
        if r['kind'] == 'func' and va in funcs and r['provenance'] != 'crt-lib' and r['confidence'] == 'high'                 and funcs[va][0] - va <= 8 and not r['name'].startswith('guess_'):
            r['confidence'] = 'medium'
            r['evidence'] += '; capped to medium: body <= 8 bytes, identical code may be folded by the linker (ICF)'
            n += 1
    return n


def derive_source_files(R, table):
    """Rows without source_file: function takes the file of its named neighbours when both agree (same TU);
    cvar data rows take the file of their static initialiser."""
    import bisect
    pts = sorted((int(a, 16), r['source_file']) for a, r in R.items()
                 if r['kind'] == 'func' and r['source_file'] and r['provenance'] != 'crt-lib'
                 and 0x10001000 <= int(a, 16) < 0x1003b710)
    keys = [p[0] for p in pts]
    n = 0
    for a, r in R.items():
        va = int(a, 16)
        if r['kind'] == 'func' and not r['source_file'] and 0x10001000 <= va < 0x1003b710 and r['provenance'] != 'crt-lib':
            i = bisect.bisect_left(keys, va)
            if 0 < i < len(pts) and pts[i - 1][1] == pts[i][1]:
                r['source_file'] = pts[i - 1][1]
                r['evidence'] += '; source_file derived: neighbouring named functions on both sides are in %s' % pts[i][1]
                n += 1
    for idx, name, obj, mirror, dflt, entry, body, readers in table:
        e = R.get('%08x' % entry)
        for va in (obj, mirror):
            r = R.get('%08x' % va) if va else None
            if r and not r['source_file'] and e and e['source_file']:
                r['source_file'] = e['source_file']
                n += 1
    return n


def apply_xcheck(R):
    """Annotate / cap rows that cite an lithtech.exe function: mnemonic-sequence similarity of the two bodies."""
    import d3dren_names_verify as V
    rows = [dict(address=a, kind=r['kind'], evidence=r['evidence']) for a, r in R.items()]
    res = V.check(rows)
    n = 0
    for a, (ratio, c, s1, s2) in res.items():
        r = R[a]
        note = '; xcheck: mnemonic similarity with lithtech.exe %08x = %.2f (sizes %d/%d)' % (c, ratio, s1, s2)
        if ratio < 0.5 and r['confidence'] == 'high' and not r['name'].startswith('guess_'):
            r['confidence'] = 'medium'
            note += ' -> capped to medium: same-named function in a different revision / compiled differently, body not identical'
            n += 1
        r['evidence'] += note
    return n


HEADER_SRC = ('ltmatrix', 'ltvector', 'stl_vector', 'stl_alloc', 'stl_threads', 'object_bank', 'ltbasedefs', 'glink',
              'ltdynarray', 'l_allocator_h')


def mark_header_sources(R):
    """Header-defined inline / template COMDAT copies sit physically in whichever TU first needed them: prefix `h:`."""
    n = 0
    for a, r in R.items():
        if r['kind'] != 'func' or r['provenance'] == 'crt-lib' or not r['source_file'] or r['source_file'].startswith('h:'):
            continue
        sf = r['source_file']
        if sf in HEADER_SRC or (('<' in r['name']) and sf == 'dynarray') or                 r['name'].startswith(('BaseObjectSet::Add', 'AllocSet::')) and sf == 'tagnodes' and int(a, 16) < 0x10038830:
            r['source_file'] = 'h:' + sf
            n += 1
    return n


def sanitize_names(R):
    for a, r in R.items():
        if r['source_file'].startswith('guess_'):
            r['source_file'] = r['source_file'][6:]
    import re as _re
    for a, r in R.items():
        if r['provenance'] != 'crt-lib':
            n = _re.sub(r"[ `']+", '_', r['name']).rstrip('_')
            if n != r['name']:
                r['name'] = n


def dedupe_names(R):
    cnt = Counter(r['name'] for r in R.values())
    for a, r in R.items():
        if cnt[r['name']] > 1:
            base = r['name']
            r['name'] = '%s__%s' % (base, (r['source_file'] or a).split('/')[-1] if r['source_file'] else a)
            r['evidence'] += '; name disambiguated (same symbol name in several places)'
    cnt = Counter(r['name'] for r in R.values())
    for a, r in R.items():
        if cnt[r['name']] > 1:
            r['name'] = '%s_%s' % (r['name'], a)


def write(R, out=OUT):
    mark_header_sources(R)
    sanitize_names(R)
    dedupe_names(R)
    with open(out, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(HDR)
        for a in sorted(R):
            r = R[a]
            w.writerow([r[k] for k in HDR])


def validate(R, o):
    funcs = o['funcs']
    names = Counter(r['name'] for r in R.values())
    dup = [n for n, c in names.items() if c > 1]
    bad = []
    for a, r in R.items():
        va = int(a, 16)
        if r['kind'] == 'func' and va not in funcs:
            bad.append((a, 'not a function entry', r['name']))
        if r['confidence'] not in ('high', 'medium', 'low'):
            bad.append((a, 'bad confidence', r['confidence']))
        if r['provenance'] not in ('string', 'cvar', 'exe-decomp', 'jupiter', 'crt-lib', 'dx-sdk', 'guid', 'vtable', 'callgraph', 'invented'):
            bad.append((a, 'bad provenance', r['provenance']))
        if r['provenance'] == 'invented' and not r['name'].startswith('guess_'):
            bad.append((a, 'invented without guess_', r['name']))
        if r['name'].startswith('guess_') and (r['provenance'] != 'invented' or r['confidence'] != 'low'):
            bad.append((a, 'guess_ must be low/invented', r['name']))
        if r['kind'] not in ('func', 'data', 'vtable', 'type'):
            bad.append((a, 'bad kind', r['kind']))
    return dup, bad


if __name__ == '__main__':
    out = OUT
    ad = ''          # '' = embedded agent rows (tools/d3dren_names_agents_data.py)
    if '--out' in sys.argv:
        out = sys.argv[sys.argv.index('--out') + 1]
    if '--agents' in sys.argv:
        ad = sys.argv[sys.argv.index('--agents') + 1]
    R, table, stats, ncrt = build(ad)
    o = S.load()
    nsi = static_init_rows(R, o)
    print('extra static-init rows', nsi)
    ncap = apply_xcheck(R)
    print('tiny-function cap', cap_tiny(R, o['funcs']))
    nder = derive_source_files(R, table)
    print('source_file derived for', nder, 'rows')
    dup, bad = validate(R, o)
    write(R, out)
    print('xcheck capped', ncap)
    print('rows', len(R), 'crt funcs', ncrt, 'convars', stats)
    print('by confidence', Counter(r['confidence'] for r in R.values()))
    print('by provenance', Counter(r['provenance'] for r in R.values()))
    print('by origin', Counter(r['origin'] for r in R.values()))
    print('duplicate names', dup[:20], len(dup))
    print('validation problems', len(bad))
    for b in bad[:30]:
        print('  ', b)
    print('conflicts', len(R.conflicts))
    for c in R.conflicts[:40]:
        print('  ', c)
