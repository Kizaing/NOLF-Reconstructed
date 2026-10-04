"""Verify a mixed relink instruction by instruction: every function whose bytes differ from the original must be
the same code with its address operands (call/jump targets, absolute memory and immediate addresses) moved
consistently with the map.

  python tools/relink_verify.py [build/relink-run]

For each function in the relink's lithtech.map, the new and the original listing are compared instruction by
instruction; an operand that is an address must equal the original operand translated old -> new (functions by
the map, everything else unchanged). Prints the first mismatch per function.
"""
import json, os, re, sys

import capstone
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main(argv):
    d = argv[0] if argv else os.path.join(ROOT, 'build', 'relink-run')
    byname = {v: int(k, 16) for k, v in json.load(open(os.path.join(ROOT, 'build', 'namemap.json'))).items()}
    funcs = []                          # (new va, old va or None, name)
    for l in open(os.path.join(d, 'lithtech.map'), encoding='latin1'):
        m = re.match(r'\s*0001:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+f?\s', l)
        if m:
            nm, new = m.group(1), int(m.group(2), 16)
            at = re.search(r'@([0-9a-f]{8})$', nm)
            old = int(at.group(1), 16) if at else byname.get(nm, byname.get(nm.lstrip('_')))
            funcs.append((new, old, nm))
    funcs.sort(key=lambda t: t[0])
    old2new = {o: n for n, o, _ in funcs if o is not None}
    # every old function start must map; labels inside a moved function move with it
    olds = sorted((o, n) for n, o, _ in funcs if o is not None)
    import bisect
    ol = [x[0] for x in olds]

    def tr(v):
        if v in old2new:
            return old2new[v]
        j = bisect.bisect_right(ol, v) - 1
        if j >= 0 and v - olds[j][0] < 0x2000 and (j + 1 >= len(ol) or v < ol[j + 1]):
            return olds[j][1] + (v - olds[j][0])
        return v

    new_img = pefile.PE(os.path.join(d, 'lithtech.exe')).get_memory_mapped_image()
    old_img = pefile.PE(r'E:\AVP2Source\bin\lithtech.exe').get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    X = capstone.x86
    bad = checked = 0
    for i, (new, old, nm) in enumerate(funcs):
        if old is None:
            continue
        end = funcs[i + 1][0] if i + 1 < len(funcs) else new + 0x100
        n = end - new
        if new == old and new_img[new - 0x400000:end - 0x400000] == old_img[old - 0x400000:end - 0x400000]:
            continue
        a = list(md.disasm(new_img[new - 0x400000:new - 0x400000 + n], new))
        b = list(md.disasm(old_img[old - 0x400000:old - 0x400000 + n], old))
        checked += 1
        for x, y in zip(a, b):
            if x.mnemonic != y.mnemonic or len(x.operands) != len(y.operands):
                if x.mnemonic in ('int3', 'nop') or y.mnemonic in ('int3', 'nop'):
                    break                   # padding after the function
                print('%-50s %08x/%08x: %s %s | %s %s' % (nm[:50], x.address, y.address, x.mnemonic, x.op_str,
                                                           y.mnemonic, y.op_str))
                bad += 1
                break
            diff = None
            for p, q in zip(x.operands, y.operands):
                if p.type != q.type:
                    diff = 'operand type'
                elif p.type == X.X86_OP_IMM:
                    pv, qv = p.imm & 0xffffffff, q.imm & 0xffffffff
                    if pv != qv and pv != tr(qv):
                        diff = '%08x vs %08x (expected %08x)' % (pv, qv, tr(qv))
                elif p.type == X.X86_OP_MEM:
                    pv, qv = p.mem.disp & 0xffffffff, q.mem.disp & 0xffffffff
                    if pv != qv and pv != tr(qv):
                        diff = 'mem %08x vs %08x (expected %08x)' % (pv, qv, tr(qv))
                elif p.type == X.X86_OP_REG and p.reg != q.reg:
                    diff = 'register'
                if diff:
                    break
            if diff:
                print('%-50s %08x/%08x: %s %s | %s %s  [%s]' % (nm[:50], x.address, y.address, x.mnemonic, x.op_str,
                                                                y.mnemonic, y.op_str, diff))
                bad += 1
                break
            if x.mnemonic == 'ret' and x.address + x.size >= end:
                break
    print('%d differing functions checked, %d with a mismatch' % (checked, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
