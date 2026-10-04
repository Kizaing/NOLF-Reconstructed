r"""d3d.ren unit recovery: the validation experiments behind config/d3dren/units_proposal.md ("What did not work").

  python tools/d3dren_units_experiments.py

1. detector test: how well do structural signals that could find object boundaries INSIDE an A run recover the 24
   hard boundaries (A/P switches and 0xCC-padded ends) that the alignment evidence gives?  A signal is useful if the
   hard boundaries rank in the lowest percentile of its score; random would be 0.50.
     calls  : call edges between the 8 functions before and the 8 after the position (fewer = more boundary-like)
     shared : private data items referenced on both sides (windows of 8 functions, items with <= 6 users)
2. bss order: Spearman rho between bss address order and first-user function order of the private variables of each
   region.  ~0: hash order (extern globals of one C++ object); > 0.5: definition order (file-statics) or several
   objects.  P regions are single objects by the padding argument, so rho > 0 there shows that the signal does NOT
   identify objects (P2: two ConVar blocks in disjoint bss ranges, rho 0.66, yet no padding between them).
"""
import collections
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import d3dren_units_regions as R  # noqa: E402


def main():
    scan = R.load_scan()
    funcs = R.renderer_functions(scan)
    tb, tlo = R.text_bytes()
    thunks = [(a['end'], b['addr']) for a, b in zip(funcs, funcs[1:]) if b['addr'] > a['end']]
    regs = R.compute_regions(funcs, tb, tlo, thunks)
    N = len(funcs)
    idx = {f['addr']: i for i, f in enumerate(funcs)}
    hard = sorted(idx[r['lo']] for r in regs[1:] if r['lo'] in idx)
    print('regions %d (A %d, P %d), hard boundaries %d' % (len(regs), sum(r['kind'] == 'A' for r in regs),
                                                             sum(r['kind'] == 'P' for r in regs), len(hard)))
    users = collections.defaultdict(set)
    for i, f in enumerate(funcs):
        for v in f['drefs'] + f['brefs'] + [x for x in f['rrefs'] if x < 0x10046666]:
            users[v].add(i)

    def pct(x):
        order = np.argsort(np.argsort(x)) / N
        p = [order[b] for b in hard]
        return np.mean(p), np.median(p), np.mean([q < 0.2 for q in p])

    def callwin(w):
        out = np.zeros(N)
        for b in range(1, N):
            c = 0
            for i in range(max(0, b - w), b):
                for t in funcs[i]['calls']:
                    j = idx.get(t)
                    if j is not None and b <= j < b + w:
                        c += 1
            for i in range(b, min(N, b + w)):
                for t in funcs[i]['calls']:
                    j = idx.get(t)
                    if j is not None and max(0, b - w) <= j < b:
                        c += 1
            out[b] = c
        return out

    def shared(w, maxn):
        sets = [set(v for v in f['drefs'] + f['brefs'] + [x for x in f['rrefs'] if x < 0x10046666] if len(users[v]) <= maxn)
                for f in funcs]
        out = np.zeros(N)
        for b in range(1, N):
            L = set().union(*sets[max(0, b - w):b])
            Rr = set().union(*sets[b:b + w])
            out[b] = len(L & Rr)
        return out
    for w in (4, 8, 16):
        m, md, f20 = pct(callwin(w))
        print('calls  w=%2d: hard boundaries mean percentile %.2f median %.2f, in lowest 20%%: %.0f%%' % (w, m, md, 100 * f20))
    for w, mx in ((8, 6), (16, 6)):
        m, md, f20 = pct(shared(w, mx))
        print('shared w=%2d maxn=%d: mean percentile %.2f median %.2f, in lowest 20%%: %.0f%%' % (w, mx, m, md, 100 * f20))

    # 2. bss order per region
    bu = collections.defaultdict(list)
    for i, f in enumerate(funcs):
        for v in f['brefs'] + f['drefs']:
            if 0x1004d5a0 <= v < 0x10093b00:
                bu[v].append(i)
    print('\nbss order vs function order (private variables of each region):')
    for ri, r in enumerate(regs):
        lo, hi = r['first'], r['last']
        pv = sorted((v, min(u)) for v, u in bu.items() if len(u) <= 6 and all(lo <= x <= hi for x in u))
        if len(pv) < 8:
            continue
        rx = np.argsort(np.argsort([a for a, _ in pv]))
        ry = np.argsort(np.argsort([p for _, p in pv]))
        rho = np.corrcoef(rx, ry)[0, 1]
        print('  %s %08x-%08x fn=%3d private bss vars=%4d rho=%+.2f' % (r['kind'], r['lo'], r['hi'], r['nf'], len(pv), rho))


if __name__ == '__main__':
    main()
