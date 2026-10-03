r"""VC6 inline-budget model and oracle (wave 7).

  python tools/inline_budget.py <function name or hex address> [options]
      -v          print the whole site tree (default: top level + every site whose decision differs)
      --exe       compare the predicted out-of-line calls with the exe's (default on for annotated functions)
      --sweep     find the budget range for which the model reproduces the exe's out-of-line calls
      --no-cost   don't measure callee costs (only B(F) and the tree)
      -j N        parallel compiles (default 6)
      --alias VA=MANGLED   name an exe address no matched function has named yet (repeatable)
      --cost NAME=U        what-if: use cost U for a callee (mangled or undecorated name; repeatable)
  python tools/inline_budget.py --validate [names...]
      run the model on matched functions (default: the README's inline cases) and score its predictions
  python tools/inline_budget.py --variants <function> <variants.py> [--sweep]
      score vtry-style source variants in memory (the source file is never touched): B, size, model and build
      out-of-line calls vs the exe
  python tools/inline_budget.py --at <function> "<anchor text>"
      remaining top-level budget just before <anchor text> inside the function (probe inserted there; the probe is
      one more pending site for everything before it, so nested shares before the anchor shrink a little)

How it works (all measurements use the real compiler on a temporary copy of the unit, written next to it as
src/<dir>/__ib_*.ibtmp (compiled with /Tp) and deleted afterwards; the unit itself is never edited):
  B(F)    an inline probe made of global stores is put first in F; the largest probe that still inlines is B(F)
          (exact to 1u). size(F) = B/2 - 4 (the probe call) when B > 1000.
  tree    the unit is compiled /Od /Ob0 /FAs: every call of an inline candidate (a COMDAT function) is a site;
          the out-of-line copies give the nested sites. /Od keeps the front end's evaluation order.
  costs   for each callee, a wrapper `inline void R() { <ballast>; <call>; }` called from a function with a known
          budget: the largest ballast at which the callee still inlines gives cost(callee) (exact to ~1u). The
          call expression is built from the undecorated signature; costs are cached in build/inline_costs.json.
  model   the rules below, replayed over the tree; compared with our /O2 build and with the exe.

THE MODEL (measured with toy programs in wave 7; u = 1/6 of `g[3] = 1;`)
 R1 Size is counted on the front end's tree, before any optimisation: dead stores, `if(0)` bodies, code after
    `return`, empty `{ }` all count; a declaration without initialiser costs 0.
 R2 size(F) = 12u + 1u per parameter + 5u for `this` + the statements' weights (see WEIGHTS below).
 R3 B(F) = max(1000u, 2 x size(F)), from F's own pre-inlining size (all of F).
 R4 cost(site) = size(callee) (its own calls count as call expressions only). Verified exact (toys, 7 shapes).
 R5 cost <= 36u: free (always inlined, never charged, never refused). __forceinline: always inlined, never charged.
 R6 Sites are visited depth first in evaluation order: statements in order; call arguments right to left;
    binary operators left to right; assignment right side first; if: condition, then, else; for: init,
    increment, condition, body (the /Od code order).
 R7 A top-level site is inlined iff cost <= remaining = B - all charges so far (top level and nested).
    A refused site charges nothing and its body's sites are not visited.
 R8 When a site S is inlined with `avail` at its level, its own sites share
        limit = (avail - cost(S)) / (1 + pending)
    pending = number of inline-candidate calls after S at S's level (free, refused, ctors, dtors, dead ones
    included; non-inline, virtual and function-pointer calls not). Applies recursively; every charge is
    subtracted at every enclosing level.
 R9 Depth limit 8 (#pragma inline_depth).

WEIGHTS (u, toys; for reading code, the tool measures the real thing): constant 2; parameter/local read 3;
global read 5; global store `g[3]=1` 6; local store `l=1` 4; field store via global ptr `gp->a=1` 7; call `f()` 4
(+2 per constant arg, +3 per param arg); `p->m()` 8; virtual call 9; inline call site 4; `if(x) s;` 5+cond+s,
`else` +4; `return;` 1; `while(x) x--;` 12; `for(i=0;i<x;i++) s;` 33-6; `switch` 2 cases 32; empty block 2;
binary operators 0 (operands only); local with inline ctor ~11; local with out-of-line dtor 15.
"""
import concurrent.futures, csv, json, os, re, struct, subprocess, sys, tempfile, threading

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import build  # noqa: E402
from coffobj import undecorate  # noqa: E402

VC6CL = os.environ.get('VC6CL') or r'E:\AVP2Source\scripts\vc6cl.bat'
COST_CACHE = os.path.join(ROOT, 'build', 'inline_costs.json')
NAMEMAPS = [os.path.join(ROOT, 'build', 'namemap.json'), r'E:\AVP2Source\decomp\build\namemap.json']
FLOOR, FREE, MAXDEPTH = 1000, 40, 8
# compiler-generated inline candidates whose cost can't be probed with a call expression (toy-measured)
FIXED_COSTS = {'??_H@YGXPAXIHP6EX0@Z@Z': 49}     # `vector constructor iterator' (arrays of classes with ctors)
OPT_FLAGS = re.compile(r'^/(O[12xdgitysab]\w*|Gy|Ob\d)$')
_ctr = [0]
_ctr_lock = threading.Lock()

# ----------------------------------------------------------------------------- compiling

def _tmpname(unit_dir):
    with _ctr_lock:
        _ctr[0] += 1
        n = _ctr[0]
    return os.path.join(unit_dir, '__ib_%d_%d.ibtmp' % (os.getpid(), n))


def compile_asm(unit, text, flags=None):
    """Compile `text` as if it were the unit's source; return the /FAs listing (or raise with the errors)."""
    cpp = _tmpname(os.path.dirname(unit.path))
    tmpd = tempfile.mkdtemp(prefix='ib_')
    asm, obj = os.path.join(tmpd, 'a.asm'), os.path.join(tmpd, 'a.obj')
    open(cpp, 'w', newline='', encoding='latin1').write(text)
    try:
        fl = unit.flags if flags is None else flags
        args = [VC6CL] + build.COMMON_FLAGS + fl + ['/FAs', '/Fa' + asm, '/Fo' + obj, '/Tp' + cpp]
        r = subprocess.run(['cmd', '/c'] + args, capture_output=True, text=True, cwd=os.path.dirname(cpp))
        if not os.path.exists(asm) or r.returncode != 0:
            errs = [l for l in r.stdout.splitlines() if ' error ' in l or 'fatal error' in l]
            raise CompileError(errs or r.stdout.splitlines()[-5:])
        return open(asm, encoding='latin1', errors='replace').read()
    finally:
        for p in (cpp, asm, obj):
            try:
                os.remove(p)
            except OSError:
                pass
        try:
            os.rmdir(tmpd)
        except OSError:
            pass


class CompileError(Exception):
    pass


def parse_listing(asm):
    """{mangled: {'comdat': bool, 'calls': [mangled...], 'desc': str}} from a /FAs listing (call/jmp to symbols)."""
    funcs, cur = {}, None
    for line in asm.splitlines():
        m = re.match(r'^(\S+)\s+PROC NEAR(.*)$', line)
        if m:
            cur = m.group(1)
            funcs[cur] = {'comdat': 'COMDAT' in m.group(2), 'calls': [], 'desc': m.group(2).strip(' ;\t')}
            continue
        if re.match(r'^\S+\s+ENDP', line):
            cur = None
            continue
        if cur:
            m = re.match(r'^\s+(call|jmp)\s+(?:DWORD PTR\s+)?(\?\S+|_\w\S*)', line)
            if m and not m.group(2).startswith('__imp_'):
                funcs[cur]['calls'].append(m.group(2))
    return funcs


def desc_name(desc):
    return desc.split(',')[0].strip()

# ----------------------------------------------------------------------------- locating the function

def find_function(key):
    """(unit, annotation) for a name (undecorated, qualified or not) or hex address."""
    units = [u for u in build.find_units() if not os.path.basename(u.path).startswith('__ib_')]   # our temp copies
    va = None
    if re.match(r'^(0x)?[0-9a-fA-F]{6,8}$', key):
        va = int(key, 16)
    cands = []
    for u in units:
        for a in u.annots:
            if a.kind == 'GLOBAL':
                continue
            if va is not None and a.va == va or va is None and a.name and (
                    a.name == key or a.name.split('::')[-1] == key):
                cands.append((u, a))
    if not cands:
        raise SystemExit('no annotated function %s' % key)
    cands.sort(key=lambda ua: ua[1].name != key)       # exact (qualified) name first
    return cands[0]


def body_start(text, line_no):
    """Offset just after the '{' that opens the definition starting at 1-based line_no (skips the parameter list
    and initialiser lists)."""
    lines = text.split('\n')
    off = sum(len(l) + 1 for l in lines[:line_no - 1])
    depth = 0
    i = off
    while i < len(text):
        c = text[i]
        if text.startswith('//', i):
            i = text.index('\n', i)
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        elif c == '{' and depth == 0:
            return i + 1
        i += 1
    raise ValueError('no body')


def fn_symbol(listing, unit_ann_name, mangled=None):
    if mangled and mangled in listing:
        return mangled
    hits = [k for k, v in listing.items() if desc_name(v['desc']) == unit_ann_name]
    if not hits:
        hits = [k for k, v in listing.items() if desc_name(v['desc']).split('::')[-1] == unit_ann_name.split('::')[-1]]
    return hits[0] if hits else None

# ----------------------------------------------------------------------------- probes

PROBE_DATA = 'int __ib_g[8000];\nvoid *__ib_p;\n'


def fine_body(W, arr='__ib_g', base=0):
    """Statements of exactly W u (W >= 0; W = 1,3,5,7,9,11 are rounded down)."""
    for a in (0, 1):
        for b in range(6):
            rem = W - 13 * a - 2 * b
            if rem >= 0 and rem % 6 == 0:
                K = rem // 6
                return (''.join('\t%s[%d]=%d;\n' % (arr, base + i, i + 1) for i in range(K)) +
                        ('\tif(0) %s[7999]=1;\n' % arr) * a + '\t{ }\n' * b)
    return fine_body(W - 1, arr, base)


def probe_def(name, W):
    return 'inline void %s() {\n%s}\n' % (name, fine_body(W))


def bsearch_parallel(jobs, lo, hi, run_round):
    """jobs: list of keys. run_round({key: W}) -> {key: inlined?}. Monotone: inlined for W <= W*. Returns {key: W*}
    (None when not inlined even at lo, hi when inlined at hi)."""
    st = {k: [lo, hi] for k in jobs}
    first = run_round({k: lo for k in jobs})
    res = {}
    for k in jobs:
        if not first.get(k):
            res[k] = None
            st.pop(k)
    if st:
        top = run_round({k: hi for k in st})
        for k in list(st):
            if top.get(k):
                res[k] = hi
                st.pop(k)
    while st:
        mids = {k: (a + b) // 2 for k, (a, b) in st.items()}
        r = run_round(mids)
        for k, m in mids.items():
            if r.get(k):
                st[k][0] = m
            else:
                st[k][1] = m
            if st[k][1] - st[k][0] <= 1:
                res[k] = st[k][0]
                st.pop(k)
    return res


def measure_B(unit, ann, text):
    """Exact budget B(F) with a probe as F's first statement."""
    pos = body_start(text, ann.line)
    anno_off = sum(len(l) + 1 for l in text.split('\n')[:ann.line - 1])
    # the probe definition goes before the annotation line (outside any function)

    def src(W):
        t = text[:pos] + '\n\t__ib_P0();' + text[pos:]
        return t[:anno_off] + PROBE_DATA + probe_def('__ib_P0', W) + t[anno_off:]
    sym = [None]

    def rnd(ws):
        W = ws['B']
        lst = parse_listing(compile_asm(unit, src(W)))
        if sym[0] is None:
            sym[0] = fn_symbol(lst, ann.name, ann.mangled)
        return {'B': '?__ib_P0@@YAXXZ' not in lst.get(sym[0], {'calls': []})['calls']}
    r = bsearch_parallel(['B'], 0, 30000, rnd)['B']
    return None if r is None else r + 12

# ----------------------------------------------------------------------------- call expressions for cost probes

UND_FULL = 0


def full_signature(mangled):
    return undecorate(mangled, UND_FULL)


def split_params(s):
    out, depth, cur = [], 0, ''
    for c in s:
        if c in '<(':
            depth += 1
        elif c in '>)':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


SCALARS = ('char', 'short', 'int', 'long', 'float', 'double', 'bool', 'unsigned', 'signed', 'enum ', '__int64')


def arg_expr(t):
    t = re.sub(r'\b(class|struct|union) ', '', t).strip()
    if t.endswith('&'):
        return '*(%s *)__ib_p' % t[:-1].strip()
    if t.endswith('*') or '(' in t:
        return '(%s)__ib_p' % t if '(' not in t else '0'
    if any(t.startswith(s) or (' ' + s) in (' ' + t) for s in SCALARS):
        return '(%s)0' % t
    return '*(%s *)__ib_p' % t


def call_expr(mangled):
    """C++ statement calling `mangled` with dummy arguments, or None."""
    sig = full_signature(mangled).strip()
    if sig == mangled or sig.startswith("void __stdcall `"):
        return None
    m = re.match(r'^(?:(public|protected|private): )?(static |virtual )?(.*?)(__thiscall|__cdecl|__stdcall|__fastcall) '
                 r'(.+?)\((.*)\)(const)?$', sig)
    if not m:
        return None
    access, static, ret, cc, qname, params, const = m.groups()
    params = [] if params.strip() in ('void', '') else split_params(params)
    if '...' in params:
        params = [p for p in params if p != '...']
    args = ', '.join(arg_expr(p) for p in params)
    # split qualified name at the last :: outside <>
    depth, cut = 0, -1
    for i, c in enumerate(qname):
        if c == '<':
            depth += 1
        elif c == '>':
            depth -= 1
        elif c == ':' and depth == 0 and qname[i:i + 2] == '::':
            cut = i
    if cut < 0:
        call = '%s(%s)' % (qname, args)
    else:
        cls, name = qname[:cut], qname[cut + 2:]
        base = re.sub(r'<.*>$', '', cls).split('::')[-1]
        if name == cls.split('::')[-1] or name == base or re.sub(r'<.*>$', '', name) == base:      # constructor
            return '{ %s __ib_o(%s); }' % (cls, args) if args else '{ %s __ib_o; }' % cls
        if name.startswith('~'):
            return '((%s *)__ib_p)->%s::%s();' % (cls, cls, name)
        if name == "`scalar deleting destructor'":
            return 'delete (%s *)__ib_p;' % cls
        if static:
            call = '%s::%s(%s)' % (cls, name, args)
        else:
            call = '((%s *)__ib_p)->%s(%s)' % (cls, name, args)
    # use the result: VC6 deletes a call of a side-effect-free function whose result is unused
    ret = re.sub(r'\b(class|struct|union) ', '', (ret or '').strip())
    if ret in ('', 'void'):
        return call + ';'
    if ret.endswith('&'):
        return '__ib_p = (void *)&%s;' % call
    if ret.endswith('*'):
        return '__ib_p = (void *)%s;' % call
    if any(ret.startswith(s) or (' ' + s) in (' ' + ret) for s in SCALARS):
        return '__ib_g[7998] = (int)%s;' % call
    return call + ';'


def load_costs():
    try:
        return json.load(open(COST_CACHE))
    except (OSError, ValueError):
        return {}


def save_costs(c):
    """Merge into the cache file (several runs may share it) and replace it atomically."""
    os.makedirs(os.path.dirname(COST_CACHE), exist_ok=True)
    cur = load_costs()
    for k, v in c.items():
        if v.get('cost') is not None or k not in cur:
            cur[k] = v
    tmp = COST_CACHE + '.%d.tmp' % os.getpid()
    json.dump(cur, open(tmp, 'w'), indent=1, sort_keys=True)
    os.replace(tmp, COST_CACHE)


BW_STORES = 500                   # wrapper ballast: B(W) = 2*(12 + 6*500 + 4) = 6032
BW = 2 * (12 + 6 * BW_STORES + 4)


def measure_costs(unit, text, callees, log=print, jobs=6):
    """{mangled: cost} for callees, via wrappers appended to the unit (cached by mangled name + unit flags)."""
    cache = load_costs()
    cache.update({k: {'cost': v, 'why': 'fixed (toy-measured)'} for k, v in FIXED_COSTS.items()})
    todo = [c for c in callees if c not in cache]
    exprs = {}
    for c in todo:
        e = call_expr(c)
        if e:
            exprs[c] = e
        else:
            cache[c] = {'cost': None, 'why': 'no call expression'}
    # drop expressions that don't compile (one compile per round of removals)
    tail = '\n#pragma inline_depth()\n' + PROBE_DATA
    lift = ''.join('\t__ib_g[%d]=%d;\n' % (4000 + i, i + 1) for i in range(BW_STORES))
    # private/protected callees: open the classes up (only in this measurement copy)
    head = ['#define private public\n#define protected public\n']
    try:
        compile_asm(unit, head[0] + text)
    except CompileError:
        head[0] = ''

    def text_for(Ws, Wp):
        t = head[0] + text + tail
        for i, c in enumerate(exprs):
            if c in Ws:
                t += 'inline void __ib_R%d() {\n%s\t%s\n}\nvoid __ib_W%d() {\n%s\t__ib_R%d();\n}\n' % (
                    i, fine_body(Ws[c]), exprs[c], i, lift, i)
            if c in Wp:
                t += probe_def('__ib_Q%d' % i, Wp[c])
                t += 'void __ib_S%d() {\n\t__ib_Q%d();\n%s\t%s\n}\n' % (i, i, lift, exprs[c])
        return t
    for _ in range(10):
        try:
            compile_asm(unit, text_for({c: 0 for c in exprs}, {c: 0 for c in exprs}))
            break
        except CompileError as e:
            lines = text_for({c: 0 for c in exprs}, {c: 0 for c in exprs}).split('\n')
            bad = set()
            for err in e.args[0]:
                m = re.search(r'\((\d+)\)\s*:', err)
                if m:
                    ln = int(m.group(1))
                    for j in range(ln - 1, -1, -1):
                        mm = re.match(r'^(?:inline )?void __ib_[RS](\d+)\(\)', lines[j])
                        if mm:
                            bad.add(list(exprs)[int(mm.group(1))])
                            break
            if not bad:
                log('cost probes: compile failed: %s' % e.args[0][:3])
                for c in exprs:
                    cache[c] = {'cost': None, 'why': 'compile failed'}
                exprs = {}
                break
            for c in bad:
                cache[c] = {'cost': None, 'why': 'call expression does not compile: ' + exprs[c]}
                del exprs[c]
    if exprs:
        idx = {c: i for i, c in enumerate(exprs)}

        def rnd_cost(ws):
            lst = parse_listing(compile_asm(unit, text_for(ws, {})))
            out = {}
            for c in ws:
                w = lst.get('?__ib_W%d@@YAXXZ' % idx[c])
                # `#define protected public` changes the access code in the mangled name: compare without it
                called = {undecorate(x, 0x80) for x in w['calls']} if w is not None else set()
                out[c] = (w is not None and undecorate(c, 0x80) not in called and
                          '?__ib_R%d@@YAXXZ' % idx[c] not in w['calls'])
            return out

        def rnd_expr(ws):
            lst = parse_listing(compile_asm(unit, text_for({}, ws)))
            out = {}
            for c in ws:
                s = lst.get('?__ib_S%d@@YAXXZ' % idx[c])
                out[c] = s is not None and '?__ib_Q%d@@YAXXZ' % idx[c] not in s['calls']
            return out
        keys = list(exprs)
        log('measuring %d callee costs (%d cached) ...' % (len(keys), len(callees) - len(todo)))
        # split into batches compiled in parallel
        nb = max(1, min(jobs, (len(keys) + 7) // 8))
        batches = [keys[i::nb] for i in range(nb)]
        with concurrent.futures.ThreadPoolExecutor(nb * 2) as ex:
            fc = [ex.submit(bsearch_parallel, b, 0, BW - 42, rnd_cost) for b in batches]
            fe = [ex.submit(bsearch_parallel, b, 0, 20000, rnd_expr) for b in batches]
            wc, we = {}, {}
            for f in fc:
                wc.update(f.result())
            for f in fe:
                we.update(f.result())
        for c in keys:
            if wc.get(c) is None or we.get(c) is None:
                cache[c] = {'cost': None, 'why': 'never inlined'}
                continue
            sizeS = (we[c] + 12) / 2.0                    # B(S) = 2 * size(S)
            wexpr = sizeS - 12 - 4 - 6 * BW_STORES        # the call expression's own weight in R / S
            costR = 12 + wc[c] + wexpr
            cost = BW - costR
            cache[c] = {'cost': int(round(cost)), 'expr': exprs[c], 'wexpr': wexpr}
            if wc[c] >= BW - 42 or cost <= FREE:       # inlined with a limit <= 30u: free (or __forceinline)
                cache[c]['cost'] = min(int(round(cost)), FREE)
                cache[c]['free'] = True
    save_costs(cache)
    return {c: cache[c] for c in callees}

# ----------------------------------------------------------------------------- the model

class Site:
    def __init__(self, callee, depth):
        self.callee, self.depth = callee, depth
        self.children = []
        self.cost = None
        self.limit = None
        self.decision = None       # 'inline' / 'free' / 'force' / 'refused' / 'depth' / 'unknown'
        self.pending = 0


def build_tree(lst, root, depth=0, stack=()):
    sites = []
    if depth >= 12:
        return sites
    for c in lst[root]['calls']:
        if c in lst and lst[c]['comdat'] and (not c.startswith('??_') or c in FIXED_COSTS or
                                                    c.startswith('??_G')):   # helpers: ??_H, ??_G
            s = Site(c, depth + 1)
            if c not in stack:
                s.children = build_tree(lst, c, depth + 1, stack + (c,))
            sites.append(s)
    return sites


def simulate(sites, limit, costs, depth=1):
    used = 0
    for i, s in enumerate(sites):
        avail = limit - used
        s.pending = len(sites) - i - 1
        s.limit = avail
        info = costs.get(s.callee) or {}
        cost = info.get('cost')
        s.cost = cost
        if depth > MAXDEPTH:
            s.decision = 'depth'
            continue
        if cost is None:
            s.decision, charge = 'unknown', 0
        elif cost <= FREE:
            s.decision, charge = 'free', 0
        elif cost <= avail:
            s.decision, charge = 'inline', cost
        else:
            s.decision = 'refused'
            continue
        used += charge
        child = (avail - charge) / (1.0 + s.pending)
        used += simulate(s.children, child, costs, depth + 1)
    return used


def refused_multiset(sites, out=None):
    out = {} if out is None else out
    for s in sites:
        if s.decision in ('refused', 'depth'):
            out[s.callee] = out.get(s.callee, 0) + 1
        elif s.decision != 'unknown':
            refused_multiset(s.children, out)
    return out


def walk(sites):
    for s in sites:
        yield s
        if s.decision not in ('refused', 'depth'):
            for x in walk(s.children):
                yield x

# ----------------------------------------------------------------------------- exe side

def exe_calls(va, symtab):
    import pefile, capstone
    exe = build.Exe(build.EXE)
    end = symtab.funcs[va][0]
    data = exe.pe.get_memory_mapped_image()[va - exe.base:end - exe.base]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    names = {}
    for p in NAMEMAPS:
        if os.path.exists(p):
            for k, v in json.load(open(p)).items():
                names.setdefault(int(k, 16), v)
    out = []
    for ins in md.disasm(data, va):
        if ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x'):
            t = int(ins.op_str, 16)
            if ins.mnemonic == 'jmp' and va <= t < end:
                continue
            out.append(ALIASES.get(t) or names.get(t) or symtab.names.get(t) or '%08x' % t)
    return out


COST_OVERRIDES = {}
ALIASES = {}     # exe address -> mangled name, for out-of-line copies no matched function has named yet (--alias)


def _norm(n):
    n = undecorate(n) if n.startswith('?') else n
    n = re.sub(r'<[^<>]*>', '', re.sub(r'<[^<>]*>', '', n))
    return n.replace(' ', '')


def map_exe(ex_calls, callees):
    """Exe call names -> our callee keys (mangled when known; Ghidra names matched by unqualified-template name)."""
    by_norm = {}
    for c in callees:
        by_norm.setdefault(_norm(c), []).append(c)
    out = {}
    for n, k in ex_calls.items():
        if n in callees:
            key = n
        else:
            hits = by_norm.get(_norm(n), [])
            if len(hits) != 1:
                continue
            key = hits[0]
        out[key] = out.get(key, 0) + k
    return out


def count(lst):
    d = {}
    for x in lst:
        d[x] = d.get(x, 0) + 1
    return d

# ----------------------------------------------------------------------------- driver

def tree_flags(unit):
    return [f for f in unit.flags if not OPT_FLAGS.match(f)] + ['/Od', '/Ob0']


def short(m):
    u = undecorate(m)
    return u.replace('_CVector<float>', 'LTVector')


def apply_variant(text, ann, repls):
    """Apply (old, new) replacements in memory; returns (text, annotation with its new line number)."""
    import copy
    for old, new in repls:
        if text.count(old) != 1:
            raise SystemExit('variant: %r occurs %d times' % (old[:60], text.count(old)))
        text = text.replace(old, new)
    a = copy.copy(ann)
    for i, l in enumerate(text.split('\n')):
        m = build.ANNOT.match(l)
        if m and int(m.group(2), 16) == ann.va:
            a.line = i + 1
            break
    return text, a


def analyse(key, verbose=False, use_exe=True, sweep=False, costs_on=True, jobs=6, quiet=False, variant=None):
    log = (lambda *a: None) if quiet else print
    unit, ann = find_function(key)
    text = open(unit.path, encoding='latin1').read()
    if variant:
        text, ann = apply_variant(text, ann, variant)
    log('%s  (%s:%d, %s)' % (ann.name, unit.rel, ann.line, ' '.join(unit.flags)))
    # our /O2 build and the /Od /Ob0 tree, in parallel with B
    with concurrent.futures.ThreadPoolExecutor(3) as ex:
        f_o2 = ex.submit(compile_asm, unit, text)
        f_tree = ex.submit(compile_asm, unit, text, tree_flags(unit))
        f_B = ex.submit(measure_B, unit, ann, text)
        o2, tl, B = parse_listing(f_o2.result()), parse_listing(f_tree.result()), f_B.result()
    root = fn_symbol(tl, ann.name, ann.mangled)
    root_o2 = fn_symbol(o2, ann.name, ann.mangled)
    if root is None or root_o2 is None:
        raise SystemExit('function not found in the listings')
    sites = build_tree(tl, root)
    callees = sorted({s.callee for s in walk_all(sites)})
    size = None if B is None or B <= FLOOR else B / 2.0 - 4
    log('B(F) = %s u  -> size(F) = %s u%s' % (B, size if size is not None else '<= %d' % (FLOOR // 2 - 4),
                                                '' if size is not None else ' (budget at the 1000u floor)'))
    costs = measure_costs(unit, text, callees, log=log, jobs=jobs) if costs_on else {}
    for k, v in COST_OVERRIDES.items():           # --cost: what-if costs (mangled or undecorated name)
        for c in callees:
            if c == k or undecorate(c) == k:
                costs[c] = {'cost': v}
                log('cost override: %s = %d' % (short(c), v))
    simulate(sites, B if B else FLOOR, costs)
    pred = refused_multiset(sites)
    ours = {k: v for k, v in count(o2[root_o2]['calls']).items() if k in tl and tl[k]['comdat'] or k in pred}
    res = {'name': ann.name, 'B': B, 'size': size, 'pred': pred, 'ours': ours, 'sites': sites, 'costs': costs}
    if use_exe:
        st = build.SymTab()
        if ann.va in st.funcs:
            ex_calls = count(exe_calls(ann.va, st))
            res['exe'] = map_exe(ex_calls, callees)
    if not quiet:
        report(res, verbose)
    if sweep and 'exe' in res:
        do_sweep(res, sites, costs, B)
    return res


def walk_all(sites):
    for s in sites:
        yield s
        for x in walk_all(s.children):
            yield x


def report(res, verbose):
    def show(sites, ind=0):
        for s in sites:
            interesting = verbose or s.depth == 1 or s.decision in ('refused', 'depth', 'unknown')
            if interesting:
                print('%s%-9s %-55s cost %5s  limit %7.1f  pending %d' % (
                    '  ' * (s.depth - 1), s.decision, short(s.callee)[:55], s.cost, s.limit, s.pending))
            if s.decision not in ('refused', 'depth') and (verbose or s.depth < 2 or True):
                show(s.children, ind + 1)
    show(res['sites'])
    keys = sorted(set(res['pred']) | set(res['ours']) | set(res.get('exe', {})))
    print('\nout-of-line calls of inline candidates:   predicted / our build%s' % (' / exe' if 'exe' in res else ''))
    for k in keys:
        p, o, e = res['pred'].get(k, 0), res['ours'].get(k, 0), res.get('exe', {}).get(k, 0)
        flag = '' if p == o else '   <-- model != build'
        if 'exe' in res and o != e:
            flag += '   <-- build != exe'
        print('  %3d %3d %s  %s%s' % (p, o, ('%3d' % e) if 'exe' in res else '', short(k)[:70], flag))
    unknown = [k for k, v in res['costs'].items() if v.get('cost') is None]
    if unknown:
        print('cost unknown (treated as inlined, not charged): %s' % ', '.join(short(k) for k in unknown))


def do_sweep(res, sites, costs, B):
    want = res['exe']
    ok = []
    best = (10 ** 9, None)
    for b in range(FLOOR, max(3 * (B or FLOOR), 4000), 4):
        simulate(sites, b, costs)
        p = refused_multiset(sites)
        miss = sum(abs(p.get(k, 0) - want.get(k, 0)) for k in set(p) | set(want))
        if miss < best[0]:
            best = (miss, b)
        if miss == 0:
            ok.append(b)
    simulate(sites, B or FLOOR, costs)
    if not ok:
        simulate(sites, best[1], costs)
        p = refused_multiset(sites)
        print('closest budget: %s u (%d out-of-line calls differ from the exe: %s)' % (best[1], best[0], ', '.join(
            '%s model %d exe %d' % (short(k), p.get(k, 0), want.get(k, 0)) for k in sorted(set(p) | set(want))
            if p.get(k, 0) != want.get(k, 0))))
        simulate(sites, B or FLOOR, costs)
    if ok:
        rngs = []
        for b in ok:
            if rngs and b - rngs[-1][1] <= 4:
                rngs[-1][1] = b
            else:
                rngs.append([b, b])
        print('budgets that reproduce the exe: %s  (ours %s)' % (', '.join('%d-%d' % tuple(r) for r in rngs), B))
    else:
        print('no budget in [1000, %d] reproduces the exe with these costs/sites' % max(3 * (B or FLOOR), 4000))


def measure_at(key, anchor):
    unit, ann = find_function(key)
    text = open(unit.path, encoding='latin1').read()
    start = body_start(text, ann.line)
    pos = text.find(anchor, start)
    if pos < 0:
        raise SystemExit('anchor not found after the function start')
    anno_off = sum(len(l) + 1 for l in text.split('\n')[:ann.line - 1])

    def src(W):
        t = text[:pos] + '__ib_P0();\n\t' + text[pos:]
        return t[:anno_off] + PROBE_DATA + probe_def('__ib_P0', W) + t[anno_off:]
    sym = [None]

    def rnd(ws):
        lst = parse_listing(compile_asm(unit, src(ws['a'])))
        if sym[0] is None:
            sym[0] = fn_symbol(lst, ann.name, ann.mangled)
        return {'a': '?__ib_P0@@YAXXZ' not in lst[sym[0]]['calls']}
    r = bsearch_parallel(['a'], 0, 30000, rnd)['a']
    print('remaining before %r: %s u' % (anchor, None if r is None else r + 12))


VALIDATE = ['SweptSphereOrient', 'FillSoundTrackPacketFromInfo', '4995f0', 'ThreadLoadFile', 'UnloadFile',
            'TransferNetDriver', 'StartHMessageWrite', 'ftc_ProcessPacket', 'OnLoadWorldPacket',
            'sm_TellClientAboutGlobalLight', 'ModelExtraInit', 'StairStep', 'ClipBoxIntoTree', 'AddMovement',
            'CSoundMgr::Update', 'w_LoadWorldBsp', 'MoveObject', 'DoNonsolidCollision', 'GrowDim',
            'RotateWorldModel', 'ChangeObjectDimensions', 'CollideAgainstWorld', 'GetPushawayPos',
            'CreateServerMgr', 'ReallySendPacket', 'CSoundMgr::Init', 'dsi_LoadServerObjects', 'ClientLoadChildModelCB',
            'sm_WriteLightAnimInfo', 'AddDataToGroupPacket', 'SetObjectFilenames', 'LockTexture']


def run_variants(key, path, jobs, sweep):
    """Score source variants in memory (vtry-style variants file: VARIANTS = [(label, old, new), ...], old/new may be
    lists): B, size, and how many of the exe's out-of-line calls the model reproduces, plus our build's calls."""
    import runpy
    vs = runpy.run_path(path)['VARIANTS']
    pre = runpy.run_path(path).get('PRE', [])
    for label, old, new in [('base', [], [])] + list(vs):
        olds = old if isinstance(old, list) else [old]
        news = new if isinstance(new, list) else [new]
        try:
            r = analyse(key, use_exe=True, jobs=jobs, quiet=True, variant=list(pre) + list(zip(olds, news)))
        except (SystemExit, CompileError) as e:
            print('%-24s %s' % (label, str(e)[:200]))
            continue
        want = r.get('exe', {})
        keys = set(r['pred']) | set(want) | set(r['ours'])
        dm = sum(abs(r['pred'].get(k, 0) - want.get(k, 0)) for k in keys)
        db = sum(abs(r['ours'].get(k, 0) - want.get(k, 0)) for k in keys)
        print('%-24s B=%-5s size=%-7s model-vs-exe %d  build-vs-exe %d   build: %s' % (
            label, r['B'], r['size'], dm, db, ', '.join('%s %d' % (short(k)[:28], v) for k, v in sorted(r['ours'].items()))),
            flush=True)
        if sweep:
            do_sweep(r, r['sites'], r['costs'], r['B'])


def validate(names, jobs):
    tot = good = 0
    exact = 0
    for n in names:
        try:
            r = analyse(n, use_exe=False, jobs=jobs, quiet=True)
        except SystemExit as e:
            print('%-32s skipped: %s' % (n, e))
            continue
        except CompileError as e:
            print('%-32s compile error: %s' % (n, e.args[0][:2]))
            continue
        sites = list(walk(r['sites']))
        # per-site agreement: predicted refusals vs our build's out-of-line counts, by callee
        keys = set(r['pred']) | set(r['ours'])
        nsite = len(sites)
        bad = sum(abs(r['pred'].get(k, 0) - r['ours'].get(k, 0)) for k in keys)
        tot += nsite
        good += max(0, nsite - bad)
        exact += (bad == 0)
        print('%-32s B=%-6s sites %3d  mispredicted %d%s' % (n, r['B'], nsite, bad, '' if bad == 0 else '  ' + ', '.join(
            '%s %d/%d' % (short(k)[:30], r['pred'].get(k, 0), r['ours'].get(k, 0))
            for k in sorted(keys) if r['pred'].get(k, 0) != r['ours'].get(k, 0))), flush=True)
    print('\n%d of %d sites predicted (%.1f%%); %d of %d functions exact' % (good, tot, 100.0 * good / max(tot, 1), exact,
                                                                           len(names)))


def main(argv):
    jobs = 6
    while '--cost' in argv:           # --cost IsWorldModel=41  (what-if; all overloads with that name)
        i = argv.index('--cost')
        a, v = argv[i + 1].rsplit('=', 1)
        COST_OVERRIDES[a] = int(v)
        del argv[i:i + 2]
    while '--alias' in argv:          # --alias 45e960=?IsWorldModel@@YAIPAVLTObject@@@Z
        i = argv.index('--alias')
        a, m = argv[i + 1].split('=', 1)
        ALIASES[int(a, 16)] = m
        del argv[i:i + 2]
    if '-j' in argv:
        i = argv.index('-j')
        jobs = int(argv[i + 1])
        del argv[i:i + 2]
    if argv and argv[0] == '--validate':
        validate(argv[1:] or VALIDATE, jobs)
        return
    if argv and argv[0] == '--variants':
        run_variants(argv[1], argv[2], jobs, '--sweep' in argv)
        return
    if argv and argv[0] == '--at':
        measure_at(argv[1], argv[2])
        return
    flags = {a for a in argv if a.startswith('-')}
    keys = [a for a in argv if not a.startswith('-')]
    if not keys:
        print(__doc__)
        return
    for k in keys:
        analyse(k, verbose='-v' in flags, sweep='--sweep' in flags, costs_on='--no-cost' not in flags, jobs=jobs)


if __name__ == '__main__':
    main(sys.argv[1:])
