r"""Cross-module gate: did a change to shared files (include/, tools/) alter either module?

  python tools/gate.py                      # full `build.py check` of both modules, compared with their baselines
  python tools/gate.py --update             # rewrite both baselines from the current numbers (after an intended change)
  python tools/gate.py --update lithtech    # rewrite one module's baseline (lithtech or d3dren)
  python tools/gate.py --only d3dren        # check one module

The modules share include/ and tools/: an engine header edit (a member added to MainWorld, WorldPoly, ...) changes the
renderer's structure layouts, and a renderer edit of a shared header changes the engine's.  Each module's baseline is the
summary of its unfiltered check (annotated and matching functions, matched bytes, STUB count/bytes, library
objects/functions/bytes): config/lithtech_gate.json and config/d3dren/gate.json.  The gate exits 1 when any number
differs or a unit fails to compile, 0 when both are equal.  An improvement is a difference too: after an intended change
(new matches), check the numbers and run --update for that module.

It compiles each module's units into this checkout's build/ and build/d3dren/ (private to the checkout) and writes no
other shared output; the two checks run in parallel.  Run it after every edit of a file in include/ or tools/ and before
every merge.
"""
import json
import os
import re
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
MODULES = ('lithtech', 'd3dren')
BASE = {'lithtech': os.path.join(ROOT, 'config', 'lithtech_gate.json'),
        'd3dren': os.path.join(ROOT, 'config', 'd3dren', 'gate.json')}


def start(module):
    env = dict(os.environ, DECOMP_LEAD='1', DECOMP_MODULE=module)
    env.pop('DECOMP_AGENT', None)
    return subprocess.Popen([sys.executable, os.path.join(TOOLS, 'build.py'), 'check'], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')


def parse(out):
    m = re.search(r'(\d+)/(\d+) annotated functions match; (\d+) of (\d+) \.text function bytes', out)
    s = re.search(r'(\d+) STUBs \((\d+) bytes\)', out)
    lib = re.search(r'libraries: (\d+) prebuilt objects, (\d+) functions, (\d+) code bytes', out)
    cf = 'COMPILE FAILED' in out
    d = {'matching': int(m.group(1)) if m else -1, 'annotated': int(m.group(2)) if m else -1,
         'match_bytes': int(m.group(3)) if m else -1, 'text_function_bytes': int(m.group(4)) if m else -1,
         'stubs': int(s.group(1)) if s else 0, 'stub_bytes': int(s.group(2)) if s else 0,
         'lib_objects': int(lib.group(1)) if lib else 0, 'lib_functions': int(lib.group(2)) if lib else 0,
         'lib_bytes': int(lib.group(3)) if lib else 0}
    return d, cf


def arg_after(flag):
    if flag in sys.argv:
        k = sys.argv.index(flag)
        if k + 1 < len(sys.argv) and sys.argv[k + 1] in MODULES:
            return sys.argv[k + 1]
    return None


def main():
    only = arg_after('--only')
    update = '--update' in sys.argv
    update_only = arg_after('--update')
    mods = [only] if only else list(MODULES)
    procs = {m: start(m) for m in mods}
    bad = False
    for mod in mods:
        out = procs[mod].communicate()[0]
        cur, cf = parse(out)
        print('%-8s now     : %s' % (mod, json.dumps(cur, sort_keys=True)))
        if update and (update_only is None or update_only == mod):
            if cf or cur['matching'] < 0:
                print('  not written: the check failed (%s)' % ('a unit failed to compile' if cf else 'no summary line'))
                bad = True
                continue
            os.makedirs(os.path.dirname(BASE[mod]), exist_ok=True)
            json.dump(cur, open(BASE[mod], 'w', newline=''), indent=1, sort_keys=True)
            print('  baseline written: %s' % os.path.relpath(BASE[mod], ROOT))
            continue
        if not os.path.exists(BASE[mod]):
            print('  no baseline (%s): run `python tools/gate.py --update %s`' % (os.path.relpath(BASE[mod], ROOT), mod))
            bad = True
            continue
        base = json.load(open(BASE[mod]))
        print('%-8s baseline: %s' % (mod, json.dumps(base, sort_keys=True)))
        diff = [k for k in sorted(base) if cur.get(k) != base[k]]
        if cf:
            print('  a %s unit failed to compile' % mod)
        for k in diff:
            print('  %s: baseline %s, now %s' % (k, base[k], cur.get(k)))
        print('  %s: %s' % (mod, 'CHANGED' if cf or diff else 'unchanged'))
        bad = bad or cf or bool(diff)
    if not update:
        print('GATE %s' % ('FAILED' if bad else 'OK: %s unchanged' % ' and '.join(mods)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
