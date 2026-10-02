"""Scan lithtech.exe functions (from config/symbols.csv): direct calls, immediate/displacement references
into .rdata/.data and .text, and EH handler stubs. Output build/units/scan.json."""
import os, csv, json, bisect, struct
import pefile, capstone

ROOT = r'E:\AVP2Source'
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'build', 'units')
EXE = os.path.join(ROOT, 'bin', 'lithtech.exe')
SYMS = os.path.join(HERE, '..', 'config', 'symbols.csv')


def load_symbols():
    rows = []
    with open(SYMS, newline='') as f:
        for r in csv.DictReader(f):
            rows.append(dict(addr=int(r['addr'], 16), end=int(r['end'], 16), kind=r['kind'], name=r['name'], extra=r['extra']))
    return rows


def main():
    os.makedirs(OUT, exist_ok=True)
    pe = pefile.PE(EXE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    secs = {}
    for s in pe.sections:
        nm = s.Name.rstrip(b'\0').decode()
        secs[nm] = (base + s.VirtualAddress, base + s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData), s.get_data())
    tlo, thi, text = secs['.text']
    rows = load_symbols()
    funcs = sorted([r for r in rows if r['kind'] == 'func'], key=lambda r: r['addr'])
    starts = [f['addr'] for f in funcs]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    dlo, dhi = secs['.rdata'][0], secs['.data'][1]
    out = []
    for f in funcs:
        code = text[f['addr'] - tlo:f['end'] - tlo]
        calls, drefs, trefs = set(), set(), set()
        for ins in md.disasm(code, f['addr']):
            if ins.id in (capstone.x86.X86_INS_CALL, capstone.x86.X86_INS_JMP) and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                t = ins.operands[0].imm
                if t < f['addr'] or t >= f['end']:
                    calls.add(t)
                continue
            for op in ins.operands:
                v = None
                if op.type == capstone.x86.X86_OP_IMM:
                    v = op.imm & 0xffffffff
                elif op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 or (op.type == capstone.x86.X86_OP_MEM and op.mem.disp > 0x400000):
                    v = op.mem.disp & 0xffffffff
                if v is None:
                    continue
                if dlo <= v < dhi:
                    drefs.add(v)
                elif tlo <= v < thi and not (f['addr'] <= v < f['end']):
                    trefs.add(v)
        # EH handler stubs: mov eax, imm32 ; jmp rel32
        stubs = []
        i = 0
        while True:
            i = code.find(b'\xb8', i)
            if i < 0 or i + 10 > len(code):
                break
            if code[i + 5] == 0xE9:
                fi = struct.unpack_from('<I', code, i + 1)[0]
                if dlo <= fi < dhi:
                    stubs.append([f['addr'] + i, fi])
            i += 1
        out.append(dict(addr=f['addr'], end=f['end'], name=f['name'], calls=sorted(calls), drefs=sorted(drefs),
                        trefs=sorted(trefs), stubs=stubs))
    data = [r for r in rows if r['kind'] in ('data', 'label') and dlo <= r['addr'] < dhi]
    json.dump(dict(funcs=out, data=[[d['addr'], d['end'], d['name']] for d in data],
                   sections={k: [v[0], v[1]] for k, v in secs.items()}), open(os.path.join(OUT, 'scan.json'), 'w'))
    print(len(out), 'funcs scanned')


if __name__ == '__main__':
    main()
