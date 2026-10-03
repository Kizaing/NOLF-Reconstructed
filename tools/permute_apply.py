r"""Transplant the function body of build/permute/<address>/min.cpp (or match.cpp) into the current source.

  python tools/permute_apply.py <src file> <hex address> [old=new ...]     (old=new: rename an identifier in the body)
"""
import os, re, sys
TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import permute

src, addr = sys.argv[1], sys.argv[2]
va = int(addr, 16)
d = os.path.join(permute.ROOT, 'build', 'permute', '%08x' % va)
m = os.path.join(d, 'min.cpp')
if not os.path.exists(m):
    m = os.path.join(d, 'match.cpp')
cur = open(src, encoding='latin1', newline='').read()
head, body, tail, _ = permute.split_source(cur, va)
_, nbody, _, _ = permute.split_source(open(m, encoding='latin1', newline='').read(), va)
for r in sys.argv[3:]:
    old, new = r.split('=')
    nbody = re.sub(r'\b%s\b' % re.escape(old), new, nbody)
open(src, 'w', encoding='latin1', newline='').write(head + nbody + tail)
print('applied %s to %s' % (os.path.basename(m), src))
