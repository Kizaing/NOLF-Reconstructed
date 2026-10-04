r"""Relink gate: link every fully matched unit from its base object and report which units relink byte-identically.

  python tools/build.py relink          (after a full `python tools/build.py`; same as python tools/relink_gate.py)

Runs `tools/relink.py --mode mixed` into build/relink-gate (RELINK_OUT overrides), compares the result with the
original exe section by section, and attributes every differing .text byte to the unit whose function holds it
(build/objvas.json + the symbol extents). Writes build/relink_gate.json and lists the units that changed since the
previous run, so integration catches a layout regression (a function emitted in a different order, a changed
COMDAT, data the code refers to at a different address) that the per-function check can't see.
All units link together, so a unit whose functions move also changes the call sites (rel32 operands) in other
units that call them: a few bytes in an otherwise identical unit usually mean that, not a fault of that unit
(check with `tools/relink.py --mode mixed --only <unit>`).
Exit code 1 if a unit that was identical in the previous run is not identical now.

By default the relink uses relink.py's --own-data: every fully matched unit whose data matches supplies its own
.rdata/.data (the others, and everything not fully matched, come from the exe's bytes). .rdata then differs where a
vtable points at a function that moved in .text. The per-unit data status is always reported
(build/relink-gate/data_units.json, data_status.json), with the units whose status got worse.

  --standin-data   the pre-wave-7 link: all .rdata/.data from one stand-in object (relink.py --standin-data
                   --data-units), so only .text can differ.
  --data, --own-data   accepted for compatibility (both are the default now).
"""
import bisect, json, os, subprocess, sys

import pefile

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
BUILD = os.path.join(ROOT, 'build')
ORIG = r'E:\AVP2Source\bin\lithtech.exe'
STATE = os.path.join(BUILD, 'relink_gate.json')


def main():
    import build as B
    out = os.path.normpath(os.environ.get('RELINK_OUT') or os.path.join(BUILD, 'relink-gate'))
    env = dict(os.environ, RELINK_OUT=out)
    own = '--standin-data' not in sys.argv
    data = True
    extra = ['--own-data'] if own else ['--standin-data', '--data-units']
    r = subprocess.run([sys.executable, os.path.join(TOOLS, 'relink.py'), '--mode', 'mixed'] + extra, cwd=ROOT, env=env,
                       capture_output=True, text=True)
    full, fell_back = [], ''
    for l in r.stdout.splitlines():
        if l.startswith('base objects used:'):
            fell_back = l.split('fell back to target:', 1)[-1].strip()
        if l.startswith('fully matched units:'):
            print(l)
        if l.startswith('units supplying their own .rdata/.data:'):
            print(l.split(':')[0] + ': ' + l.split(':')[1].strip())
        if l.startswith(('verified native library data:', 'verified source EH:',
                         'native source EH:', 'native library data:')):
            print(l)
    exe = os.path.join(out, 'lithtech.exe')
    if r.returncode != 0 or 'link rc 0' not in r.stdout or not os.path.exists(exe):
        print(r.stdout[-3000:], r.stderr[-3000:])
        print('RELINK FAILED')
        return 2
    print('base objects that fell back to their target object: %s' % fell_back)

    a, b = pefile.PE(ORIG), pefile.PE(exe)
    sb = {s.Name.rstrip(b'\0').decode('latin1'): s for s in b.sections}
    base = a.OPTIONAL_HEADER.ImageBase
    differing = {}
    for s in a.sections:
        n = s.Name.rstrip(b'\0').decode('latin1')
        if n not in sb:
            print('  %-6s missing from the relink' % n)
            continue
        da, db = s.get_data(), sb[n].get_data()
        diff = [i for i, (p, q) in enumerate(zip(da, db)) if p != q] + list(range(min(len(da), len(db)), max(len(da), len(db))))
        differing[n] = [base + s.VirtualAddress + i for i in diff]
        print('  %-6s %d of %d bytes differ' % (n, len(diff), len(da)))

    # attribute .text bytes to units
    symtab = B.SymTab()
    starts = sorted(symtab.funcs)
    objvas = json.load(open(os.path.join(BUILD, 'objvas.json')))
    owner = {va: u for u, vas in objvas.items() for va in vas}
    per_unit = {}
    for va in differing.get('.text', []):
        k = bisect.bisect_right(starts, va) - 1
        f = starts[k] if k >= 0 else None
        u = owner.get(f, '(no unit: %08x)' % f if f is not None else '(before .text)') \
            if f is not None and va < symtab.funcs[f][0] else '(padding)'
        per_unit[u] = per_unit.get(u, 0) + 1

    inv_full = [l for l in r.stdout.splitlines() if l.startswith('fully matched units:')]
    import relink as R
    import io, contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        _, full, _ = R.inventory()
    bad = {u: n for u, n in per_unit.items() if u in full}
    ident = sorted(u for u in full if u not in per_unit)
    print('fully matched units relinking byte-identically: %d of %d' % (len(ident), len(full)))
    for u, n in sorted(per_unit.items(), key=lambda x: -x[1]):
        print('  %-40s %6d .text bytes differ%s' % (u, n, '' if u in full else '  (not a fully matched unit)'))

    prev = json.load(open(STATE)) if os.path.exists(STATE) else None
    state = {'identical': ident, 'differing': per_unit,
             'sections': {k: len(v) for k, v in differing.items()}}
    rc = 0
    if prev:
        regressed = sorted(set(prev['identical']) - set(ident) - (set(prev['identical']) - set(full)))
        gained = sorted(set(ident) - set(prev['identical']))
        dropped = sorted(set(prev['identical']) - set(full))
        if regressed:
            print('REGRESSED (identical in the previous run): %s' % ', '.join(regressed))
            rc = 1
        if gained:
            print('newly identical: %s' % ', '.join(gained))
        if dropped:
            print('no longer fully matched (not checked): %s' % ', '.join(dropped))
        for k, v in state['sections'].items():
            if k != '.text' and v != prev['sections'].get(k, 0):
                print('%s: %d bytes differ (previous run %d)' % (k, v, prev['sections'].get(k, 0)))
    # data status (relink.py --data-units): 'match' > 'edge' (own sections match, unexplained bytes beside them) > 'differs'
    rank = {'match': 2, 'edge': 1, 'differs': 0}
    if data and os.path.exists(os.path.join(out, 'data_status.json')):
        ds = json.load(open(os.path.join(out, 'data_status.json')))
        state['data'] = {u: v['status'] for u, v in ds.items()}
        cnt = {}
        for v in state['data'].values():
            cnt[v] = cnt.get(v, 0) + 1
        print('data status of the fully matched units: %s' % ', '.join('%s %d' % kv for kv in sorted(cnt.items())))
        for u, v in sorted(ds.items()):
            if v['status'] != 'match':
                print('  %-40s %s: %s' % (u, v['status'], '; '.join(v['issues'][:2])))
        old = (prev or {}).get('data', {})
        worse = sorted(u for u, v in state['data'].items() if u in old and rank[v] < rank.get(old[u], 0))
        if worse:
            print('DATA REGRESSED (status worse than in the previous --data run): %s' % ', '.join(worse))
    elif prev and 'data' in prev:
        state['data'] = prev['data']
    json.dump(state, open(STATE, 'w'), indent=1, sort_keys=True)
    return rc


if __name__ == '__main__':
    sys.exit(main())
