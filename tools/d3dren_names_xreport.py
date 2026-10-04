import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_xmatch as X
r = X.load(); ren, exe = r['ren'], r['exe']
names = X.exe_names()
lib = json.load(open(os.path.join(X.ROOT, 'config', 'd3dren', 'libraries.json')))
libf = {int(a, 16) for u in lib['units'] for a in u['functions']}
byhash = {}
for a, (h, sz, n, fn) in exe.items():
    byhash.setdefault(h, []).append(a)
rows = []
for a, (h, sz, n, fn) in sorted(ren.items()):
    if h in byhash and a not in libf:
        ms = byhash[h]
        rows.append((a, sz, n, [(m, names.get(m, exe[m][3])) for m in ms]))
print(len(rows))
for a, sz, n, ms in rows:
    print('%08x %4d %3d  %s' % (a, sz, n, ' | '.join('%08x %s' % (m, nm[:70]) for m, nm in ms[:3])))
