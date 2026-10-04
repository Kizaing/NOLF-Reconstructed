r"""d3d.ren work packages: partition the renderer source units into disjoint packages for parallel agents.

  python tools/d3dren_units_recover.py     # first (writes build/d3dren/units/units.json)
  python tools/d3dren_units_wave.py [K]    # K packages (default 9); writes build/d3dren/units/waves.json and prints the tables
                                           # (config/d3dren/WAVE_PLAN.md is written by tools/d3dren_units_report.py from waves.json)

Balanced metrics: code bytes and *non-trivial* functions (static-initialiser wrappers/bodies of ConVars and other
`_$E` groups, and 1-byte `ret` stubs are copy-paste work and excluded; the plain function count is reported too).
Objective: minimise the largest of (bytes / mean bytes, non-trivial functions / mean) over the packages, plus a small
sum-of-squares term, minus a cohesion bonus (call edges between units that end up in the same package), by
simulated annealing with random restarts (fixed seeds: the result is reproducible).
"""
import bisect
import collections
import json
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, 'build', 'd3dren', 'units')
sys.path.insert(0, HERE)


def load():
    U = json.load(open(os.path.join(OUT, 'units.json')))['units']
    S = json.load(open(os.path.join(OUT, 'scan.json')))
    return U, S


def static_init_set(funcs, xcu, lo, hi):
    """static-initialiser functions: XCU wrappers plus the functions they call/jump to inside the unit (`_$E` bodies)"""
    by = {f['addr']: f for f in funcs}
    S = set()
    for a in xcu:
        if lo <= a < hi and a in by:
            S.add(a)
            for c in by[a]['calls']:
                if lo <= c < hi and abs(c - a) <= 0x100 and c in by and by[c]['end'] - c <= 0x100:
                    S.add(c)
    return S


def unit_metrics(U, S, xcu):
    funcs = S['funcs']
    starts = [u['lo'] for u in U]
    for u in U:
        fs = [f for f in funcs if u['lo'] <= f['addr'] < u['hi']]
        si = static_init_set(funcs, xcu, u['lo'], u['hi'])
        # second pass: 1-byte ret stubs
        trivial = set(si) | set(f['addr'] for f in fs if f['end'] - f['addr'] <= 1)
        u['nf'] = len(fs)
        u['nontrivial'] = sum(1 for f in fs if f['addr'] not in trivial)
        nt = [f for f in fs if f['addr'] not in trivial]      # statistics exclude the ConVar initialisers (fldz/fstp defaults)
        u['x87'] = sum(f['nfp'] for f in nt)
        u['x87funcs'] = sum(1 for f in nt if f['nfp'])
        u['x87cmp'] = sum(1 for f in nt if f['nx87cmp'])
        u['com'] = sum(f['ncom'] for f in nt)
        u['switches'] = sum(f['nswitch'] for f in nt)
        u['huge'] = sum(1 for f in nt if f['end'] - f['addr'] >= 0x800)
        top = sorted(nt, key=lambda f: -(f['end'] - f['addr']))[:4]
        u['top'] = [(f['addr'], f['end'] - f['addr'], f['nfp'], f['ncom']) for f in top]
        u['fs'] = [f['addr'] for f in fs]
    return starts


def unit_of(U, starts, a):
    i = bisect.bisect_right(starts, a) - 1
    return i if i >= 0 and U[i]['lo'] <= a < U[i]['hi'] else None


def call_matrix(U, starts, S):
    M = collections.Counter()
    for f in S['funcs']:
        i = unit_of(U, starts, f['addr'])
        if i is None:
            continue
        for c in f['calls']:
            j = unit_of(U, starts, c)
            if j is not None and j != i:
                M[(i, j)] += 1
    return M


def anneal(items, K, coh, seed_base=1, restarts=8, iters=40000):
    """items: [(name, bytes, nontrivial)]; coh[(i,j)] cohesion weight (symmetric sum)"""
    N = len(items)
    tb = sum(i[1] for i in items)
    tn = sum(i[2] for i in items)
    ab, an = tb / K, tn / K
    totcoh = sum(coh.values()) or 1

    def cost(a):
        b = [0] * K
        n = [0] * K
        for (name, by, nt), k in zip(items, a):
            b[k] += by
            n[k] += nt
        mx = max(max(b[k] / ab, n[k] / an) for k in range(K))
        sq = sum((b[k] / ab - 1) ** 2 + (n[k] / an - 1) ** 2 for k in range(K))
        ci = sum(w for (i, j), w in coh.items() if a[i] == a[j])
        return mx + 0.15 * sq - 0.10 * ci / totcoh
    best = None
    for rs in range(restarts):
        random.seed(seed_base * 100 + rs)
        a = [random.randrange(K) for _ in range(N)]
        c = cost(a)
        T = 0.4
        for it in range(iters):
            i = random.randrange(N)
            if random.random() < 0.5:
                old = a[i]
                a[i] = random.randrange(K)
                c2 = cost(a)
                if c2 < c or random.random() < math.exp((c - c2) / T):
                    c = c2
                else:
                    a[i] = old
            else:
                j = random.randrange(N)
                a[i], a[j] = a[j], a[i]
                c2 = cost(a)
                if c2 < c or random.random() < math.exp((c - c2) / T):
                    c = c2
                else:
                    a[i], a[j] = a[j], a[i]
            T = max(0.003, T * 0.9998)
        if best is None or c < best[0]:
            best = (c, a[:])
    return best


def main():
    K = int(sys.argv[1]) if len(sys.argv) > 1 else 9
    U, S = load()
    import pefile
    pe = pefile.PE(r'E:\AVP2Source\bin\talon\d3d.ren')
    base = pe.OPTIONAL_HEADER.ImageBase
    dsec = [s for s in pe.sections if s.Name.startswith(b'.data')][0]
    import struct
    dd = dsec.get_data()
    xcu = [x for x in (struct.unpack_from('<I', dd, o)[0] for o in range(4, 0x368, 4)) if x]
    starts = unit_metrics(U, S, xcu)
    src = [i for i, u in enumerate(U) if u['src']]
    M = call_matrix(U, starts, S)
    coh = collections.Counter()
    pos = {i: k for k, i in enumerate(src)}
    for (i, j), w in M.items():
        if i in pos and j in pos:
            coh[(min(pos[i], pos[j]), max(pos[i], pos[j]))] += w
    items = [(U[i]['unit'], U[i]['hi'] - U[i]['lo'], U[i]['nontrivial']) for i in src]
    best = anneal(items, K, coh)
    a = best[1]
    pk = collections.defaultdict(list)
    for k, i in zip(a, src):
        pk[k].append(i)
    # order packages by lowest start address
    order = sorted(pk, key=lambda k: min(U[i]['lo'] for i in pk[k]))
    res = []
    for n, k in enumerate(order, 1):
        us = sorted(pk[k], key=lambda i: U[i]['lo'])
        b = sum(U[i]['hi'] - U[i]['lo'] for i in us)
        nf = sum(U[i]['nf'] for i in us)
        nt = sum(U[i]['nontrivial'] for i in us)
        res.append(dict(id='W%d' % n, units=[U[i]['unit'] for i in us], bytes=b, nf=nf, nontrivial=nt,
                        x87=sum(U[i]['x87'] for i in us), com=sum(U[i]['com'] for i in us),
                        switches=sum(U[i]['switches'] for i in us)))
    json.dump(dict(K=K, packages=res, cost=best[0]), open(os.path.join(OUT, 'waves.json'), 'w'))
    ext = {u['unit']: {k: u[k] for k in ('nf', 'nontrivial', 'x87', 'x87funcs', 'x87cmp', 'com', 'switches', 'top', 'huge')} for u in U}
    json.dump(ext, open(os.path.join(OUT, 'units_ext.json'), 'w'))
    tb = sum(r['bytes'] for r in res)
    tn = sum(r['nontrivial'] for r in res)
    tf = sum(r['nf'] for r in res)
    for r in res:
        print('%s bytes=%6d (%3.0f%%) fn=%4d (%3.0f%%) nontrivial=%4d (%3.0f%%) x87=%5d com=%4d  %s' %
              (r['id'], r['bytes'], 100 * r['bytes'] * K / tb, r['nf'], 100 * r['nf'] * K / tf, r['nontrivial'], 100 * r['nontrivial'] * K / tn,
               r['x87'], r['com'], ', '.join(r['units'])))
    print('total bytes %d  functions %d  non-trivial %d' % (tb, tf, tn))
    # cross-package cohesion
    pkg_of = {}
    for r in res:
        for u in r['units']:
            pkg_of[u] = r['id']
    inside = cross = 0
    for (i, j), w in M.items():
        if U[i]['unit'] in pkg_of and U[j]['unit'] in pkg_of:
            if pkg_of[U[i]['unit']] == pkg_of[U[j]['unit']]:
                inside += w
            else:
                cross += w
    print('call edges inside packages %d, across packages %d (%.0f%% inside)' % (inside, cross, 100 * inside / max(1, inside + cross)))


if __name__ == '__main__':
    main()
