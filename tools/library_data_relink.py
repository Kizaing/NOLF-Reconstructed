"""Relink helpers for the narrowly verified native library-data members.

This module only handles the selected sections in config/library_data.json.  It
does not add functions to the source inventory or assign function addresses to
data-only members.
"""
from __future__ import annotations

import coffedit as F


TEXT_ANCHOR = 0x4AF960
PADDING = ((0x4C9E9C, 4, b"\0\0\0\0"),)


def enabled(mode, own_data, standin_data, only=None):
    """Native objects are used only in the default, unfiltered mixed own-data path."""
    return mode == "mixed" and own_data == "" and not standin_data and not only


def verify_default(cache_dir):
    try:
        from .library_data import verify_library_data
    except ImportError:
        from library_data import verify_library_data
    return verify_library_data(cache_dir=cache_dir)


def section_specs(verified):
    """Return selected section metadata in the manifest's proven order."""
    units = []
    order = verified.config["placement_evidence"]["order"]
    by_name = {u.metadata["name"]: u for u in verified.units}
    if set(order) != set(by_name):
        raise ValueError("native data manifest order does not cover its selected units")
    for ordinal, name in enumerate(order):
        unit = by_name[name]
        sections = sorted(unit.sections, key=lambda s: int(s.metadata["coff_section_index"]))
        units.append((ordinal, unit, sections))
    return units


def copy_selected(unit, Coff, Sym, subset_sections, target_name):
    """Copy the selected COFF sections and bind external relocs by verified target VA."""
    src = Coff.load(str(unit.member_path))
    metas = sorted(unit.sections, key=lambda s: int(s.metadata["coff_section_index"]))
    secnos = [int(s.metadata["coff_section_index"]) for s in metas]
    if len(set(secnos)) != len(secnos) or any(k < 1 or k > len(src.sections) for k in secnos):
        raise ValueError("%s: selected COFF sections are invalid" % unit.metadata["name"])
    out = subset_sections(src, secnos)
    newno = {old: i + 1 for i, old in enumerate(secnos)}
    by_section = {int(m.metadata["coff_section_index"]): m for m in metas}
    selected_defs = {(s["name"], int(s["va"], 16))
                     for m in metas for s in m.metadata["symbols"]}
    new_symbols = {}

    def symbol_for(name):
        if name not in new_symbols:
            new_symbols[name] = len(out.syms)
            out.syms.append(Sym(name, 0, 0, 0, F.CLS_EXTERNAL))
        return new_symbols[name]

    for oldno, meta in by_section.items():
        # The verifier checked each relocation against the archive member and original image.
        declared = {(int(r["offset"]), int(r["type"])): r for r in meta.metadata["native_relocations"]}
        old_sec = src.sections[oldno - 1]
        new_sec = out.sections[newno[oldno] - 1]
        old_relocs = {(off, typ): si for off, si, typ in old_sec.relocs}
        if set(old_relocs) != set(declared):
            raise ValueError("%s: relocation metadata changed after verification" % unit.metadata["name"])
        for rel in new_sec.relocs:
            off, si, typ = rel
            evidence = declared[(off, typ)]
            old_si = old_relocs[(off, typ)]
            old_sym = src.syms[old_si]
            target_va = int(evidence["target_va"], 16)
            internal = old_sym.sec in newno and old_sym.sec > 0
            if internal:
                if (old_sym.name, target_va) not in selected_defs:
                    raise ValueError("%s: internal relocation does not name a selected definition" % unit.metadata["name"])
                continue
            canonical = target_name(target_va)
            if canonical is None:
                raise ValueError("%s: no canonical name for relocation target %08x" % (unit.metadata["name"], target_va))
            rel[1] = symbol_for(canonical)

    # Validate selected definitions survived section filtering with their manifest offsets.
    for meta in metas:
        oldno = int(meta.metadata["coff_section_index"])
        for spec in meta.metadata["symbols"]:
            name, value = spec["name"], int(spec["offset"])
            if not any(s is not None and s.name == name and s.sec == newno[oldno] and s.value == value
                       for s in out.syms):
                raise ValueError("%s: selected definition %s was not preserved" % (unit.metadata["name"], name))
    return out, newno


def native_aliases(verified):
    """Map selected symbol VA to original linker name; reject duplicate conflicting aliases."""
    aliases = {}
    for sec in verified.sections():
        va0 = int(sec.metadata["original_va"], 16)
        for sym in sec.metadata["symbols"]:
            va = int(sym["va"], 16)
            name = sym["name"]
            if va != va0 + int(sym["offset"]):
                raise ValueError("%s: selected symbol VA/offset mismatch" % sec.unit_name)
            if va in aliases and aliases[va] != name:
                raise ValueError("conflicting native aliases at %08x" % va)
            aliases[va] = name
    return aliases


def selected_intervals(verified):
    return sorted((int(s.metadata["original_va"], 16),
                   int(s.metadata["original_va"], 16) + int(s.metadata["size"]), s.unit_name)
                  for s in verified.sections() if s.metadata["original_output_group"] == ".rdata")


def subtract_native_spans(pieces, intervals, padding=PADDING):
    """Remove verified native .rdata payload and alignment bytes from copied stand-in pieces."""
    cuts = sorted([(lo, hi, owner) for lo, hi, owner in intervals] +
                  [(lo, lo + size, "alignment padding") for lo, size, _ in padding])
    out = []
    for key, group, lo, hi, owner in pieces:
        if group != ".rdata":
            out.append((key, group, lo, hi, owner))
            continue
        cursor = lo
        for cut_lo, cut_hi, label in cuts:
            if cut_hi <= cursor or cut_lo >= hi:
                continue
            if cut_lo > cursor:
                out.append((key, group, cursor, min(cut_lo, hi), owner))
            cursor = max(cursor, cut_hi)
            if cursor >= hi:
                break
        if cursor < hi:
            out.append((key, group, cursor, hi, owner))
    return [p for p in out if p[3] > p[2]]


def make_table_seed(verified, Coff, subset_sections):
    """A temporary .text-only coverage object so add_gaps does not copy the joystick table."""
    matches = [s for s in verified.sections() if s.metadata["original_output_group"] == ".text"]
    if len(matches) != 1 or int(matches[0].metadata["original_va"], 16) != TEXT_ANCHOR:
        raise ValueError("expected one verified DirectInput table at %08x" % TEXT_ANCHOR)
    spec = matches[0]
    unit = next(u for u in verified.units if u.metadata["name"] == spec.unit_name)
    source = Coff.load(str(unit.member_path))
    original = source.sections[int(spec.metadata["coff_section_index"]) - 1]
    if original.name != ".text" or len(original.data) != int(spec.metadata["size"]):
        raise ValueError("verified table section does not match its manifest extent")
    c = subset_sections(source, [int(spec.metadata["coff_section_index"])])
    return c, spec, unit


def verified_padding(verified):
    padding = tuple((int(p["va"], 16), int(p["size"]), bytes.fromhex(p["bytes"]))
                    for p in verified.config.get("padding", []))
    if padding != PADDING:
        raise ValueError("verified library-data padding differs from the supported native interval")
    if sum(size for _, size, _ in padding) != verified.padding_bytes:
        raise ValueError("verified padding byte count is inconsistent")
    return padding


def verify_linked_output(path, verified):
    """Fail closed unless native payload, relocation results, and proved padding survived LINK."""
    try:
        import mktarget
        img = mktarget._image(path)
        relocation_count = 0
        for sec in verified.sections():
            va = int(sec.metadata["original_va"], 16)
            got = img.read(va, int(sec.metadata["size"]))
            if got != sec.resolved_data:
                raise ValueError("linked native data differs at %08x (%s)" % (va, sec.unit_name))
            for rel in sec.metadata["native_relocations"]:
                relocation_count += 1
                off, typ = int(rel["offset"]), int(rel["type"])
                target, addend = int(rel["target_va"], 16), int(rel["addend"])
                if typ == 6:
                    expected = (target + addend) & 0xffffffff
                elif typ == 20:
                    expected = (target + addend - (va + off + 4)) & 0xffffffff
                else:
                    raise ValueError("unsupported verified relocation type %x" % typ)
                if int.from_bytes(got[off:off + 4], "little") != expected:
                    raise ValueError("linked relocation target differs at %08x+%x" % (va, off))
        if relocation_count != 13:
            raise ValueError("expected 13 selected native pointer relocations, found %d" % relocation_count)
        for va, size, expected in verified_padding(verified):
            if img.read(va, size) != expected:
                raise ValueError("linked native alignment padding differs at %08x" % va)
    except Exception as e:
        raise ValueError("native library-data post-link verification failed: %s" % e) from e
