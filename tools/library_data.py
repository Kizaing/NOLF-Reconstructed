"""Verify selected data sections from installed MS COFF libraries against the original PE."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    from .coffobj import CoffObj, SCN_LNK_COMDAT
except ImportError:  # direct script execution
    from coffobj import CoffObj, SCN_LNK_COMDAT

AR_MAGIC = b"!<arch>\n"
DIR32 = 0x06


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _fail(message):
    raise ValueError(message)


def _ar_members(data: bytes):
    """Yield validated (name, member bytes) pairs from a regular COFF archive."""
    if not data.startswith(AR_MAGIC):
        _fail("invalid archive magic")
    pos, longnames, raw = len(AR_MAGIC), b"", []
    while pos < len(data):
        if pos + 60 > len(data):
            _fail("truncated archive header")
        h = data[pos:pos + 60]
        if h[58:60] != b"`\n":
            _fail("invalid archive member header")
        try:
            size = int(h[48:58].decode("ascii").strip())
        except (ValueError, UnicodeDecodeError):
            _fail("invalid archive member size")
        start, end = pos + 60, pos + 60 + size
        if size < 0 or end > len(data):
            _fail("truncated archive member")
        namefield = h[:16].decode("ascii", "strict").rstrip()
        body = data[start:end]
        if namefield == "//":
            longnames = body
        elif namefield not in ("/", "/SYM64/"):
            if namefield.startswith("#1/"):
                try:
                    n = int(namefield[3:])
                except ValueError:
                    _fail("invalid BSD archive name")
                if n > len(body):
                    _fail("truncated BSD archive name")
                name, body = body[:n].decode("utf-8", "strict").rstrip("\0"), body[n:]
            elif namefield.startswith("/") and namefield[1:].isdigit():
                off = int(namefield[1:])
                if off >= len(longnames):
                    _fail("archive long-name offset out of range")
                stops = [x for x in (longnames.find(b"/\n", off), longnames.find(b"\0", off)) if x >= 0]
                stop = min(stops) if stops else -1
                if stop < 0:
                    _fail("unterminated archive long name")
                name = longnames[off:stop].decode("utf-8", "strict")
            else:
                name = namefield[:-1] if namefield.endswith("/") else namefield
            raw.append((name.replace("/", "\\").casefold(), body))
        pos = end + (size & 1)
    if pos != len(data):
        _fail("invalid archive padding")
    return raw


def _extract_member(archive: bytes, requested: str) -> bytes:
    matches = [body for name, body in _ar_members(archive)
               if name == requested.replace("/", "\\").casefold()]
    if len(matches) != 1:
        _fail("archive member %r matched %d entries" % (requested, len(matches)))
    return matches[0]


def _validate_coff_bounds(data: bytes, label: str):
    if len(data) < 20:
        _fail("%s: truncated COFF header" % label)
    machine, nsec, _, symoff, nsym, optsz, _ = struct.unpack_from("<HHIIIHH", data)
    if machine != 0x14C:
        _fail("%s: not i386 COFF" % label)
    sec_end = 20 + optsz + nsec * 40
    if sec_end > len(data):
        _fail("%s: truncated section table" % label)
    for i in range(nsec):
        off = 20 + optsz + i * 40
        size, raw, rel, line, nrel, nline, _ = struct.unpack_from("<IIIIHHI", data, off + 16)
        if raw and raw + size > len(data):
            _fail("%s: section raw data out of bounds" % label)
        if rel and rel + nrel * 10 > len(data):
            _fail("%s: relocation table out of bounds" % label)
        if line and line + nline * 6 > len(data):
            _fail("%s: line table out of bounds" % label)
    if symoff > len(data) or symoff + nsym * 18 + 4 > len(data):
        _fail("%s: symbol table out of bounds" % label)
    stroff = symoff + nsym * 18
    strlen = struct.unpack_from("<I", data, stroff)[0]
    if strlen < 4 or stroff + strlen > len(data):
        _fail("%s: invalid string table" % label)


def _pe_sections(image: bytes):
    if len(image) < 64 or image[:2] != b"MZ":
        _fail("original image is not a PE executable")
    peoff = struct.unpack_from("<I", image, 0x3C)[0]
    if peoff + 24 > len(image) or image[peoff:peoff + 4] != b"PE\0\0":
        _fail("invalid PE header")
    nsec, optsz = struct.unpack_from("<HxxxxxxH", image, peoff + 6)[0], struct.unpack_from("<H", image, peoff + 20)[0]
    opt = peoff + 24
    if opt + optsz > len(image) or optsz < 32:
        _fail("truncated PE optional header")
    magic = struct.unpack_from("<H", image, opt)[0]
    if magic != 0x10B:
        _fail("expected PE32 image")
    imagebase = struct.unpack_from("<I", image, opt + 28)[0]
    table = opt + optsz
    if table + nsec * 40 > len(image):
        _fail("truncated PE section table")
    secs = []
    for i in range(nsec):
        o = table + i * 40
        vsize, rva, rawsize, rawptr = struct.unpack_from("<IIII", image, o + 8)
        if rawptr + rawsize > len(image):
            _fail("PE section raw data out of bounds")
        name = image[o:o + 8].split(b"\0", 1)[0].decode("ascii", "strict")
        secs.append((name, imagebase + rva, max(vsize, rawsize), rawptr, rawsize))
    return secs


def _read_va(image: bytes, sections, va: int, size: int) -> bytes:
    for _, base, span, raw, rawsize in sections:
        if base <= va and va + size <= base + span:
            delta = va - base
            if delta + size > rawsize:
                break
            return image[raw + delta:raw + delta + size]
    _fail("VA %08x+%x is not backed by PE bytes" % (va, size))


def _align_from_flags(flags: int) -> int:
    code = (flags >> 20) & 0xF
    return 1 if code == 0 else 1 << (code - 1) if 1 <= code <= 14 else 0


def _section_comdat_selection(data: bytes, section_no: int, section_name: str) -> int:
    _, _, _, symoff, nsym, _, _ = struct.unpack_from("<HHIIIHH", data)
    found, i = [], 0
    str_off = symoff + nsym * 18
    while i < nsym:
        off = symoff + i * 18
        rawname = data[off:off + 8]
        if rawname[:4] == b"\0\0\0\0":
            name_off = struct.unpack_from("<I", rawname, 4)[0]
            if name_off < 4 or str_off + name_off >= len(data):
                _fail("section symbol name offset out of range")
            end = data.find(b"\0", str_off + name_off)
            if end < 0:
                _fail("unterminated section symbol name")
            name = data[str_off + name_off:end].decode("latin1")
        else:
            name = rawname.rstrip(b"\0").decode("latin1")
        _, secno, _, cls, naux = struct.unpack_from("<IhHBB", data, off + 8)
        value = struct.unpack_from("<I", data, off + 8)[0]
        if cls == 3 and secno == section_no and value == 0 and naux:
            if name != section_name:
                _fail("section static symbol name mismatch")
            aux = off + 18
            if aux + 18 > len(data):
                _fail("truncated section symbol auxiliary record")
            found.append(data[aux + 14])
        i += 1 + naux
    if len(found) != 1:
        _fail("section static symbol is missing or ambiguous")
    return found[0]


@dataclass
class VerifiedSection:
    unit_name: str
    metadata: dict
    native_data: bytes
    resolved_data: bytes
    section: object


@dataclass
class VerifiedUnit:
    metadata: dict
    member_path: Path
    coff: CoffObj
    sections: list[VerifiedSection]


@dataclass
class VerifiedLibraryData:
    config: dict
    units: list[VerifiedUnit]
    payload_bytes: int
    padding_bytes: int

    def sections(self):
        return [s for u in self.units for s in u.sections]


def verify_library_data(config_path="config/library_data.json", image_path=None, cache_dir="build/library_data"):
    config = json.loads(Path(config_path).read_text(encoding="utf-8"))
    if config.get("schema") != "verified-library-data-config-v1":
        _fail("unsupported library data config schema")
    image_path = Path(image_path or config["original_image"])
    image = image_path.read_bytes()
    if sha256(image) != config["original_image_sha256"]:
        _fail("original image SHA256 mismatch")
    pe_sections = _pe_sections(image)
    cache = Path(cache_dir)
    cache.mkdir(parents=True, exist_ok=True)
    intervals, declared_names, selected_symbols = [], set(), {}
    for unit in config["units"]:
        for meta in unit["sections"]:
            va, end, group = int(meta["original_va"], 16), int(meta["original_va"], 16) + meta["size"], meta["original_output_group"]
            if meta["size"] <= 0 or va < 0 or end > 0x100000000:
                _fail("%s: invalid selected section range" % unit["name"])
            for start2, end2, _, owner in intervals:
                if va < end2 and start2 < end:
                    _fail("selected output intervals overlap: %s and %s" % (unit["name"], owner))
            intervals.append((va, end, group, unit["name"]))
            if meta.get("kind") != "data":
                _fail("%s: selected library section is not typed data" % unit["name"])
            for symbol in meta["symbols"]:
                name = symbol["name"]
                if name in declared_names:
                    _fail("duplicate selected symbol name %s" % name)
                declared_names.add(name)
                selected_symbols[name] = (int(symbol["va"], 16), unit["name"], meta["coff_section_index"], symbol["offset"])
    all_sections, units = [], []
    for unit in config["units"]:
        archive_path = Path(unit["archive"])
        archive = archive_path.read_bytes()
        if sha256(archive) != unit["archive_sha256"]:
            _fail("%s: archive SHA256 mismatch" % unit["name"])
        member = _extract_member(archive, unit["member"])
        if sha256(member) != unit["member_sha256"]:
            _fail("%s: member SHA256 mismatch" % unit["name"])
        member_path = cache / (unit["member_sha256"] + ".obj")
        member_path.write_bytes(member)
        _validate_coff_bounds(member, unit["name"])
        coff = CoffObj(str(member_path))
        unit_sections = []
        for meta in unit["sections"]:
            idx = meta["coff_section_index"]
            if idx < 1 or idx > len(coff.sections):
                _fail("%s: selected section index out of range" % unit["name"])
            sec = coff.sections[idx - 1]
            size, va = meta["size"], int(meta["original_va"], 16)
            if sec.name != meta["coff_section_name"] or len(sec.data) != size:
                _fail("%s: selected section identity/size mismatch" % unit["name"])
            if sec.flags != int(meta["coff_section_flags"], 16):
                _fail("%s: selected section flags mismatch" % unit["name"])
            if _align_from_flags(sec.flags) != meta["alignment"]:
                _fail("%s: selected section alignment mismatch" % unit["name"])
            is_comdat = bool(sec.flags & SCN_LNK_COMDAT)
            selection = _section_comdat_selection(member, idx, sec.name)
            if is_comdat != meta["comdat"] or selection != meta["comdat_selection"]:
                _fail("%s: selected section COMDAT metadata mismatch" % unit["name"])
            if sha256(sec.data) != meta["native_section_sha256"]:
                _fail("%s: selected section SHA256 mismatch" % unit["name"])
            expected_symbols = meta["symbols"]
            if not expected_symbols:
                _fail("%s: selected section has no anchored symbol" % unit["name"])
            actual_names = {}
            for sym in coff.symbols.values():
                actual_names.setdefault(sym.name, []).append(sym)
            for symmeta in expected_symbols:
                name = symmeta["name"]
                found = actual_names.get(name, [])
                if len(found) != 1:
                    _fail("%s: symbol %s is not unique in native member" % (unit["name"], name))
                sym = found[0]
                if (sym.secno != idx or sym.value != symmeta["offset"] or
                    sym.cls != symmeta["storage_class"] or sym.typ != symmeta["coff_type"] or
                    not 0 <= sym.value < size or va + sym.value != int(symmeta["va"], 16)):
                    _fail("%s: symbol %s range/metadata mismatch" % (unit["name"], name))
            actual_relocs = sorted(sec.relocs)
            declared = sorted(meta["native_relocations"], key=lambda r: (r["offset"], r["type"]))
            if len(actual_relocs) != len(declared):
                _fail("%s: native relocation count mismatch" % unit["name"])
            resolved = bytearray(sec.data)
            occupied = []
            for (off, symidx, typ), rel in zip(actual_relocs, declared):
                if typ != DIR32 or rel["type"] != DIR32:
                    _fail("%s: unsupported native relocation type %x" % (unit["name"], typ))
                if off != rel["offset"]:
                    _fail("%s: native relocation offset mismatch" % unit["name"])
                if off < 0 or off + 4 > size or any(off < e and b < off + 4 for b, e in occupied):
                    _fail("%s: relocation out of range or overlapping" % unit["name"])
                occupied.append((off, off + 4))
                actual = coff.symbols.get(symidx)
                actual_name_matches = actual_names.get(rel["target_symbol"], [])
                if actual is None or len(actual_name_matches) != 1 or actual_name_matches[0].index != symidx:
                    _fail("%s: unresolved or ambiguous relocation target" % unit["name"])
                if (actual.name != rel["target_symbol"] or actual.cls != rel["target_symbol_storage_class"] or
                    actual.secno != rel["target_symbol_section"]):
                    _fail("%s: relocation target symbol metadata mismatch" % unit["name"])
                target = selected_symbols.get(actual.name)
                if target is None or int(rel["target_va"], 16) != target[0]:
                    _fail("%s: relocation target does not match a selected symbol VA" % unit["name"])
                if actual.secno > 0 and (target[1] != unit["name"] or actual.secno != target[2] or actual.value != target[3]):
                    _fail("%s: internal relocation target does not match its selected definition" % unit["name"])
                if actual.secno < 0:
                    _fail("%s: unsupported relocation target section" % unit["name"])
                addend = int.from_bytes(sec.data[off:off + 4], "little")
                field_va = va + off
                target_va = int(rel["target_va"], 16)
                if addend != rel["addend"] or int(rel["verified_original_field_va"], 16) != field_va:
                    _fail("%s: relocation addend/field evidence mismatch" % unit["name"])
                if not 0 <= target_va + addend <= 0xFFFFFFFF:
                    _fail("%s: resolved relocation value out of range" % unit["name"])
                resolved[off:off + 4] = struct.pack("<I", target_va + addend)
            if _read_va(image, pe_sections, va, size) != bytes(resolved):
                _fail("%s: resolved selected section does not match original image" % unit["name"])
            pe_name = next((name for name, base, span, _, rawsize in pe_sections
                            if base <= va and va + size <= base + span and va - base + size <= rawsize), None)
            if pe_name != meta["original_output_group"]:
                _fail("%s: declared output group does not match PE section" % unit["name"])
            entry = VerifiedSection(unit["name"], meta, sec.data, bytes(resolved), sec)
            unit_sections.append(entry)
            all_sections.append(entry)
        units.append(VerifiedUnit(unit, member_path, coff, unit_sections))
    by_group = {}
    for s in all_sections:
        group = s.metadata["original_output_group"]
        by_group[group] = by_group.get(group, 0) + s.metadata["size"]
    payload = sum(by_group.values())
    if by_group != config.get("typed_payload_bytes", {}):
        _fail("typed payload totals by output group mismatch")
    padding_by_group, padding_intervals = {}, []
    for p in config.get("padding", []):
        if p.get("kind") != "alignment_padding":
            _fail("invalid alignment padding kind")
        pva, psize = int(p["va"], 16), p["size"]
        pend = pva + psize
        if psize <= 0 or pva < 0 or pend > 0x100000000:
            _fail("invalid alignment padding range")
        for start, end in padding_intervals:
            if pva < end and start < pend:
                _fail("alignment padding intervals overlap")
        padding_intervals.append((pva, pend))
        if any(pva < end and start < pend for start, end, _, _ in intervals):
            _fail("alignment padding overlaps a selected section")
        group = p["original_output_group"]
        padding_by_group[group] = padding_by_group.get(group, 0) + psize
        b = bytes.fromhex(p["bytes"])
        pe_name = next((name for name, base, span, _, rawsize in pe_sections
                        if base <= pva and pend <= base + span and pend - base <= rawsize), None)
        if (len(b) != psize or pe_name != group or
                _read_va(image, pe_sections, pva, psize) != b):
            _fail("alignment padding evidence mismatch")
    if padding_by_group != config.get("alignment_padding_bytes", {}):
        _fail("alignment padding totals by output group mismatch")
    padding = sum(padding_by_group.values())
    return VerifiedLibraryData(config, units, payload, padding)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--config", default="config/library_data.json")
    ap.add_argument("--image")
    ap.add_argument("--cache", default="build/library_data")
    ap.add_argument("--output", default="build/library_data.json")
    args = ap.parse_args(argv)
    try:
        verified = verify_library_data(args.config, args.image, args.cache)
        report = {"schema": "verified-library-data-report-v1", "units": len(verified.units),
                  "sections": len(verified.sections()), "payload_bytes": verified.payload_bytes,
                  "padding_bytes": verified.padding_bytes,
                  "section_bytes": {name: sum(s.metadata["size"] for s in verified.sections()
                                                  if s.metadata["original_output_group"] == name)
                                    for name in sorted({s.metadata["original_output_group"] for s in verified.sections()})}}
        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("verified %d units, %d sections, %d payload bytes, %d padding bytes" %
              (report["units"], report["sections"], report["payload_bytes"], report["padding_bytes"]))
        return 0
    except (OSError, ValueError, KeyError, TypeError, struct.error) as exc:
        print("library-data verification failed: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
