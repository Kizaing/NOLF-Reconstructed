r"""Shared setup of the helper tools (permute, vtry, hillclimb, flagscan, inline_scan, inline_ballast, inline_budget).

  import toolenv      # first import of a helper: module selection, argument repair, then the helpers below

* Module: tools/modcfg.py consumes `--module d3dren` (or DECOMP_MODULE) at import and exports DECOMP_MODULE, so child
  processes follow.  Importing toolenv first guarantees that for tools that did not import modcfg themselves.
* MSYS argument repair: from Git Bash, `python tool.py /O1` arrives as `C:/Program Files/Git/O1` (and `"/O2 /Ob2"` as
  `C:/Program Files/Git/O2 /Ob2`, `/I` as `I:/`).  fix_argv() turns them back; it runs at import.
* env(): the compiler environment of the active module (modcfg.CL_ENV: DX8INC for d3dren) + a private TMP/TEMP.
* run_cl(): run the module's compiler wrapper (modcfg.CL) with a timeout; a hung CL.EXE (and the C1/C2 children) is killed.
* scratch_tu(): a private copy of a unit's source in build/<module>/scratch/ whose relative #include "..." lines point at
  the original files, so it compiles from anywhere.  Tools never write into src/.
* evaluate(): compile a modified copy of a unit privately and run the real checker on it: status, ALIGNED score and
  differing bytes per function, without touching the unit's real object or source.
"""
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402  (consumes --module <name>)

ROOT = modcfg.ROOT
NAME = modcfg.NAME
BUILD = modcfg.BUILD
PYTHON = sys.executable


# ---------------------------------------------------------------- argument repair

def _msys_roots():
    roots = []
    for k in ('EXEPATH', 'MSYS2_ROOT'):
        if os.environ.get(k):
            roots.append(os.environ[k])
    pre = os.environ.get('MSYSTEM_PREFIX')
    if pre:
        roots.append(os.path.dirname(pre))
    roots += [r'C:\Program Files\Git', r'C:\Program Files (x86)\Git', r'C:\msys64', r'C:\msys32']
    out = []
    for r in roots:
        r = r.replace('\\', '/').rstrip('/')
        if r and r.lower() not in out:
            out.append(r.lower())
    return out


def unmsys(arg):
    """Undo MSYS path conversion of one argument that is really a compiler flag or option value."""
    m = re.match(r'^(--?[\w-]+=)(.*)$', arg)
    if m:                                       # --opt=C:/Program Files/Git/O1
        return m.group(1) + unmsys(m.group(2))
    a = arg.replace('\\', '/')
    for r in _msys_roots():
        if a.lower().startswith(r + '/'):
            return '/' + a[len(r) + 1:]
    m = re.fullmatch(r'([A-Za-z]):/', a)        # `/I include` arrives as `I:/ include`
    if m and not os.path.isdir(a):
        return '/' + m.group(1)
    return arg


def fix_argv():
    sys.argv[1:] = [unmsys(a) for a in sys.argv[1:]]


fix_argv()
os.environ['DECOMP_MODULE'] = NAME


# ---------------------------------------------------------------- compiling

def env(tmp=None):
    """Environment for the compiler wrapper: the module's CL_ENV (DX8INC for d3dren) and, when given, a private TMP/TEMP
    (cl's intermediate files collide between parallel compiles otherwise)."""
    e = dict(os.environ)
    e.update(modcfg.CL_ENV)
    e['DECOMP_MODULE'] = NAME
    if tmp:
        e['TMP'] = e['TEMP'] = tmp
    return e


def cl_args(flags, *more, first=()):
    """Command line (after `cmd /c`) of one compile: wrapper, `first` (include directories that must win over the common
    /I include, e.g. permute.py --include), common flags, the unit's optimisation flags, extras."""
    return [modcfg.CL] + list(first) + list(modcfg.COMMON_FLAGS) + list(flags) + list(more)


def run_cmd(args, cwd=None, env_=None, timeout=120):
    """Run `cmd /c args`; returns (returncode, output, timed_out).  On timeout the whole process tree is killed."""
    p = subprocess.Popen(['cmd', '/c'] + list(args), cwd=cwd, env=env_ or env(), stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, errors='replace')
    try:
        out, _ = p.communicate(timeout=timeout)
        return p.returncode, out, False
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/F', '/T', '/PID', str(p.pid)], capture_output=True)
        try:
            out, _ = p.communicate(timeout=20)
        except Exception:      # noqa: BLE001
            out = ''
        return 1, (out or '') + '\nTIMEOUT after %ss: compiler killed' % timeout, True


def run_cl(flags, *more, cwd=None, tmp=None, timeout=120, first=()):
    """Compile with the module's wrapper: (returncode, output, timed_out)."""
    return run_cmd(cl_args(flags, *more, first=first), cwd=cwd, env_=env(tmp), timeout=timeout)


INCLUDE_RE = re.compile(r'^(\s*#\s*include\s*")([^"\n]+)(")', re.M)


def localise_includes(text, orig_dir):
    """Quoted includes that resolve relative to the original source's directory are rewritten to absolute paths (forward
    slashes), so a copy of the source in another directory still finds them."""
    def sub(m):
        p = os.path.normpath(os.path.join(orig_dir, m.group(2)))
        if os.path.isfile(p):
            return m.group(1) + p.replace('\\', '/') + m.group(3)
        return m.group(0)
    return INCLUDE_RE.sub(sub, text)


def scratch_dir(tool, *parts):
    """build/<module>/scratch/<tool>/...: private, never under src/."""
    d = os.path.join(BUILD, 'scratch', tool, *[str(x) for x in parts])
    os.makedirs(d, exist_ok=True)
    return d


def scratch_tu(src_path, text, dest_dir, name=None):
    """Write `text` (a modified version of the unit at src_path) into dest_dir as a private translation unit; returns its path."""
    dest = os.path.abspath(dest_dir)
    for d in (os.path.join(modcfg.ROOT, 'src'), modcfg.INC):      # anywhere but the source tree (permute.py --outdir may point outside build/)
        assert not os.path.normcase(dest).startswith(os.path.normcase(os.path.abspath(d)) + os.sep),             'scratch files never go under %s' % d
    os.makedirs(dest, exist_ok=True)
    cpp = os.path.join(dest, name or os.path.basename(src_path))
    with open(cpp, 'w', encoding='latin1', newline='') as f:
        f.write(localise_includes(text, os.path.dirname(os.path.abspath(src_path))))
    return cpp


def compile_tu(src_path, text, dest_dir, flags, timeout=120, extra=(), name=None, asm=None, first=()):
    """Compile `text` as if it were the unit at src_path.  Returns (obj path or None, compiler output).  asm: also write
    a /FAs listing there."""
    cpp = scratch_tu(src_path, text, dest_dir, name)
    obj = cpp[:-4] + '.obj'
    if os.path.exists(obj):
        os.remove(obj)
    more = ['/I' + os.path.dirname(os.path.abspath(src_path))]
    if asm:
        more += ['/FAs', '/Fa' + asm]
    rc, out, _ = run_cl(flags, *(more + list(extra) + ['/Fo' + obj, cpp]), cwd=os.path.dirname(os.path.abspath(src_path)),
                        tmp=dest_dir, timeout=timeout, first=first)
    if rc != 0 or not os.path.exists(obj):
        return None, out
    return obj, out


def compile_errors(out, limit=6):
    return [l.strip() for l in out.splitlines() if ' error ' in l or 'fatal error' in l or 'TIMEOUT' in l][:limit]


# ---------------------------------------------------------------- private checks of a modified unit

_ctx = {}


def _context():
    if not _ctx:
        import build
        _ctx['build'] = build
        _ctx['exe'], _ctx['symtab'], _ctx['libs'] = build.Exe(build.EXE), build.SymTab(), build.Libraries()
        _ctx['units'] = build.find_units()
        _orig = build.CoffObj
        cache = {}

        def cached(path):
            if os.path.normcase(path).startswith(os.path.normcase(scratch_dir('check'))):
                return _orig(path)
            try:
                st = os.stat(path)
                key = (path, st.st_mtime_ns, st.st_size)
            except OSError:
                return _orig(path)
            if key not in cache:
                cache[key] = _orig(path)
            return cache[key]
        build.CoffObj = cached
    return _ctx


class Eval:
    """Result of evaluate(): error (compiler output) or rows {va: Result} + the checker's Unit."""

    def __init__(self):
        self.error = None
        self.rows = {}
        self.unit = None
        self.obj = None
        self.out = ''

    def aligned(self, va):
        """(mismatches, mismatches ignoring stack offsets, our instructions, exe instructions) of a non-matching function."""
        c = _context()
        r = self.rows[va]
        return c['build'].aligned_counts(r, c['build'].LAST_OBJS[self.unit.name], c['exe'])

    def line(self, va):
        r = self.rows[va]
        return '%-6s %08x %5d  %-45s %s' % (r.status, r.a.va, r.size, (r.a.mangled or r.a.name or '?')[:45], r.detail)


def evaluate(src_path, text, slot='0', flags=None, timeout=120, tool='check'):
    """Compile `text` privately as the unit at src_path and check ALL its annotated functions with the real checker (names and
    relocation targets come from the other units' existing objects).  Never touches src/ or the unit's real object."""
    c = _context()
    b = c['build']
    ev = Eval()
    src_path = os.path.abspath(src_path)
    real = [u for u in c['units'] if os.path.normcase(u.path) == os.path.normcase(src_path)]
    if not real:
        raise SystemExit('%s is not a unit of module %s' % (src_path, NAME))
    real = real[0]
    d = scratch_dir('check', tool + '_%s' % slot)
    # FLAGS: of the modified text count (a variant may change them); else the unit's own
    u2_flags = None
    for i, l in enumerate(text.split('\n')[:30]):
        m = b.FLAGS_RE.match(l)
        if m:
            u2_flags = m.group(1).split()
    obj, out = compile_tu(src_path, text, d, flags or u2_flags or real.flags, timeout=timeout)
    ev.out = out
    if obj is None:
        ev.error = compile_errors(out) or out.splitlines()[-4:]
        return ev
    cpp = obj[:-4] + '.cpp'
    u2 = b.Unit(cpp)
    u2.name, u2.rel, u2.base_obj = real.name, real.rel, obj
    units = [u2 if u is real else u for u in c['units']]
    buf = io.StringIO()
    import contextlib
    with contextlib.redirect_stdout(buf):
        results, _ = b.run_check(units, c['exe'], c['symtab'], False, ['\0none'], c['libs'], scope=set())
    ev.rows = {r.a.va: r for r in results if r.a.unit is u2 and r.a.kind != 'GLOBAL'}
    ev.unit = u2
    ev.obj = obj
    return ev
