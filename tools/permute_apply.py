r"""Transplant the function body of build/permute/<address>/min.cpp (or match.cpp) into the current source.

  python tools/permute_apply.py <src file> <hex address> [--from DIR] [--file best.cpp] [old=new ...]

old=new renames an identifier in the body (the permuter's names: pServerMgrLocal, vTmp0, ...). --from takes the
candidate from another output directory (permute.py --outdir); --file picks another candidate (best.cpp is NOT
verified: read it first). Writes the source in place with LF line ends; run `build.py check <unit>` after.
"""
import os, re, sys
TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import permute

args = sys.argv[1:]
d = f = None
for opt in ('--from', '--file'):
    if opt in args:
        k = args.index(opt)
        if opt == '--from':
            d = args[k + 1]
        else:
            f = args[k + 1]
        del args[k:k + 2]
if len(args) < 2:
    sys.exit(__doc__)
src, addr = args[0], args[1]
va = int(addr, 16)
d = d or os.path.join(permute.ROOT, 'build', 'permute', '%08x' % va)
if f:
    m = os.path.join(d, f)
else:
    m = os.path.join(d, 'min.cpp')
    if not os.path.exists(m):
        m = os.path.join(d, 'match.cpp')
cur = open(src, encoding='latin1', newline='').read()
head, body, tail, _ = permute.split_source(cur, va)
_, nbody, _, _ = permute.split_source(open(m, encoding='latin1', newline='').read(), va)
for r in args[2:]:
    old, new = r.split('=')
    nbody = re.sub(r'\b%s\b' % re.escape(old), new, nbody)
open(src, 'w', encoding='latin1', newline='').write(head + nbody + tail)
print('applied %s to %s' % (m, src))
