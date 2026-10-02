"""Minimal MSF7/DBI reader: module list + section contributions (+ public/proc symbols per module).

Usage: python pdbmods.py <pdb> [out.json]
Writes {modules:[{idx,name,obj}], contribs:[{sec,off,size,mod,chars}], sections:[...], procs:[{mod,sec,off,name}]}
"""
import struct, sys, json


class MSF:
    def __init__(self, path):
        self.f = open(path, 'rb')
        hdr = self.f.read(0x38)
        assert hdr[:29] == b'Microsoft C/C++ MSF 7.00\r\n\x1aDS'
        self.bs, _fpm, self.nblocks, dirsize, _ = struct.unpack_from('<IIIII', hdr, 32)
        self.dirsize = dirsize
        nmapblocks = (dirsize + self.bs - 1) // self.bs
        nidx = (nmapblocks * 4 + self.bs - 1) // self.bs
        idxblocks = struct.unpack_from('<%dI' % nidx, hdr + self.f.read(nidx * 4), 0x34)
        mapblocks = b''.join(self.block(b) for b in idxblocks)
        dirblocks = struct.unpack_from('<%dI' % nmapblocks, mapblocks)
        d = b''.join(self.block(b) for b in dirblocks)[:dirsize]
        n = struct.unpack_from('<I', d)[0]
        sizes = struct.unpack_from('<%dI' % n, d, 4)
        pos = 4 + 4 * n
        self.streams = []
        for s in sizes:
            if s == 0xFFFFFFFF:
                s = 0
            nb = (s + self.bs - 1) // self.bs
            self.streams.append((s, struct.unpack_from('<%dI' % nb, d, pos)))
            pos += 4 * nb

    def block(self, b):
        self.f.seek(b * self.bs)
        return self.f.read(self.bs)

    def stream(self, i):
        s, blocks = self.streams[i]
        return b''.join(self.block(b) for b in blocks)[:s]


def cstr(b, p):
    e = b.index(b'\0', p)
    return b[p:e].decode('latin-1'), e + 1


def parse(path):
    m = MSF(path)
    dbi = m.stream(3)
    (sig, ver, age, gsi, bld, psi, pdbver, symrec, pdbbld, modsz, secconsz, secmapsz,
     fileisz, tsmsz, mfc, dbgsz, ecsz, flags, machine, pad) = struct.unpack_from('<iIIHHHHHHiiiiiIiiHHI', dbi, 0)
    p = 64
    mods = []
    end = p + modsz
    while p < end:
        # MODI: opened(4) SC(28) flags(2) stream(2) cbSyms(4) cbLines(4) cbC13(4) nfiles(2) pad(2) offsets(4) niSrc(4) niPdb(4)
        sc = struct.unpack_from('<HHIIIHHII', dbi, p + 4)
        stream, cbsyms, cblines, cbc13 = struct.unpack_from('<HIII', dbi, p + 34)
        q = p + 64
        name, q = cstr(dbi, q)
        obj, q = cstr(dbi, q)
        q = (q + 3) & ~3
        mods.append(dict(idx=len(mods), name=name, obj=obj, stream=stream, cbsyms=cbsyms))
        p = q
    # section contributions
    sc = dbi[end:end + secconsz]
    sver = struct.unpack_from('<I', sc)[0]
    esz = 28 if sver == 0xF12EBA2D else 32
    contribs = []
    for q in range(4, len(sc) - esz + 1, esz):
        sec, _p1, off, size, chars, mod = struct.unpack_from('<HHiiIH', sc, q)
        contribs.append(dict(sec=sec, off=off, size=size, chars=chars, mod=mod))
    # section headers from optional debug header stream
    procs = []
    for md in mods:
        if md['stream'] == 0xFFFF or md['cbsyms'] <= 4:
            continue
        s = m.stream(md['stream'])[:md['cbsyms']]
        q = 4
        while q + 4 <= len(s):
            ln, typ = struct.unpack_from('<HH', s, q)
            if ln < 2:
                break
            # S_LPROC32=0x110F S_GPROC32=0x1110 (new), 0x1146/0x1147 ID variants
            if typ in (0x110F, 0x1110, 0x1146, 0x1147):
                off, seg = struct.unpack_from('<IH', s, q + 4 + 28)
                nm, _ = cstr(s, q + 4 + 35)
                procs.append(dict(mod=md['idx'], sec=seg, off=off, name=nm, local=typ in (0x110F, 0x1146)))
            q += ln + 2
    return dict(modules=[{k: v for k, v in x.items() if k in ('idx', 'name', 'obj')} for x in mods],
                contribs=contribs, procs=procs)


if __name__ == '__main__':
    r = parse(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else None
    if out:
        json.dump(r, open(out, 'w'), indent=0)
    print(len(r['modules']), 'modules', len(r['contribs']), 'contribs', len(r['procs']), 'procs')
