"""Build a function-name -> defining-file database for unit recovery.

Sources:
  * C/C++ definition finder over the Jupiter tree, the Talon SDK and lithshared
  * Jupiter's original Lithtech.map (publics -> object, non-inline entries only)
  * the VS2010 Jupiter PDBs (procs per module, includes statics)
  * library objects in E:\\AVP2Source\\libs_objs (COFF function symbols)

Output: build/units/defs.json  {name: [[unitpath, source, weight], ...]}, plus filepaths.json
"""
import os, re, sys, json, collections

ROOT = r'E:\AVP2Source'
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
OUT = os.path.join(HERE, '..', 'build', 'units')

# ---------------------------------------------------------------- definition finder

_tok = re.compile(r'''
    (?P<str>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')
  | (?P<cmt>//[^\n]*|/\*.*?\*/)
''', re.S | re.X)


def strip_source(t):
    # remove comments and string literals (keep newlines), drop preprocessor lines
    def rep(m):
        s = m.group(0)
        if m.group('str'):
            return '""'
        return '\n' * s.count('\n') + ' '
    t = _tok.sub(rep, t)
    out = []
    cont = False
    for line in t.split('\n'):
        if cont or line.lstrip().startswith('#'):
            cont = line.rstrip().endswith('\\')
            out.append('')
            continue
        out.append(line)
    return '\n'.join(out)


QUAL = r'(?:[A-Za-z_]\w*(?:\s*<[^;{}()]*?>)?\s*::\s*)*(?:~\s*)?(?:operator\s*(?:\(\)|[^\s(]+)|[A-Za-z_]\w*)'
_defhead = re.compile(r'(' + QUAL + r')\s*\(')
_kw = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'catch', 'defined', '__declspec', 'throw',
       'DECLARE_HANDLE', 'define_holder', 'LTASSERT', 'ASSERT'}


def norm_name(n):
    """strip template arguments and whitespace: CMoArray<int>::Foo -> CMoArray::Foo"""
    out, depth = [], 0
    for ch in n:
        if ch == '<':
            depth += 1
        elif ch == '>' and depth:
            depth -= 1
        elif depth == 0:
            out.append(ch)
    return re.sub(r'\s+', '', ''.join(out))


def find_defs(text):
    """Return list of (qualified_name, is_static) for function definitions in a C/C++ source text."""
    t = strip_source(text)
    defs = []
    stack = []          # scope kinds: 'ns', 'class:<name>', 'func', 'block'
    seg_start = 0
    i, n = 0, len(t)
    while i < n:
        c = t[i]
        if c in ';':
            seg_start = i + 1
        elif c == '{':
            seg = t[seg_start:i]
            kind = 'block'
            in_code = any(k in ('func', 'block') for k in stack)
            if not in_code:
                s = seg.strip()
                m_ns = re.match(r'(?:namespace\b\s*\w*|extern\s*""\s*)$', s)
                m_cls = re.search(r'\b(class|struct|union)\s+(?:__declspec\([^)]*\)\s*)?(\w+)\s*(?::[^{]*)?$', s)
                if m_ns:
                    kind = 'ns'
                elif m_cls and '(' not in s.split(m_cls.group(0))[0][-1:]:
                    kind = 'class:' + m_cls.group(2)
                elif re.search(r'\benum\b', s) or s.endswith('=') or re.search(r'=\s*$', s):
                    kind = 'block'
                else:
                    # function definition?  find the first qualified-name followed by '(' (skipping keywords)
                    for m in _defhead.finditer(s):
                        nm = norm_name(m.group(1))
                        last = nm.split('::')[-1].lstrip('~')
                        if last in _kw or nm in _kw:
                            continue
                        # require the paren group to close and be followed by const/throw/:/end
                        cls = [k[6:] for k in stack if k.startswith('class:')]
                        if cls and '::' not in nm:
                            nm = '::'.join(cls) + '::' + nm
                        static = bool(re.match(r'(?:inline\s+)?static\b', s)) or bool(re.search(r'\bstatic\b', s[:m.start()]))
                        defs.append((nm, static))
                        kind = 'func'
                        break
            stack.append(kind)
            seg_start = i + 1
        elif c == '}':
            if stack:
                stack.pop()
            seg_start = i + 1
        i += 1
    return defs


# ---------------------------------------------------------------- unit path naming

def jupiter_unit(path):
    rel = os.path.relpath(path, os.path.join(ROOT, 'jupiter')).replace('\\', '/')
    if rel.startswith('runtime/'):
        rel = rel[len('runtime/'):]
        rel = rel.replace('/src/', '/')
        return rel.lower()
    if rel.startswith('sdk/'):
        return 'sdk/' + os.path.basename(rel).lower()
    if rel.startswith('libs/'):
        parts = rel.split('/')
        return 'lib:%s/%s' % (parts[1].lower(), os.path.splitext(os.path.basename(rel))[0].lower())
    return rel.lower()


def lithshared_unit(path):
    rel = os.path.relpath(path, os.path.join(ROOT, r'build\proj\LT2')).replace('\\', '/')
    parts = rel.split('/')
    if parts[0] == 'sdk':
        return 'sdk/' + parts[-1].lower()
    return 'lib:%s/%s' % (parts[1].lower(), os.path.splitext(parts[-1])[0].lower())


PLATFORM_BAD = ('/linux/', '/null/', '/nulldib/', '/s_nul/', 'esd/', '/toport/')


BASES = collections.defaultdict(set)
_inh = re.compile(r'\b(?:class|struct)\s+(?:\w+\s+)?(\w+)\s*:\s*([^{;]+)\{')


def scan_tree(base, unitfn, tag, db, files, exts=('.cpp', '.c', '.cc')):
    for dp, dn, fn in os.walk(base):
        low = dp.lower()
        if '_build' in low or '\\debug' in low or '\\release' in low:
            continue
        for f in fn:
            ext = os.path.splitext(f)[1].lower()
            if ext not in exts + ('.h', '.inl'):
                continue
            p = os.path.join(dp, f)
            try:
                txt = open(p, 'r', encoding='latin-1').read()
            except OSError:
                continue
            unit = unitfn(p)
            for m in _inh.finditer(strip_source(txt)):
                for b in re.findall(r'(?:public|protected|private)?\s*(?:virtual\s+)?(\w+)\s*(?:<[^>]*>)?\s*(?:,|$)', m.group(2).strip()):
                    BASES[m.group(1)].add(b)
            if ext in exts:
                files.setdefault(unit, p)
            for nm, static in find_defs(txt):
                if ext in exts:
                    w = 1.0
                    if any(b in unit for b in PLATFORM_BAD):
                        w = 0.3
                    db[nm].append((unit, tag + ('-static' if static else ''), w))
                else:
                    db[nm].append(('header:' + unit, tag + '-hdr', 0.0))


# ---------------------------------------------------------------- map / pdb / lib objs

def jupiter_map(db, objunit):
    from coffobj import undecorate
    p = os.path.join(ROOT, r'jupiter\runtime\winbuild\lithtech\Lithtech.map')
    for l in open(p, encoding='latin-1'):
        m = re.match(r'\s*0001:([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{8})\s+f\s+(i\s+)?(\S+)\s*$', l)
        if not m or m.group(4):
            continue
        obj = m.group(5)
        nm = norm_name(undecorate(m.group(2)))
        base = obj.split(':')[-1].lower().replace('.obj', '')
        u = objunit.get(base)
        if u is None:
            continue
        db[nm].append((u, 'jmap', 1.0))


def jupiter_pdbs(db, objunit):
    import pdbmods
    for pdb in ('Lithtech.pdb', 'Server.pdb'):
        r = pdbmods.parse(os.path.join(ROOT, 'bin', 'jupiter', pdb))
        mods = r['modules']
        for p in r['procs']:
            mn = mods[p['mod']]['name'].replace('/', '\\')
            base = os.path.basename(mn).lower()
            base = re.sub(r'\.(cpp|c)\.obj$|\.obj$', '', base)
            u = objunit.get(base)
            if u is None:
                continue
            db[norm_name(p['name'])].append((u, 'jpdb' + ('-static' if p['local'] else ''), 0.7))


def lib_objs(db):
    from coffobj import CoffObj, undecorate
    base = os.path.join(ROOT, 'libs_objs')
    for lib in sorted(os.listdir(base)):
        d = os.path.join(base, lib)
        if not os.path.isdir(d):
            continue
        if lib.startswith('VC6'):
            # several CRT flavours: keep only the SP5 multithreaded build (others duplicate names)
            crt = 'VC6SP5_LIBCMT' if os.path.isdir(os.path.join(base, 'VC6SP5_LIBCMT')) else 'VC6_LIBCMT_1999'
            cpp = 'VC6SP5_LIBCPMT' if os.path.isdir(os.path.join(base, 'VC6SP5_LIBCPMT')) else 'VC6_LIBCPMT'
            if lib not in (crt, cpp):
                continue
            pre = 'crt:' if lib == crt else 'crt:libcpmt/'
        else:
            pre = 'lib:%s/' % lib[3:].lower()
        for f in sorted(os.listdir(d)):
            if not f.lower().endswith('.obj'):
                continue
            try:
                o = CoffObj(os.path.join(d, f))
            except Exception:
                continue
            unit = pre + f[:-4].lower()
            seen = set()
            for s in o.functions():
                sec = o.section_of(s)
                if not sec.name.startswith('.text'):
                    continue
                comdat = bool(sec.flags & 0x1000)
                nm = norm_name(undecorate(s.name))
                if (nm, unit) in seen:
                    continue
                seen.add((nm, unit))
                # C functions in a COMDAT are just /Gy output; C++ COMDATs are often inline/template copies
                w = 1.0 if (not comdat or not s.name.startswith('?')) else 0.3
                if s.cls == 3:      # static
                    w = min(w, 0.6)
                db[nm].append((unit, 'libobj' + ('-comdat' if comdat else ''), w))
                if s.name.startswith('_') and not s.name.startswith('?'):
                    db[s.name].append((unit, 'libobj', w))


def main():
    os.makedirs(OUT, exist_ok=True)
    db = collections.defaultdict(list)
    files = {}
    scan_tree(os.path.join(ROOT, 'jupiter', 'runtime'), jupiter_unit, 'jsrc', db, files)
    scan_tree(os.path.join(ROOT, 'jupiter', 'sdk'), jupiter_unit, 'jsrc', db, files)
    scan_tree(os.path.join(ROOT, 'jupiter', 'libs'), jupiter_unit, 'jlibsrc', db, files)
    scan_tree(os.path.join(ROOT, r'build\proj\LT2\lithshared'), lithshared_unit, 'lt2src', db, files)
    scan_tree(os.path.join(ROOT, r'build\proj\LT2\sdk'), lithshared_unit, 'lt2sdk', db, files)
    # object base name -> preferred unit path
    cand = collections.defaultdict(list)
    for u in files:
        if u.startswith('lib:'):
            continue
        cand[os.path.splitext(os.path.basename(u))[0]].append(u)
    objunit = {}
    for b, us in cand.items():
        us.sort(key=lambda u: (any(x in u for x in PLATFORM_BAD), '/win' not in u and '/d3d' not in u, len(u)))
        objunit[b] = us[0]
    for u in files:
        if u.startswith('lib:'):
            objunit.setdefault(os.path.splitext(os.path.basename(u))[0], u)
    jupiter_map(db, objunit)
    jupiter_pdbs(db, objunit)
    lib_objs(db)
    json.dump({k: v for k, v in db.items()}, open(os.path.join(OUT, 'defs.json'), 'w'))
    json.dump({'files': files, 'objunit': objunit, 'bases': {k: sorted(v) for k, v in BASES.items()}}, open(os.path.join(OUT, 'filepaths.json'), 'w'), indent=0)
    print(len(db), 'names', len(files), 'files')


if __name__ == '__main__':
    main()
