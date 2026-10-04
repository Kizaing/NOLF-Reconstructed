r"""Statistics of config/d3dren/names_proposal.csv for NAMING.md (read-only).   python tools/d3dren_names_report.py [csv]"""
import csv
import json
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_scan as S  # noqa: E402

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(S.ROOT, 'config', 'd3dren', 'names_proposal.csv')
rows = list(csv.DictReader(open(path, encoding='utf-8')))
o = S.load()
funcs = o['funcs']
lib = json.load(open(os.path.join(S.ROOT, 'config', 'd3dren', 'libraries.json')))
libf = {int(a, 16) for u in lib['units'] for a in u['functions']} & set(funcs)
print('rows', len(rows))
print('kind', Counter(r['kind'] for r in rows))
print('confidence', Counter(r['confidence'] for r in rows))
print('provenance', Counter(r['provenance'] for r in rows))
print('conf x prov', sorted(Counter((r['confidence'], r['provenance']) for r in rows).items()))
fn = [r for r in rows if r['kind'] == 'func' and int(r['address'], 16) in funcs]
nonlib = [r for r in fn if int(r['address'], 16) not in libf]
print('functions in symbols.csv', len(funcs), ' crt functions (lib match)', len(libf))
print('named funcs (all)', len(fn), 'named non-CRT funcs', len(nonlib), 'of', len(funcs) - len(libf))
real = [r for r in nonlib if not r['name'].startswith('guess_')]
print('named non-CRT excl guess_', len(real), 'by confidence', Counter(r['confidence'] for r in real))
print('named non-CRT excl guess_ and excl static-init rows',
      len([r for r in real if not r['name'].startswith('_$E_')]))
data = [r for r in rows if r['kind'] in ('data', 'vtable', 'type')]
print('data/vtable/type rows', len(data), Counter(r['kind'] for r in rows if r['kind'] != 'func'))
print('non-CRT data rows', len([r for r in data if r['provenance'] != 'crt-lib']))
sf = [r for r in nonlib if r['source_file']]
print('non-CRT funcs with source_file', len(sf), 'distinct source files', len({r['source_file'] for r in sf}))
print(Counter(r['source_file'] for r in sf).most_common(40))
