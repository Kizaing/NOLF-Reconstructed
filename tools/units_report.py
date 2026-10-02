"""Write config/units_proposal.csv / .md from units_recover.main() results."""
import os, re, csv, json, bisect, collections, statistics

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = os.path.join(HERE, '..', 'config')
B = os.path.join(HERE, '..', 'build', 'units')
ROOT = r'E:\AVP2Source'


def alpha_key(u):
    return os.path.splitext(u.split('/')[-1])[0].lower()


def is_lib(u):
    return u.startswith(('lib:', 'crt:'))


def jupiter_map_order():
    p = os.path.join(ROOT, r'jupiter\runtime\winbuild\lithtech\Lithtech.map')
    objs = {}
    for l in open(p, encoding='latin-1'):
        m = re.match(r'\s*0001:([0-9a-f]{8})\s+\S+\s+[0-9a-f]{8}\s+f\s+(i\s+)?(\S+)\s*$', l)
        if not m or m.group(2):
            continue
        off = int(m.group(1), 16)
        if off >= 0x1f2520:
            continue
        o = m.group(3).split(':')[-1].lower().replace('.obj', '')
        objs.setdefault(o, []).append(off)
    return sorted(objs, key=lambda o: statistics.median(objs[o]))


def lis_indices(seq):
    """indices of one longest non-decreasing subsequence"""
    tails, tidx, prev = [], [], [None] * len(seq)
    for i, x in enumerate(seq):
        k = bisect.bisect_right(tails, x)
        if k == len(tails):
            tails.append(x)
            tidx.append(i)
        else:
            tails[k] = x
            tidx[k] = i
        prev[i] = tidx[k - 1] if k else None
    out, i = [], tidx[-1] if tidx else None
    while i is not None:
        out.append(i)
        i = prev[i]
    return set(out)


def data_check(res, rows):
    """Place each unknown block's exclusively-referenced data relative to the neighbouring units' data
    (.rdata/.data are grouped per object in link order too). Inside the previous/next unit's data -> merge."""
    funcs, info = res['funcs'], res['info']
    refs = []
    owners = collections.defaultdict(set)
    for k, r in enumerate(rows):
        s = set()
        for i in range(r['a'], r['b']):
            s.update(funcs[i]['drefs'])
        refs.append(s)
        for d in s:
            owners[d].add(k)
    excl = sorted((d, next(iter(o))) for d, o in owners.items() if len(o) == 1 and not rows[next(iter(o))]['is_unknown'])
    ea = [d for d, k in excl]
    for k, r in enumerate(rows):
        if not r['is_unknown']:
            continue
        pk = k - 1 if k and not rows[k - 1]['is_unknown'] else None
        nk = k + 1 if k + 1 < len(rows) and not rows[k + 1]['is_unknown'] else None
        votes = collections.Counter()
        for d in refs[k]:
            if len(owners[d]) != 1:
                continue
            j = bisect.bisect_left(ea, d)
            lo = excl[j - 1][1] if j else None
            hi = excl[j][1] if j < len(excl) else None
            if lo is not None and lo == hi and lo in (pk, nk):
                votes['prev' if lo == pk else 'next'] += 1
            elif lo is not None and hi is not None and k - 3 <= lo < k < hi <= k + 3:
                votes['between'] += 1      # sits between the neighbours' data: a separate object here
            else:
                votes['elsewhere'] += 1    # data sits among unrelated units' data
        r['data_votes'] = dict(votes)
        tot = sum(votes.values())
        if tot >= 2:
            best, c = votes.most_common(1)[0]
            if c / tot >= 0.75 and best in ('prev', 'next'):
                r['merge'] = best
    # a tiny unknown block at the very start (compiler helper COMDATs) belongs to the first object
    if rows and rows[0]['is_unknown'] and rows[0]['b'] - rows[0]['a'] <= 3 and len(rows) > 1:
        rows[0]['merge'] = 'next'
    # apply merges
    out = []
    for k, r in enumerate(rows):
        if r.get('merge') == 'prev' and out and not out[-1]['is_unknown']:
            p = out[-1]
            p['b'] = r['b']
            p['gap_after'] = r['b'] - r['a']
            p['data_merged'] = p.get('data_merged', 0) + (r['b'] - r['a'])
            if p['conf'] == 'high':
                p['conf'] = 'medium'
            continue
        out.append(r)
    out2 = []
    for k in range(len(out) - 1, -1, -1):
        r = out[k]
        if r.get('merge') == 'next' and out2 and not out2[-1]['is_unknown'] and r['is_unknown']:
            nx = out2[-1]
            nx['a'] = r['a']
            nx['gap_before'] = r['b'] - r['a']
            nx['data_merged'] = nx.get('data_merged', 0) + (r['b'] - r['a'])
            nx['merged_note'] = 'leading compiler-helper COMDATs' if r['a'] == 0 else 'data position'
            if nx['conf'] == 'high':
                nx['conf'] = 'medium'
            continue
        out2.append(r)
    out2.reverse()
    for r in out2:
        if r['is_unknown']:
            r['contra'] = [i for i in range(r['a'], r['b']) if info[i][1] == 'strong']
        else:
            u = r['unit']
            lm = set(r.get('libmatch', []))
            r['contra'] = [i for i in range(r['a'], r['b']) if info[i][1] == 'strong' and u not in info[i][0] and i not in lm]
    return out2


def write(res):
    funcs, rows, info, extra = res['funcs'], res['rows'], res['info'], res['extra']
    main_n, owner, fp = res['main_n'], res['owner'], res['fp']
    n = len(funcs)
    fname = lambda i: funcs[i]['name']

    # ---------------- data-position check for unknown blocks
    rows = data_check(res, rows)
    res['rows'] = rows

    # ---------------- engine-named units placed in the library region: use the library object of the same name
    libobjs = {}
    for dp in os.listdir(os.path.join(ROOT, 'libs_objs')):
        if dp.startswith('LT_'):
            for f in os.listdir(os.path.join(ROOT, 'libs_objs', dp)):
                if f.lower().endswith('.obj'):
                    libobjs.setdefault(f[:-4].lower(), 'lib:%s/%s' % (dp[3:].lower(), f[:-4].lower()))
    first_lib_k = next((k for k, r in enumerate(rows) if is_lib(r['unit'])), None)
    if first_lib_k is not None:
        for r in rows[first_lib_k:]:
            if not r['is_unknown'] and not is_lib(r['unit']) and alpha_key(r['unit']) in libobjs:
                r['renamed_from'] = r['unit']
                r['unit'] = libobjs[alpha_key(r['unit'])]

    # ---------------- EH funclet tail rows
    row_of = {}
    for k, r in enumerate(rows):
        for i in range(r['a'], r['b']):
            row_of[i] = k
    tail = []
    for i in range(main_n, n):
        o = owner.get(i)
        u = rows[row_of[o]]['unit'] if o is not None and o in row_of else None
        if tail and tail[-1]['unit'] == u:
            tail[-1]['b'] = i + 1
        else:
            tail.append(dict(a=i, b=i + 1, unit=u, owners=set()))
        if o is not None:
            tail[-1]['owners'].add(o)
    for t in tail:
        if t['unit'] is None:
            t['unit'] = 'unknown_%08x' % funcs[t['a']]['addr']
            t['conf'] = 'low'
            t['is_unknown'] = True
        else:
            t['conf'] = 'high' if rows[row_of[next(iter(t['owners']))]]['conf'] != 'low' else 'medium'
            t['is_unknown'] = t['unit'].startswith('unknown_')
            if t['is_unknown']:
                t['conf'] = 'low'

    # ---------------- unknown-row hints (alphabetical slot)
    all_engine = sorted({alpha_key(u): u for u in fp['objunit'].values() if not is_lib(u)}.items())
    used = {alpha_key(r['unit']) for r in rows if not r['is_unknown'] and not is_lib(r['unit'])}
    for k, r in enumerate(rows):
        if not r['is_unknown']:
            continue
        prev = next((rows[j]['unit'] for j in range(k - 1, -1, -1) if not rows[j]['is_unknown']), None)
        nxt = next((rows[j]['unit'] for j in range(k + 1, len(rows)) if not rows[j]['is_unknown']), None)
        r['prev'], r['next'] = prev, nxt
        slot = []
        if prev and nxt and not is_lib(prev) and not is_lib(nxt):
            lo, hi = alpha_key(prev), alpha_key(nxt)
            if lo < hi:
                slot = [u for kk, u in all_engine if lo < kk < hi and kk not in used]
        r['slot'] = slot
        cls = collections.Counter()
        for i in range(r['a'], r['b']):
            nm = fname(i)
            if nm.startswith(('FUN_', 'Unwind@', 'Catch@', 'thunk_')):
                continue
            cls[nm.split('::')[0].split('<')[0] if '::' in nm or '<' in nm else (re.match(r'[A-Za-z]{1,6}_', nm) or re.match(r'.*', nm)).group(0)] += 1
        r['names'] = cls.most_common(5)

    # ---------------- evidence text
    def evid(r):
        if r.get('is_unknown') and 'slot' in r:
            s = 'no name evidence; between %s and %s' % (r.get('prev'), r.get('next'))
            if r['slot']:
                s += '; unassigned Jupiter files in alpha slot: ' + ' '.join(alpha_key(u) for u in r['slot'][:6])
            if r['names']:
                s += '; names: ' + ' '.join('%s(%d)' % x for x in r['names'])
            if r.get('data_votes'):
                s += '; data position: ' + ' '.join('%s=%d' % kv for kv in sorted(r['data_votes'].items()))
            return s
        if 'owners' in r:
            return 'EH funclets (.text$x) owned by %d function(s) of this unit' % len(r['owners'])
        parts = ['%d strong names' % len(r['strong'])]
        if r.get('libmatch'):
            parts.insert(0, '%d funcs byte-matched to the library object (libraries.json)' % len(r['libmatch']))
        if r['weak']:
            parts.append('%d weak (class/iface/prefix)' % len(r['weak']))
        if r['x']:
            parts.append('%d call/data xrefs' % len(r['x']))
        if r['contra']:
            parts.append('%d contra (%s)' % (len(r['contra']), ', '.join(fname(i) for i in r['contra'][:2])))
        g = []
        if r['gap_before'] > 0:
            g.append('start ambiguous by %d unnamed funcs' % r['gap_before'])
        if r['gap_after'] > 0:
            g.append('end ambiguous by %d unnamed funcs' % r['gap_after'])
        if r['gap_before'] < 0:
            g.append('preceded by unknown block')
        if r['gap_after'] < 0:
            g.append('followed by unknown block')
        if r.get('renamed_from'):
            g.append('Jupiter has it as %s; here it is linked from the prebuilt library' % r['renamed_from'])
        if r.get('data_merged'):
            g.append('%d funcs merged in (%s)' % (r['data_merged'], r.get('merged_note', 'data position')))
        return '; '.join(parts + g)

    allrows = [dict(r, kind='text') for r in rows] + [dict(t, kind='text$x') for t in tail]
    out = []
    for r in allrows:
        a, b = r['a'], r['b']
        named = sum(1 for i in range(a, b) if not fname(i).startswith(('FUN_', 'Unwind@', 'Catch@')))
        out.append(dict(unit=r['unit'], start=funcs[a]['addr'], end=funcs[b - 1]['end'], nfuncs=b - a,
                        named_funcs=named, confidence=r['conf'], evidence=evid(r), r=r))
    out.sort(key=lambda x: x['start'])
    # sanity: every function exactly once
    cover = collections.Counter()
    for o in out:
        for i in range(o['r']['a'], o['r']['b']):
            cover[i] += 1
    assert len(cover) == n and max(cover.values()) == 1, 'coverage error'

    with open(os.path.join(CFG, 'units_proposal.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['unit', 'start', 'end', 'nfuncs', 'named_funcs', 'confidence', 'evidence'])
        for o in out:
            w.writerow([o['unit'], '%08x' % o['start'], '%08x' % o['end'], o['nfuncs'], o['named_funcs'],
                        o['confidence'], o['evidence']])

    # ---------------- validation
    val = {}
    q = [o for o in out if o['unit'] == 'sdk/ltquatbase.cpp' and o['r']['kind'] == 'text']
    val['ltquatbase'] = ['%08x-%08x' % (o['start'], o['end']) for o in q]
    contra = []
    for o in out:
        r = o['r']
        if r['kind'] != 'text':
            continue
        for i in r.get('contra', []):
            files = sorted(info[i][0], key=lambda u: -info[i][0][u])
            contra.append((funcs[i]['addr'], fname(i), files[0] if files else '?', o['unit']))
    nstrong = sum(1 for i in range(main_n) if info[i][1] == 'strong')
    pairs = collections.Counter((c[2], c[3]) for c in contra)
    cov = collections.Counter()
    for o in out:
        cat = 'unknown' if o['unit'].startswith('unknown_') else o['confidence']
        cov[cat] += sum(funcs[i]['end'] - funcs[i]['addr'] for i in range(o['r']['a'], o['r']['b']))
    tot = sum(cov.values())

    # ---------------- link order
    text_known = [o for o in out if o['r']['kind'] == 'text' and not o['unit'].startswith('unknown_')]
    engine = [o for o in text_known if not is_lib(o['unit'])]
    keys = [alpha_key(o['unit']) for o in engine]
    lis = lis_indices(keys)
    anomalies = [(engine[i], engine[i - 1]['unit'] if i else None, engine[i + 1]['unit'] if i + 1 < len(engine) else None)
                 for i in range(len(engine)) if i not in lis]
    jorder = jupiter_map_order()
    jpos = {o: k for k, o in enumerate(jorder)}
    common = [(jpos[alpha_key(o['unit'])], o) for o in text_known if alpha_key(o['unit']) in jpos]
    inv = sum(1 for x in range(len(common)) for y in range(x + 1, len(common)) if common[x][0] > common[y][0])
    npairs = len(common) * (len(common) - 1) // 2
    first_lib = next((o for o in text_known if is_lib(o['unit'])), None)

    # ---------------- data corroboration
    dsec = res['scan']['sections']
    rlo, rhi = dsec['.rdata']
    dlo, dhi = dsec['.data']
    refs_by_row = []
    owner_rows = collections.defaultdict(set)
    for k, o in enumerate(out):
        s = set()
        for i in range(o['r']['a'], o['r']['b']):
            s.update(funcs[i]['drefs'])
        refs_by_row.append(s)
        for d in s:
            owner_rows[d].add(k)
    stats = {}
    import pefile
    pe = pefile.PE(os.path.join(ROOT, 'bin', 'lithtech.exe'), fast_load=True)
    ds = [s for s in pe.sections if s.Name.startswith(b'.data')][0]
    init_end = 0x400000 + ds.VirtualAddress + ds.SizeOfRawData
    for name, lo, hi in (('.rdata', rlo + 0x400, rhi), ('.data (initialised)', dlo, init_end), ('.bss part of .data', init_end, dhi)):
        med = []
        spans = []
        for k, o in enumerate(out):
            if o['r']['kind'] != 'text' or o['unit'].startswith('unknown_'):
                continue
            ex = sorted(d for d in refs_by_row[k] if lo <= d < hi and len(owner_rows[d]) == 1)
            if len(ex) >= 2:
                med.append((o['start'], statistics.median(ex), o['unit']))
                spans.append((ex[0], ex[-1], o['unit'], len(ex)))
        seq = [m for s, m, u in med]
        inv_d = sum(1 for x in range(len(seq)) for y in range(x + 1, len(seq)) if seq[x] > seq[y])
        tp = len(seq) * (len(seq) - 1) // 2
        # how many units' exclusive data spans overlap another unit's span
        spans.sort()
        ov = 0
        for x in range(len(spans)):
            if (x and spans[x][0] <= spans[x - 1][1]) or (x + 1 < len(spans) and spans[x + 1][0] <= spans[x][1]):
                ov += 1
        stats[name] = dict(units=len(seq), inversions=inv_d, pairs=tp, overlapping_spans=ov)

    json.dump(dict(validation=val, coverage=cov, contra=len(contra), nstrong=nstrong,
                   anomalies=[(a['unit'], '%08x' % a['start'], p, q2) for a, p, q2 in anomalies],
                   data=stats, jupiter_inversions=[inv, npairs]),
              open(os.path.join(B, 'summary.json'), 'w'), indent=1, default=str)

    # ---------------- markdown
    L = []
    P = L.append
    P('# lithtech.exe source-unit recovery (proposal)\n')
    P('Generated by `tools/units_defs.py` + `tools/units_scan.py` + `tools/units_recover.py` (report: `tools/units_report.py`).')
    P('Rows: %d (%d .text units incl. %d unknown blocks, %d `.text$x` EH-funclet rows). Functions: %d.\n' % (
        len(out), sum(1 for o in out if o['r']['kind'] == 'text'),
        sum(1 for o in out if o['r']['kind'] == 'text' and o['unit'].startswith('unknown_')),
        sum(1 for o in out if o['r']['kind'] == 'text$x'), n))
    P('## Method\n')
    P('''1. **Definition database** (`units_defs.py` -> `build/units/defs.json`): every function name mapped to the file(s) defining it, from
   - a brace/scope-aware C/C++ definition finder over `jupiter/runtime`, `jupiter/sdk`, `jupiter/libs`, `build/proj/LT2/lithshared` and the LT2 SDK
     (qualified `Class::Method`, free functions, statics, in-class bodies; header definitions are kept but marked inline/neutral; linux/null/toport variants down-weighted so `sys/win` wins),
   - the original Monolith Jupiter `Lithtech.map` (2006; publics -> object, non-inline entries only),
   - the VS2010 Jupiter PDBs `bin/jupiter/Lithtech.pdb` and `Server.pdb`, read with a small MSF/DBI parser (`tools/pdbmods.py`: module list, section contributions, S_GPROC32/S_LPROC32 per module, so statics are included),
   - COFF symbols of the library objects in `libs_objs` (SP5 LIBCMT/LIBCPMT for the CRT, LT_* for Lithtech libs),
   - `config/libraries.json` from the library byte matcher (verified/exact/unique section placements; conflicts and constrained-unverified ones ignored) -> those functions are pinned to their library object with a large emission,
   - class inheritance (`class X : public ILTFoo`) so Ghidra's interface-slot names (`ILTClient::Foo`) map weakly to the implementing classes' files.
2. **Code scan** (`units_scan.py`): capstone over every function: direct call targets, immediate/displacement references into .rdata/.data and .text, EH handler stubs (`mov eax, FuncInfo; jmp`).
3. **Emissions**: a named function gets its candidate file(s) (strong: direct definition; weak: class-majority, interface implementers, `prefix_` majority, template-argument class, unique method name). Generic/STL/template/compiler names are neutral.
   Function-level `/Gy` COMDATs (inline/template copies) are neutral because the linker places them inside whichever object first pulled them in.
4. **Ordered Viterbi segmentation** over the function sequence: switching unit costs 0.75, a named function inside a foreign run costs its weight,
   and a transition that goes *backwards* in the link-order prior costs 4.0. The prior is: engine objects in alphabetical order of their base name
   (observed in Talon, and the same rule as the original Jupiter link), then library/CRT objects (unordered). Each unit also has an "unknown file after it" state.
   Runs that repeat a unit are forbidden outside the best run and the segmentation is redone (contiguity), then call-graph and data evidence
   (callers/callees in a unit; references to data used exclusively by one unit, or sandwiched between two exclusive data items of the same unit) is added for unnamed functions and the segmentation is redone (6 rounds).
5. **Boundary cut**: each unit spans from its first to its last evidence function. Evidence-less functions between two units go to the preceding unit
   (trailing statics/COMDAT helpers; validated on ltquatbase.cpp) when the gap is <= %d functions or <= 0x%x bytes; larger gaps become `unknown_<start>` rows.
6. **EH funclets**: `Unwind@` rows (VC6 `.text$x`, all at the end of .text) are attached to the function whose prologue pushes the handler stub found in that row, and grouped per unit (same link order).
7. **Confidence**: high = >=2 strong names, no contradiction, boundaries tight (<=2 ambiguous unnamed functions); medium = >=1 strong name and few contradictions; low = weak-only evidence, many contradictions, or unknown. Library rows: high when >=3 or >=half of their functions are byte-matched in libraries.json. A `.text$x` row is high/medium when its owner rows are not low.

Note: lithtech.exe has **no `__FILE__`/assert strings** (release build; a search for `*.cpp/.c/.h` strings found none), so evidence source 3 contributed nothing.
''' % (res.get('gap_funcs', 6), res.get('gap_bytes', 0x300)))
    P('## Validation\n')
    P('- **ltquatbase.cpp**: %s (expected 0044cc80-0044d0c0) -> %s' % (
        ', '.join(val['ltquatbase']) or 'not found', 'OK' if val['ltquatbase'] == ['0044cc80-0044d0c0'] else 'MISMATCH'))
    P('- **Contradictions**: %d of %d strongly-named functions (%.1f%%) lie inside a run of a different unit (or in an unknown block).' % (
        len(contra), nstrong, 100.0 * len(contra) / max(1, nstrong)))
    P('  Most frequent (attributed file -> run unit):\n')
    P('| count | name attributed to | found inside run |')
    P('|---:|---|---|')
    for (a, b2), c in pairs.most_common(20):
        P('| %d | %s | %s |' % (c, a, b2))
    P('\n  Individual contradictions (first 40 by address):\n')
    P('| addr | name | attributed to | run |')
    P('|---|---|---|---|')
    for c in sorted(contra)[:40]:
        P('| %08x | `%s` | %s | %s |' % c)
    P('\n- **Coverage** (function bytes):\n')
    P('| confidence | bytes | % |')
    P('|---|---:|---:|')
    for k in ('high', 'medium', 'low', 'unknown'):
        P('| %s | 0x%x (%d) | %.1f |' % (k, cov[k], cov[k], 100.0 * cov[k] / tot))
    P('| total | 0x%x | 100 |\n' % tot)

    P('## Link order\n')
    P('Talon .text (engine objects): alphabetical by object base name, %d of %d recovered engine units in alphabetical order (longest non-decreasing run); '
      'then libraries starting at %s (%08x), then the CRT, then lith basehash/virtlist after the CRT, then `.text$x` EH funclets.' % (
          len(lis), len(engine), first_lib['unit'] if first_lib else '?', first_lib['start'] if first_lib else 0))
    P('EH funclets: only library objects (WONAPI, RezMgr, StdLith, ControlFileMgr, Lith basehash) own `.text$x` funclets; no engine object does, '
      'and their order in `.text$x` repeats the library order of `.text` (independent corroboration of the library cut).' + chr(10))
    P('Compared with the original Jupiter link (Lithtech.map, 2006): %d units in common, %d of %d pairs inverted (%.1f%%). '
      'Jupiter is also alphabetical for the bulk of the engine but appends later-added folders (world_*, *_ilt*, icommandlineargs) and puts Lib_UI/StdLith/D3DRender/RezMgr libraries in a different order.\n' % (
          len(common), inv, npairs, 100.0 * inv / max(1, npairs)))
    P('Order anomalies (engine units outside the alphabetical chain; the Talon file name probably differs, or the unit is mis-labelled):\n')
    P('| start | unit | after | before |')
    P('|---|---|---|---|')
    for a, p, q2 in anomalies:
        P('| %08x | %s | %s | %s |' % (a['start'], a['unit'], p, q2))
    P('\nInferred order (.text):\n')
    P('```')
    for o in out:
        if o['r']['kind'] == 'text':
            P('%08x-%08x %-6s %s' % (o['start'], o['end'], o['confidence'], o['unit']))
    P('```\n')

    P('## Boundaries that need a human\n')
    P('Unknown blocks (no name evidence; the alpha slot lists Jupiter files that sort between the neighbours and were not found elsewhere):\n')
    P('| start | end | nfuncs | between | candidates / names |')
    P('|---|---|---:|---|---|')
    for o in out:
        r = o['r']
        if r['kind'] == 'text' and o['unit'].startswith('unknown_'):
            P('| %08x | %08x | %d | %s .. %s | %s %s |' % (o['start'], o['end'], o['nfuncs'], r.get('prev'), r.get('next'),
                                                       ' '.join(alpha_key(u) for u in r.get('slot', [])[:6]),
                                                       ' '.join('%s(%d)' % x for x in r.get('names', []))))
    P('\nAmbiguous edges (unnamed functions assigned to the preceding unit by default, >=3 functions):\n')
    P('| boundary at | previous unit | next unit | unnamed funcs in gap |')
    P('|---|---|---|---:|')
    tx = [o for o in out if o['r']['kind'] == 'text']
    for k in range(1, len(tx)):
        g = tx[k]['r'].get('gap_before', 0)
        if g and g >= 3:
            P('| %08x | %s | %s | %d |' % (tx[k]['start'], tx[k - 1]['unit'], tx[k]['unit'], g))
    P('\nLow-confidence named units:\n')
    P('| start | unit | evidence |')
    P('|---|---|---|')
    for o in tx:
        if o['confidence'] == 'low' and not o['unit'].startswith('unknown_'):
            P('| %08x | %s | %s |' % (o['start'], o['unit'], o['evidence']))

    P('\n## Data-section corroboration\n')
    for name, s in stats.items():
        P('- **%s**: %d units with >=2 exclusively-referenced items; ordering of their median data address vs .text order: %d of %d pairs inverted (%.1f%%); '
          '%d units have an exclusive-data span overlapping another unit\'s span.' % (
              name, s['units'], s['inversions'], s['pairs'], 100.0 * s['inversions'] / max(1, s['pairs']), s['overlapping_spans']))
    P('''
Interpretation: if per-object grouping of .rdata/.data followed link order exactly, inversions would be ~0% and spans would not overlap.
The measured values say how far data grouping corroborates the .text cut; the data "sandwich" rule is also used as weak evidence for unnamed functions
(an unnamed function referencing data that lies between two items used only by unit X is pulled towards X).
''')
    P('## Per-unit stats\n')
    P('| start | end | unit | funcs | named | conf | evidence |')
    P('|---|---|---|---:|---:|---|---|')
    for o in out:
        P('| %08x | %08x | %s | %d | %d | %s | %s |' % (o['start'], o['end'], o['unit'], o['nfuncs'], o['named_funcs'],
                                                     o['confidence'], o['evidence'].replace('|', '/')))
    open(os.path.join(CFG, 'units_proposal.md'), 'w', encoding='utf-8').write('\n'.join(L) + '\n')
    print('rows', len(out), 'coverage', dict(cov), 'contra', len(contra), '/', nstrong, 'ltquatbase', val['ltquatbase'])
    print('data', stats, 'jupiter inversions', inv, npairs, 'alpha', len(lis), len(engine))
