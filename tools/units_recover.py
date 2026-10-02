"""Recover original source-file (object) boundaries in lithtech.exe's .text.

Inputs (run units_defs.py and units_scan.py first):
  build/units/defs.json   name -> [(unit, source, weight)]
  build/units/scan.json   per-function calls / data refs / EH stubs
  config/libraries.json   (optional, from the library matcher) exact library object placements
Outputs:
  config/units_proposal.csv, config/units_proposal.md, build/units/assign.json
"""
import os, re, json, csv, bisect, collections, statistics
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, '..', 'build', 'units')
CFG = os.path.join(HERE, '..', 'config')

SWITCH = 0.75           # Viterbi cost of starting a new run
UNK = '?'

NEUTRAL_PREFIX = ('_STL::', 'std::', '`', 'Unwind@', 'Catch@', 'FUN_', 'thunk_', 'LAB_', 'map<>')
GENERIC = {'find', 'begin', 'end', 'insert_unique', 'Read', 'Write', 'Allocate', 'Free', 'Init', 'Term', 'insert',
           'erase', 'clear', 'size', 'Delete', 'operator=', 'operator==', 'swap', 'copy', 'fill', 'destroy',
           'construct', 'Construct', 'Destruct', 'GetSize', 'entry'}


def load():
    defs = json.load(open(os.path.join(B, 'defs.json')))
    scan = json.load(open(os.path.join(B, 'scan.json')))
    fp = json.load(open(os.path.join(B, 'filepaths.json')))
    libs = None
    p = os.path.join(CFG, 'libraries.json')
    if os.path.exists(p):
        try:
            libs = json.load(open(p))
        except Exception:
            libs = None
    return defs, scan, fp, libs


def clean_name(n):
    n = n.replace('FID_conflict:', '')
    return n


def is_neutral(n):
    return n.startswith(NEUTRAL_PREFIX) or '<' in n


def lookup(name, defs, prefix_map, class_map, method_map, iface_units=lambda c: {}):
    """-> (dict unit->weight, kind) kind in strong/weak/none/neutral"""
    n = clean_name(name)
    if is_neutral(n):
        m = re.match(r'(?:ObjectBank|CMoArray|CGLinkedList|LTList|CMultiLinkList|GenList)<\s*(?:class\s+|struct\s+)?(\w+)', n)
        if m and m.group(1) in class_map:
            us = class_map[m.group(1)]
            tot = sum(us.values())
            best = max(us, key=us.get)
            if us[best] / tot >= 0.6:
                return {best: 0.3}, 'weak'
        return {}, 'neutral'
    keys = [n]
    if n.startswith('_') and '::' not in n:
        keys += [n[1:], n.lstrip('_')]
    if n.endswith("`scalar_deleting_destructor'") or n.endswith("`vector_deleting_destructor'"):
        cls = n.rsplit('::', 1)[0]
        keys = [cls + '::~' + cls.split('::')[-1]]
    cands = collections.defaultdict(float)
    hdr = False
    for k in keys:
        for unit, src, w in defs.get(k, []):
            if unit.startswith('header:'):
                hdr = True
                continue
            cands[unit] = max(cands[unit], w) + (0.05 if cands[unit] else 0)
        if cands:
            break
    last = n.split('::')[-1]
    if cands:
        if len(cands) > 4 or (last in GENERIC and len(cands) > 1):
            return {}, 'neutral'
        k = len(cands)
        if hdr:
            # also defined inline in a header: the out-of-line copy can land in any user's run
            return {u: 0.3 * min(w, 1.0) / k for u, w in cands.items()}, 'weak'
        return {u: min(w, 1.2) / (k ** 0.5) for u, w in cands.items()}, 'strong'
    if hdr or last in GENERIC:
        return {}, 'neutral'
    if '::' in n:
        cls, meth = n.rsplit('::', 1)
        c = cls.split('::')[-1]
        if re.match(r'I(LT|lt)\w+$', c):
            us = iface_units(c)
            if 0 < len(us) <= 4:
                return {u: 0.35 / len(us) for u in us}, 'weak'
            return {}, 'none'
        us = class_map.get(cls) or class_map.get(c)
        if us:
            tot = sum(us.values())
            best = max(us, key=us.get)
            if us[best] / tot >= 0.6:
                return {best: 0.5 * us[best] / tot}, 'weak'
        us = {u: v for u, v in method_map.get(meth, {}).items() if not u.startswith(('lib:', 'crt:'))}
        if 0 < len(us) <= 2:
            return {u: 0.25 / len(us) for u in us}, 'weak'
        return {}, 'none'
    m = re.match(r'([A-Za-z]{1,6}_)', n)
    if m and m.group(1) in prefix_map:
        us = prefix_map[m.group(1)]
        tot = sum(us.values())
        best = max(us, key=us.get)
        if us[best] / tot >= 0.7 and tot >= 3:
            return {best: 0.4 * us[best] / tot}, 'weak'
    return {}, 'none'


def build_maps(defs):
    prefix_map = collections.defaultdict(collections.Counter)
    class_map = collections.defaultdict(collections.Counter)
    method_map = collections.defaultdict(collections.Counter)
    for n, lst in defs.items():
        units = {u for u, s, w in lst if not u.startswith('header:') and w >= 0.7}
        if not units:
            continue
        m = re.match(r'([A-Za-z]{1,6}_)', n)
        if m and '::' not in n:
            for u in units:
                prefix_map[m.group(1)][u] += 1
        if '::' in n:
            cls, meth = n.rsplit('::', 1)
            for u in units:
                class_map[cls][u] += 1
                c = cls.split('::')[-1]
                if c != cls:
                    class_map[c][u] += 1
                method_map[meth][u] += 1
    return prefix_map, class_map, method_map


BACK = 4.0             # cost of a transition that goes backwards in (alphabetical) link order


def is_unknown(s):
    return s == UNK or s.endswith('+')


def unit_rank(u):
    """link-order prior: engine objects alphabetical by base name; library/CRT objects after them, unordered"""
    if u == UNK:
        return (-1, '')
    if u.startswith(('lib:', 'crt:')):
        return (2, '')
    return (1, alpha_key(u))


def make_states(units):
    """state list: every unit plus an 'unknown file after it' state (u + '+')"""
    units = sorted(set(units), key=lambda u: (unit_rank(u), u))
    states = [UNK]
    for u in units:
        states += [u, u + '+']
    return states


def viterbi(emis, n, states, forbid=None):
    S = len(states)
    idx = {s: i for i, s in enumerate(states)}
    # rank group per state: '+' states sort right after their base state
    keys = []
    for s in states:
        base = s[:-1] if s.endswith('+') else s
        r = unit_rank(base)
        keys.append((r, base if r[0] != 2 else '', s.endswith('+')))
    order = sorted(range(S), key=lambda i: keys[i])
    grp = np.zeros(S, dtype=np.int64)
    g = -1
    last = None
    for i in order:
        if keys[i] != last:
            g += 1
            last = keys[i]
        grp[i] = g
    G = g + 1
    is_unk = np.array([s == UNK or s.endswith('+') for s in states])
    score = np.zeros(S)
    score[1:] = -SWITCH
    bp = np.zeros((n, S), dtype=np.int32)
    ar = np.arange(S)
    for i in range(n):
        # best previous state per rank group, then prefix/suffix maxima over groups
        gbest = np.full(G, -np.inf)
        garg = np.zeros(G, dtype=np.int64)
        np.maximum.at(gbest, grp, score)
        # argmax per group
        for_g = np.where(score == gbest[grp])[0]
        garg[grp[for_g]] = for_g
        pre = np.maximum.accumulate(gbest)                      # best over groups <= g
        prearg = garg[np.maximum.accumulate(np.where(gbest == pre, np.arange(G), 0))]
        rg = gbest[::-1]
        rmax = np.maximum.accumulate(rg)
        rarg = np.maximum.accumulate(np.where(rg == rmax, np.arange(G), 0))   # index in reversed array
        sufv = rmax[::-1]                          # best over groups >= g
        sufa = garg[(G - 1 - rarg)[::-1]]
        suf = np.concatenate([sufv[1:], [-np.inf]])   # groups > g
        sufidx = np.concatenate([sufa[1:], [0]])
        fwd = pre[grp] - SWITCH
        bwd = suf[grp] - BACK
        cand = np.stack([score, fwd, bwd])
        ch = np.argmax(cand, axis=0)
        bp[i] = np.where(ch == 0, ar, np.where(ch == 1, prearg[grp], sufidx[grp]))
        score = cand[ch, ar]
        e = emis[i]
        if e:
            pen = e.get(None, 0.0)
            em = np.full(S, -pen)
            em[is_unk] = -pen * 0.999
            for s, v in e.items():
                if s is not None and s in idx:
                    em[idx[s]] = v
            if forbid:
                for s in forbid.get(i, ()):
                    if s in idx:
                        em[idx[s]] = -1e6
            score = score + em
    path = [0] * n
    s = int(np.argmax(score))
    for i in range(n - 1, -1, -1):
        path[i] = s
        s = int(bp[i][s])
    return [states[p] for p in path]


def runs_of(path):
    out = []
    for i, s in enumerate(path):
        if out and out[-1][2] == s:
            out[-1][1] = i + 1
        else:
            out.append([i, i + 1, s])
    return out


def main():
    defs, scan, fp, libs = load()
    funcs = scan['funcs']
    n = len(funcs)
    start2i = {f['addr']: i for i, f in enumerate(funcs)}
    prefix_map, class_map, method_map = build_maps(defs)
    children = collections.defaultdict(set)
    for c, bs in fp.get('bases', {}).items():
        for b in bs:
            children[b].add(c)

    def iface_units(iface):
        seen, todo = set(), [iface]
        while todo:
            x = todo.pop()
            for ch in children.get(x, ()):
                if ch not in seen:
                    seen.add(ch)
                    todo.append(ch)
        us = collections.Counter()
        for c in seen:
            for u, k in class_map.get(c, {}).items():
                if k >= 2 and not u.startswith(('lib:', 'crt:')) and '/linux/' not in u:
                    us[u] += k
        return set(us)

    # ---- EH funclets (.text$x): attach Unwind@ rows to the function owning the handler stub
    stub_owner = {}
    for i, f in enumerate(funcs):
        for t in f['trefs']:
            stub_owner.setdefault(t, i)
    first_x = None
    for i, f in enumerate(funcs):
        if f['name'].startswith('Unwind@') and all(g['name'].startswith('Unwind@') for g in funcs[i:i + 5]):
            first_x = i
            break
    owner = {}
    pending = []
    for i in range(first_x, n):
        pending.append(i)
        for st, fi in funcs[i]['stubs']:
            if st in stub_owner:
                for j in pending:
                    owner[j] = stub_owner[st]
                pending = []
    main_n = first_x

    # ---- emissions
    info = []
    for i, f in enumerate(funcs[:main_n]):
        c, kind = lookup(f['name'], defs, prefix_map, class_map, method_map, iface_units)
        info.append((c, kind))

    # library placements from the byte matcher (config/libraries.json), if available
    lib_at = {}
    lib_status = {}
    if libs and isinstance(libs, dict) and 'units' in libs:
        starts = [f['addr'] for f in funcs[:main_n]]
        for ent in libs['units']:
            lib = ent.get('lib', '')
            obj = ent['name'].split('/')[-1].lower()
            if lib.startswith('VC6') and lib.endswith('LIBCPMT'):
                u = 'crt:libcpmt/' + obj
            elif lib.startswith('VC6'):
                u = 'crt:' + obj
            else:
                u = 'lib:%s/%s' % (lib[3:].lower() if lib.startswith('LT_') else lib.lower(), obj)
            for va, sec in ent.get('sections', {}).items():
                a = int(va, 16)
                size, status, uniq = sec[1], sec[2], sec[3] if len(sec) > 3 else ''
                if status == 'conflict' or (status == 'unverified' and uniq != 'unique'):
                    continue
                k = bisect.bisect_left(starts, a)
                while k < main_n and funcs[k]['addr'] < a + size:
                    lib_at[k] = u
                    lib_status[k] = status
                    k += 1
    def emissions(extra=None):
        em = []
        for i in range(main_n):
            c, kind = info[i]
            e = {}
            if i in lib_at:
                e = {lib_at[i]: 3.0, None: 3.0}
            elif c:
                e = dict(c)
                e[None] = max(c.values())
            if i in boost:
                e = dict(e)
                e[boost[i]] = e.get(boost[i], 0) + 3.0
                e.setdefault(None, 0.0)
            if extra and i in extra and i not in lib_at:
                for u, v in extra[i].items():
                    e[u] = e.get(u, 0) + v
                e.setdefault(None, 0.0)
            em.append(e)
        return em

    allunits = {u for c, k in info for u in c} | set(lib_at.values())
    for c in fp['files']:
        pass
    states = make_states(allunits)
    forbid = collections.defaultdict(set)
    boost = {}
    extra = None
    for it in range(6):
        em = emissions(extra)
        path = viterbi(em, main_n, states, forbid if it >= 3 else None)
        rs = runs_of(path)
        # a unit must be contiguous: keep its run with the most evidence, forbid the others
        ev = collections.defaultdict(list)
        for r in rs:
            if is_unknown(r[2]):
                continue
            score = sum(em[i].get(r[2], 0) for i in range(r[0], r[1]))
            ev[r[2]].append((score, r))
        changed = False
        for u, lst in (ev.items() if it >= 2 else ()):
            if len(lst) < 2:
                continue
            # runs of the same unit separated by a short interloper: absorb the interloper (inline/COMDAT copy)
            byaddr = sorted(lst, key=lambda x: x[1][0])
            for (s1, r1), (s2, r2) in zip(byaddr, byaddr[1:]):
                if r2[0] - r1[1] <= 4:
                    for i in range(r1[1], r2[0]):
                        if boost.get(i) != u:
                            boost[i] = u
                            changed = True
            lst = [x for x in lst if not any(boost.get(i) == u for i in range(x[1][0] - 4, x[1][0]))] or lst
            lst.sort(key=lambda x: -x[0])
            for sc, r in lst[1:]:
                if any(boost.get(i) == u for i in range(r[0] - 4, r[1] + 4)):
                    continue
                for i in range(r[0], r[1]):
                    if u not in forbid[i]:
                        forbid[i].add(u)
                        changed = True
        # weak evidence for evidence-less functions from callers/callees and data neighbourhood
        extra = weak_evidence(funcs, main_n, path, info, scan)
        if not changed and it >= 2:
            break

    path = viterbi(emissions(extra), main_n, states, forbid)
    result = finalize(funcs, main_n, path, info, extra, owner, defs, fp, scan, forbid, lib_at)
    return result


NEAR = 60


def weak_evidence(funcs, main_n, path, info, scan):
    """call-graph + data-reference evidence for functions without name evidence.
    Only units with an agreeing strong function within NEAR functions are eligible."""
    start2i = {f['addr']: i for i, f in enumerate(funcs)}
    good = [info[i][1] == 'strong' and not is_unknown(path[i]) and path[i] in info[i][0] for i in range(main_n)]
    unit_pos = collections.defaultdict(list)
    for i in range(main_n):
        if good[i]:
            unit_pos[path[i]].append(i)

    def near(u, i):
        ps = unit_pos.get(u)
        if not ps:
            return False
        k = bisect.bisect_left(ps, i)
        return any(0 <= kk < len(ps) and abs(ps[kk] - i) <= NEAR for kk in (k - 1, k))

    callers = collections.defaultdict(set)
    for i in range(main_n):
        for t in funcs[i]['calls'] + funcs[i]['trefs']:
            j = start2i.get(t)
            if j is not None and j < main_n:
                callers[j].add(i)
    # data address -> units of agreeing strong functions referencing it
    dref_units = collections.defaultdict(collections.Counter)
    for i in range(main_n):
        if good[i]:
            for d in funcs[i]['drefs']:
                dref_units[d][path[i]] += 1
    excl = sorted((d, next(iter(c))) for d, c in dref_units.items() if len(c) == 1)
    exaddr = [d for d, u in excl]
    extra = {}
    for i in range(main_n):
        if info[i][1] == 'strong':
            continue
        e = collections.Counter()
        cs = [path[j] for j in callers[i] if good[j]]
        for u in cs:
            e[u] += 0.3 / len(cs) * min(len(cs), 3)
        for t in funcs[i]['calls']:
            j = start2i.get(t)
            if j is not None and j < main_n and good[j]:
                e[path[j]] += 0.15
        for d in funcs[i]['drefs']:
            c = dref_units.get(d)
            if c and len(c) == 1:
                e[next(iter(c))] += 0.4
            elif not c:
                # sandwiched between exclusive data of one unit (data is grouped per object too)
                k = bisect.bisect_left(exaddr, d)
                if 0 < k < len(exaddr) and excl[k - 1][1] == excl[k][1] and exaddr[k] - exaddr[k - 1] <= 0x800:
                    e[excl[k][1]] += 0.25
        e = {u: min(v, 0.8) for u, v in e.items() if near(u, i)}
        if e:
            extra[i] = e
    return extra


def alpha_key(u):
    b = u.split('/')[-1]
    return os.path.splitext(b)[0].lower()


GAP_MAX_FUNCS = 6       # evidence-less gaps up to this size go to the preceding unit
GAP_MAX_BYTES = 0x300


def finalize(funcs, main_n, path, info, extra, owner, defs, fp, scan, forbid, lib_at=None):
    lib_at = lib_at or {}

    def supports(i, u):
        return lib_at.get(i) == u or (info[i][0] and u in info[i][0]) or (extra and i in extra and u in extra[i])

    path = [UNK if is_unknown(s) else s for s in path]
    rs = runs_of(path)
    # evidence positions per run
    segs = []
    for a, b, u in rs:
        ev = [i for i in range(a, b) if not is_unknown(u) and supports(i, u)]
        segs.append(dict(a=a, b=b, unit=u, ev=ev))
    # re-cut every boundary: functions between the last evidence of one unit and the first of the next
    rows = []
    known = [s for s in segs if s['ev']]
    boundaries = []
    cur = 0
    out = []
    for k, s in enumerate(known):
        first, last = s['ev'][0], s['ev'][-1]
        if first > cur:
            gap = (cur, first)
            nb = funcs[first]['addr'] - funcs[cur]['addr']
            ng = first - cur
            prev = out[-1] if out else None
            if prev is not None and (ng <= GAP_MAX_FUNCS or nb <= GAP_MAX_BYTES):
                prev['b'] = first
                prev['gap_after'] = ng
                s['gap_before'] = ng
            else:
                out.append(dict(a=cur, b=first, unit=None, ev=[], gap_after=0, gap_before=0,
                                prev=prev['unit'] if prev else None, next=s['unit']))
                if prev is not None:
                    prev['gap_after'] = -1
                s['gap_before'] = -1
        out.append(dict(a=first, b=last + 1, unit=s['unit'], ev=s['ev'], gap_after=0,
                        gap_before=s.get('gap_before', 0)))
        cur = last + 1
    if cur < main_n:
        out.append(dict(a=cur, b=main_n, unit=None, ev=[], gap_after=0, gap_before=0,
                        prev=out[-1]['unit'] if out else None, next=None))
    # merge adjacent rows of the same unit (unknown separated nothing)
    merged = []
    for r in out:
        if merged and r['unit'] is not None and merged[-1]['unit'] == r['unit']:
            merged[-1]['b'] = r['b']
            merged[-1]['ev'] += r['ev']
            merged[-1]['gap_after'] = r['gap_after']
        else:
            merged.append(r)
    for k, r in enumerate(merged):
        a, b, u = r['a'], r['b'], r['unit']
        r['named'] = sum(1 for i in range(a, b) if not funcs[i]['name'].startswith(('FUN_', 'Unwind@', 'Catch@')))
        if u is None:
            r['unit'] = 'unknown_%08x' % funcs[a]['addr']
            r.update(strong=[], weak=[], x=[], contra=[i for i in range(a, b) if info[i][1] == 'strong'])
            r['conf'] = 'low'
            r['is_unknown'] = True
            continue
        r['is_unknown'] = False
        r['strong'] = [i for i in range(a, b) if info[i][1] == 'strong' and u in info[i][0]]
        r['weak'] = [i for i in range(a, b) if info[i][0] and u in info[i][0] and info[i][1] != 'strong']
        r['x'] = [i for i in range(a, b) if extra and i in extra and u in extra[i] and i not in r['strong']]
        r['libmatch'] = [i for i in range(a, b) if lib_at.get(i) == u]
        r['contra'] = [i for i in range(a, b) if info[i][1] == 'strong' and u not in info[i][0] and lib_at.get(i) != u]
        ns = len(r['strong'])
        if r['libmatch']:
            r['conf'] = 'high' if len(r['libmatch']) * 2 >= (b - a) or len(r['libmatch']) >= 3 else 'medium'
            continue
        ambiguous = r['gap_before'] != 0 or r['gap_after'] != 0
        if ns >= 2 and not r['contra'] and not ambiguous:
            r['conf'] = 'high'
        elif ns >= 2 and not r['contra'] and max(r['gap_before'], r['gap_after']) <= 2 and min(r['gap_before'], r['gap_after']) >= 0:
            r['conf'] = 'high'
        elif ns >= 1 and len(r['contra']) <= max(1, ns // 4):
            r['conf'] = 'medium'
        else:
            r['conf'] = 'low'
    return dict(rows=merged, funcs=funcs, main_n=main_n, owner=owner, path=path, info=info, extra=extra, fp=fp,
                scan=scan, defs=defs)


if __name__ == '__main__':
    import units_report
    units_report.write(main())
