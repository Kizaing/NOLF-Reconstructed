"""Narrow integration of verified source EH sections into the mixed relink.

The verifier owns the trust decision.  This module only builds a fresh plan
from its MATCH/reachable results, preserves the relevant base-object sections,
and checks the linked result against that plan.
"""
from __future__ import annotations

import hashlib
import os
import re
import struct

import coffedit as F


UNIT_ORDER = (
    'lithshared/rezmgr/rezmgr',
    'lithshared/rezmgr/rezfile',
    'lithshared/stdlith/stringholder',
    'lithshared/stdlith/l_allocator',
    'lithshared/controlfilemgr/controlfilemgr',
    'lithshared/lith/basehash',
)
TEXT_START, TEXT_END = 0x4C51D0, 0x4C54B5
XDATA_START, XDATA_END = 0x4CD028, 0x4CD37C
EXPECTED_TEXT_SECTIONS, EXPECTED_TEXT_BYTES = 21, 573
EXPECTED_XDATA_SECTIONS, EXPECTED_XDATA_BYTES = 21, 852
EXPECTED_VERIFIED_RELOCS = 150


def enabled(mode, own_data, standin_data, only=None):
    """Keep source EH under exactly the existing native-library-data gate."""
    import library_data_relink as LDR
    return LDR.enabled(mode, own_data, standin_data, only)


def build_plan(prep, units, full):
    """Run a fresh verifier pass and construct the selected integration plan."""
    import source_eh as EH
    return SourceEHPlan(prep, units, full, EH.verify_sources(sorted(full)))


def _sha256(path):
    absolute = os.path.abspath(path)
    before = _stat_signature(absolute)
    with open(absolute, 'rb') as f:
        content = f.read()
    after = _stat_signature(absolute)
    if before != after or len(content) != after[2]:
        raise ValueError('source EH base object changed while reading: %s' % absolute)
    return absolute, hashlib.sha256(content).hexdigest()


def _stat_signature(path):
    st = os.stat(path)
    return (st.st_dev, st.st_ino, st.st_size,
            getattr(st, 'st_mtime_ns', int(st.st_mtime * 1000000000)),
            getattr(st, 'st_ctime_ns', int(st.st_ctime * 1000000000)))


def _alignment(sec):
    encoded = (sec.flags >> 20) & 0xF
    return 1 << (encoded - 1) if encoded else 16


def _align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def _section_symbol(coff, secno):
    hits = [s for s in coff.syms if s is not None and s.sec == secno and
            s.cls == F.CLS_STATIC and s.naux and s.name.startswith('.')]
    if len(hits) != 1:
        raise ValueError('source EH section %d has %d section symbols' % (secno, len(hits)))
    return hits[0]


def _same_path(a, b):
    return os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b))


class SourceEHPlan:
    """Fresh, fail-closed source EH selection and relocation plan."""

    def __init__(self, prep, units, full, report):
        self.prep = prep
        self.orig = prep.orig
        self.report = report
        if not hasattr(prep, 'used_names'):
            prep.used_names = set(prep.text_name.values()) | set(prep.data_name.values())
        self.units = {}
        self.text_sections = []
        self.xdata_sections = []
        self.root_relocations = []
        self.required_text_targets = set()
        self.required_data_targets = set()
        self.text_gaps = []
        self.text_start = TEXT_START
        self.text_end = TEXT_END
        self.xdata_start = XDATA_START
        self.xdata_end = XDATA_END
        self._prepare(units, full)

    def _prepare(self, units, full):
        from coffedit import Coff
        import source_eh as EH

        by_unit = {u.name: u for u in units}
        full_set = set(full)
        reports = {r['unit']: r for r in self.report.get('units', [])}
        unknown = set(reports) - full_set
        if unknown:
            raise ValueError('source EH verifier returned non-full units: %s' % ', '.join(sorted(unknown)))
        if set(reports) != set(UNIT_ORDER):
            raise ValueError('source EH selected unit set changed: expected %s, got %s' % (
                ', '.join(UNIT_ORDER), ', '.join(sorted(reports))))

        totals = self.report.get('totals', {})
        expected_totals = {
            'text_helper_sections': EXPECTED_TEXT_SECTIONS,
            'text_helper_bytes': EXPECTED_TEXT_BYTES,
            'xdata_sections': EXPECTED_XDATA_SECTIONS,
            'xdata_bytes': EXPECTED_XDATA_BYTES,
            'verified_relocations': EXPECTED_VERIFIED_RELOCS,
            'unverified_relocations': 0,
        }
        for field, expected in expected_totals.items():
            if totals.get(field) != expected:
                raise ValueError('source EH %s changed: expected %d, got %r' %
                                 (field, expected, totals.get(field)))

        for unit_name in UNIT_ORDER:
            if unit_name not in by_unit:
                raise ValueError('source EH unit is absent from inventory: %s' % unit_name)
            result = reports[unit_name]
            if result.get('status') != 'MATCH':
                raise ValueError('%s: source EH graph status is %s' % (unit_name, result.get('status')))
            if result.get('alias_conflicts'):
                raise ValueError('%s: source EH function alias conflicts are present' % unit_name)
            roots = result.get('roots', [])
            bad_roots = [r for r in roots if r.get('status') != 'MATCH' or r.get('errors')]
            if bad_roots:
                raise ValueError('%s: source EH root dependency graph is not a strict MATCH' % unit_name)
            if result.get('unverified'):
                raise ValueError('%s: source EH dependency graph has unverified relocations' % unit_name)

            selected = {}
            reachable = set()
            for key, ent in result.get('sections', {}).items():
                if ent.get('reachable_from_match_root'):
                    secno = int(key)
                    reachable.add(secno)
                    if ent.get('status') != 'MATCH' or ent.get('anchor') is None:
                        raise ValueError('%s section %s is reachable but not a placed MATCH' % (unit_name, key))
                    if not (ent.get('name', '').startswith('.text$x') or
                            ent.get('name', '').startswith('.xdata$x')):
                        raise ValueError('%s section %s has unsupported EH name %r' %
                                         (unit_name, key, ent.get('name')))
                    selected[secno] = ent
            if not selected:
                raise ValueError('%s: verifier returned no reachable EH sections' % unit_name)
            graph_errors = [e for e in result.get('errors', [])
                            if isinstance(e.get('section'), int) and e['section'] in reachable]
            if graph_errors:
                raise ValueError('%s: selected source EH dependency graph contains verifier errors' % unit_name)
            if result.get('counts', {}).get('unverified_relocations'):
                raise ValueError('%s: source EH report has unknown relocations' % unit_name)

            provenance = result.get('source_object')
            if not isinstance(provenance, dict) or not provenance.get('base_obj') or not provenance.get('sha256'):
                raise ValueError('%s: source EH verifier did not provide an object fingerprint' % unit_name)
            u = by_unit[unit_name]
            if not _same_path(provenance['base_obj'], u.base_obj):
                raise ValueError('%s: verifier fingerprint names a different base object' % unit_name)
            path, digest = _sha256(u.base_obj)
            if digest.lower() != str(provenance['sha256']).lower():
                raise ValueError('%s: base object changed after source EH verification' % unit_name)
            before_load = _stat_signature(path)
            coff = Coff.load(path)
            after_load = _stat_signature(path)
            _, digest_after_load = _sha256(path)
            if before_load != after_load or digest_after_load.lower() != digest.lower():
                raise ValueError('%s: base object changed while loading the verified COFF sections' % unit_name)
            section_specs = {}
            for secno, ent in selected.items():
                if secno < 1 or secno > len(coff.sections):
                    raise ValueError('%s: selected source EH section %d is out of range' % (unit_name, secno))
                sec = coff.sections[secno - 1]
                anchor = int(ent['anchor'])
                if sec.name != ent['name'] or len(sec.data) != int(ent['size']):
                    raise ValueError('%s section %d no longer matches its verification record' % (unit_name, secno))
                if not (sec.flags & F.SCN_LNK_COMDAT):
                    raise ValueError('%s section %d is not a COMDAT' % (unit_name, secno))
                ss = _section_symbol(coff, secno)
                if len(ss.aux) < 18 or ss.aux[14] != 5:
                    raise ValueError('%s section %d is not associative COMDAT selection 5' % (unit_name, secno))
                parent = struct.unpack_from('<H', ss.aux, 12)[0]
                if not (1 <= parent <= len(coff.sections)) or parent == secno:
                    raise ValueError('%s section %d has invalid associative parent %d' % (unit_name, secno, parent))
                if not (coff.sections[parent - 1].flags & F.SCN_CNT_CODE) or \
                        coff.sections[parent - 1].name.startswith(('.text$x', '.xdata$x')):
                    raise ValueError('%s section %d is not associated with an ordinary code section' %
                                     (unit_name, secno))
                al = _alignment(sec)
                if anchor % al:
                    raise ValueError('%s section %d anchor %08x violates its %d-byte alignment' %
                                     (unit_name, secno, anchor, al))
                entry = {
                    'unit': unit_name, 'secno': secno, 'name': sec.name, 'anchor': anchor,
                    'size': len(sec.data), 'section': sec, 'parent': parent,
                    'alignment': al, 'start_alias': self._start_alias(unit_name, secno, sec.name),
                    'relocations': {},
                }
                section_specs[secno] = entry
                (self.text_sections if sec.name.startswith('.text$x') else self.xdata_sections).append(entry)

            self.units[unit_name] = {
                'unit': u, 'path': path, 'sha256': digest, 'coff': coff,
                'sections': section_specs, 'roots': roots, 'root_relocations': [],
                'retained_section_numbers': [], 'output_section_numbers': {},
                'root_link_names': {},
            }

        self.text_sections.sort(key=lambda s: (s['anchor'], s['unit'], s['secno']))
        self.xdata_sections.sort(key=lambda s: (s['anchor'], s['unit'], s['secno']))
        self._validate_layout()
        self._derive_relocations(EH)
        self._collect_required_targets()
        self.text_helper_sections = len(self.text_sections)
        self.text_helper_bytes = sum(s['size'] for s in self.text_sections)
        self.xdata_sections_count = len(self.xdata_sections)
        self.xdata_bytes = sum(s['size'] for s in self.xdata_sections)
        self.root_relocation_count = len(self.root_relocations)
        self.helper_relocation_count = sum(len(s['relocations']) for s in self.text_sections + self.xdata_sections)
        if (self.text_helper_sections, self.text_helper_bytes,
                self.xdata_sections_count, self.xdata_bytes) != (
                    EXPECTED_TEXT_SECTIONS, EXPECTED_TEXT_BYTES,
                    EXPECTED_XDATA_SECTIONS, EXPECTED_XDATA_BYTES):
            raise ValueError('fresh source EH section instances differ from the supported 21/573 and 21/852 plan')
        if self.root_relocation_count + self.helper_relocation_count != EXPECTED_VERIFIED_RELOCS:
            raise ValueError('fresh source EH relocation plan differs from the supported %d verified relocations' %
                             EXPECTED_VERIFIED_RELOCS)

    @staticmethod
    def _start_alias(unit, secno, secname):
        kind = 'text' if secname.startswith('.text$x') else 'xdata'
        safe = re.sub(r'[^A-Za-z0-9_]', '_', unit)
        return '__source_eh_%s_%s_%d' % (kind, safe, secno)

    def _canonicalize_target(self, va):
        if self.orig.text_lo <= va < self.orig.text_hi:
            if va not in self.prep.text_name:
                self.prep.add_code_symbol(va)
            name = self.prep.text_name.get(va)
            if not name:
                raise ValueError('source EH code target %08x has no canonical text alias' % va)
            self.required_text_targets.add(va)
            return name
        name = self.prep.canon_target(va)
        if not name:
            raise ValueError('source EH data target %08x has no canonical alias' % va)
        self.required_data_targets.add(va)
        return name

    def _validate_layout(self):
        if not self.text_sections or not self.xdata_sections:
            raise ValueError('source EH plan is missing helper code or metadata')
        if min(s['anchor'] for s in self.text_sections) != TEXT_START:
            raise ValueError('source EH text tail no longer starts at %08x' % TEXT_START)
        cursor = TEXT_START
        self.text_gaps = []
        for ent in self.text_sections:
            lo, hi = ent['anchor'], ent['anchor'] + ent['size']
            if lo < cursor:
                raise ValueError('overlapping source EH text sections at %08x' % lo)
            expected = _align(cursor, ent['alignment'])
            if lo != expected:
                raise ValueError('source EH text section at %08x follows unexpected gap from %08x' % (lo, cursor))
            if lo > cursor:
                padding = self.orig.img.read(cursor, lo - cursor)
                if padding != b'\xCC' * (lo - cursor):
                    raise ValueError('source EH alignment gap %08x-%08x is not INT3 padding' % (cursor, lo))
                self.text_gaps.append((cursor, lo))
            original = self.orig.img.read(lo, ent['size'])
            mask = set()
            for off, _, _ in ent['section'].relocs:
                if off < 0 or off + 4 > ent['size']:
                    raise ValueError('%s section %d has relocation outside its payload' %
                                     (ent['unit'], ent['secno']))
                mask.update(range(off, off + 4))
            bad = next((i for i, b in enumerate(ent['section'].data)
                        if i not in mask and original[i] != b), None)
            if bad is not None:
                raise ValueError('source EH section bytes differ at %08x+%x' % (lo, bad))
            cursor = hi
        if cursor != TEXT_END:
            raise ValueError('source EH text helpers end at %08x, expected %08x' % (cursor, TEXT_END))
        if self.orig.text_end != TEXT_END:
            raise ValueError('original text end changed from %08x to %08x' % (TEXT_END, self.orig.text_end))

        cursor = XDATA_START
        for ent in self.xdata_sections:
            lo, hi = ent['anchor'], ent['anchor'] + ent['size']
            if lo != cursor:
                raise ValueError('source EH metadata is not contiguous at %08x (next section at %08x)' %
                                 (cursor, lo))
            cursor = hi
        if cursor != XDATA_END:
            raise ValueError('source EH metadata ends at %08x, expected %08x' % (cursor, XDATA_END))

    def _derive_relocations(self, EH):
        """Recover targets only after every selected graph and layout has passed."""
        for unit_name in UNIT_ORDER:
            up = self.units[unit_name]
            coff = up['coff']
            for secno, ent in up['sections'].items():
                sec = coff.sections[secno - 1]
                for off, target, typ, addend in EH._relocations(coff, secno, 0, len(sec.data)):
                    target_va = EH._image_target(self.orig.img, ent['anchor'] + off, typ, addend)
                    if target_va is None:
                        raise ValueError('%s section %d relocation +%x has no original target VA' %
                                         (unit_name, secno, off))
                    target_name = None if target.sec == -1 else self._canonicalize_target(target_va)
                    ent['relocations'][off] = {
                        'target_va': target_va, 'target_name': target_name, 'type': typ,
                        'addend': addend, 'symbol_index': coff.syms.index(target),
                        'symbol_name': target.name, 'symbol_sec': target.sec,
                        'symbol_value': target.value,
                    }

            root_relocs = []
            for root in up['roots']:
                sym = EH._symbol(coff, root['symbol'])
                size = int(root['size'])
                for off, target, typ, addend in EH._relocations(coff, sym.sec, sym.value, sym.value + size):
                    if target.sec not in up['sections']:
                        continue
                    site = int(root['va']) + off - sym.value
                    target_va = EH._image_target(self.orig.img, site, typ, addend)
                    if target_va is None:
                        raise ValueError('%s root %s relocation +%x has no original target VA' %
                                         (unit_name, root['symbol'], off - sym.value))
                    target_name = self._canonicalize_target(target_va)
                    root_relocs.append({
                        'unit': unit_name, 'symbol': root['symbol'], 'va': int(root['va']),
                        'size': size, 'section': sym.sec, 'symbol_value': sym.value,
                        'offset': off - sym.value, 'site_va': site, 'target_va': target_va,
                        'target_name': target_name, 'type': typ, 'addend': addend,
                        'target_sec': target.sec, 'target_symbol': target.name,
                    })
            up['root_relocations'] = root_relocs
            self.root_relocations.extend(root_relocs)

    def _collect_required_targets(self):
        for up in self.units.values():
            for ent in up['sections'].values():
                for rel in ent['relocations'].values():
                    if rel['target_name'] is None:
                        continue
                    if self.orig.text_lo <= rel['target_va'] < self.orig.text_hi:
                        self.required_text_targets.add(rel['target_va'])
                    else:
                        self.required_data_targets.add(rel['target_va'])
            for rel in up['root_relocations']:
                self.required_text_targets.add(rel['target_va'])

    def unit(self, name):
        return self.units.get(name)

    def section_numbers(self, name):
        up = self.units.get(name)
        return sorted(up['sections']) if up else []

    def relocation(self, unit_name, secno, off):
        try:
            return self.units[unit_name]['sections'][secno]['relocations'][off]
        except KeyError as exc:
            raise ValueError('%s section %d relocation +%x is absent from the fresh source EH plan' %
                             (unit_name, secno, off)) from exc

    def assert_object_unchanged(self, unit_name, path):
        up = self.units.get(unit_name)
        if not up:
            return
        if not _same_path(path, up['path']):
            raise ValueError('%s: source EH base object path changed during relink' % unit_name)
        _, digest = _sha256(path)
        if digest.lower() != up['sha256'].lower():
            raise ValueError('%s: source EH base object changed after plan creation' % unit_name)

    @staticmethod
    def _add_alias(coff, name, secno, offset):
        exact = [s for s in coff.syms if s is not None and s.name == name and s.sec == secno and
                 s.value == offset and s.cls == F.CLS_EXTERNAL]
        if exact:
            return
        other = [s for s in coff.syms if s is not None and s.name == name and s.sec > 0 and
                 (s.sec != secno or s.value != offset) and s.cls == F.CLS_EXTERNAL]
        if other:
            raise ValueError('source EH alias %s is already defined elsewhere in the base object' % name)
        coff.add_symbol(name, offset, secno, 0, F.CLS_EXTERNAL)

    def extend_base_unit(self, prep, unit_name, coff, ordinary_code, funcs):
        """Append native EH sections and aliases to one prepared base object."""
        up = self.units.get(unit_name)
        # Give every required code target in an ordinary section of this object
        # a public alias. This keeps map-based validation independent of local
        # symbol visibility while the input relocation itself remains internal.
        for secno in ordinary_code:
            entries = funcs.get(secno, [])
            bases = {int(va) - int(value) for value, _, va in entries}
            if len(bases) > 1:
                raise ValueError('%s ordinary code section %d has inconsistent function anchors' %
                                 (unit_name, secno))
            if not bases:
                continue
            base = next(iter(bases))
            size = len(coff.sections[secno - 1].data)
            for va in sorted(self.required_text_targets):
                offset = va - base
                if 0 <= offset < size:
                    self._add_alias(coff, prep.text_name[va], secno, offset)

        if up is None:
            return list(ordinary_code)
        self.assert_object_unchanged(unit_name, up['path'])
        eh_sections = sorted(up['sections'])
        retained = list(ordinary_code) + eh_sections
        for ent in up['sections'].values():
            original_sec = ent['section']
            actual_sec = coff.sections[ent['secno'] - 1]
            if (actual_sec.name != original_sec.name or actual_sec.data != original_sec.data or
                    actual_sec.flags != original_sec.flags or actual_sec.relocs != original_sec.relocs):
                raise ValueError('%s section %d changed after the verified source object was loaded' %
                                 (unit_name, ent['secno']))
            actual_parent = struct.unpack_from('<H', _section_symbol(coff, ent['secno']).aux, 12)[0]
            if actual_parent != ent['parent']:
                raise ValueError('%s section %d associative parent changed after verification' %
                                 (unit_name, ent['secno']))
            if ent['parent'] not in ordinary_code:
                raise ValueError('%s section %d parent code section %d is not in ordinary retained code' %
                                 (unit_name, ent['secno'], ent['parent']))

        prep.own_spans = getattr(prep, 'own_spans', [])
        prep.own_names = getattr(prep, 'own_names', set())
        prep.own_secno = getattr(prep, 'own_secno', {})
        up['retained_section_numbers'] = retained
        up['output_section_numbers'] = {old: new for new, old in enumerate(retained, 1)}

        for ent in up['sections'].values():
            secno = ent['secno']
            lo, hi = ent['anchor'], ent['anchor'] + ent['size']
            self._add_alias(coff, ent['start_alias'], secno, 0)
            for va, name in sorted(prep.text_name.items()):
                if lo <= va < hi:
                    self._add_alias(coff, name, secno, va - lo)
            for va, name in sorted(prep.data_name.items()):
                if lo <= va < hi:
                    self._add_alias(coff, name, secno, va - lo)
                    prep.own_names.add(va)
            # own_data() and its late-alias pass subset the same prefix. These
            # entries let later data names land in native .xdata$x too.
            outno = up['output_section_numbers'][secno]
            prep.own_spans.append((lo, hi, unit_name, secno))
            prep.own_secno[(unit_name, secno)] = outno
        return retained

    def record_function_symbol(self, unit_name, source_name, va, linked_name):
        up = self.units.get(unit_name)
        if not up:
            return
        key = (source_name, int(va))
        if any((r['symbol'], int(r['va'])) == key for r in up['roots']):
            prior = up['root_link_names'].get(key)
            if prior is not None and prior != linked_name:
                raise ValueError('%s root %s has conflicting linked names' % (unit_name, source_name))
            up['root_link_names'][key] = linked_name

    def remove_target_tail(self, prep, subset_sections):
        """Remove target coverage for the verified helper tail after canonicalization."""
        lo, hi = self.text_start, self.text_end
        if not hasattr(prep, 'used_names'):
            prep.used_names = set(prep.text_name.values()) | set(prep.data_name.values())

        sections = []
        for name in list(prep.objs):
            if name not in prep.objvas:
                continue
            for va, secno, size in prep.sections(name):
                end = va + size
                if va < hi and end > lo:
                    if va < lo < end or va < hi < end:
                        raise ValueError('%s section %d unexpectedly straddles source EH tail cutoff' % (name, secno))
                    sec = prep.objs[name].sections[secno - 1]
                    if not (sec.flags & F.SCN_CNT_CODE):
                        raise ValueError('%s section %d in source EH tail is not code' % (name, secno))
                    original = prep.orig.img.read(va, size)
                    reloc_bytes = set()
                    for off, symidx, typ in sec.relocs:
                        if off < 0 or off + 4 > size:
                            raise ValueError('%s section %d has relocation outside its target payload' %
                                             (name, secno))
                        if typ not in (0x06, 0x14):
                            raise ValueError('%s section %d has unsupported tail relocation type %x' %
                                             (name, secno, typ))
                        if prep.objs[name].syms[symidx] is None:
                            raise ValueError('%s section %d relocation targets an auxiliary symbol slot' %
                                             (name, secno))
                        reloc_bytes.update(range(off, off + 4))
                    bad = next((i for i, b in enumerate(sec.data)
                                if i not in reloc_bytes and original[i] != b), None)
                    if bad is not None:
                        raise ValueError('%s section %d in source EH tail differs from the original image' %
                                         (name, secno))
                    sections.append((name, secno, va, end))

        # The target sections and the native payloads together must explain the
        # entire tail; any uncovered bytes are accepted only as verified INT3
        # alignment fill from the native section alignments.
        cursor = lo
        intervals = sorted((s['anchor'], s['anchor'] + s['size']) for s in self.text_sections)
        tail_gaps = []
        for start, end in intervals:
            if start < cursor:
                raise ValueError('source EH text payload overlaps at %08x' % start)
            expected = _align(cursor, next(s['alignment'] for s in self.text_sections if s['anchor'] == start))
            if start != expected:
                raise ValueError('source EH text alignment changed at %08x' % start)
            if start > cursor:
                padding = prep.orig.img.read(cursor, start - cursor)
                if padding != b'\xCC' * (start - cursor):
                    raise ValueError('source EH text gap %08x-%08x is not INT3 padding' % (cursor, start))
                tail_gaps.append((cursor, start))
            cursor = end
        if cursor != hi:
            raise ValueError('source EH tail is not completely tiled through %08x' % hi)

        dropped = {(name, secno) for name, secno, _, _ in sections}
        if not sections:
            raise ValueError('no target sections cover the selected source EH tail')
        target_intervals = sorted((va, end) for _, _, va, end in sections)
        for i, (seglo, seghi) in enumerate(target_intervals):
            if i and seglo < target_intervals[i - 1][1]:
                raise ValueError('target sections overlap in the reserved source EH tail')
        for ent in self.text_sections:
            seglo, seghi = ent['anchor'], ent['anchor'] + ent['size']
            covered = seglo
            for tlo, thi in target_intervals:
                if thi <= covered or tlo >= seghi:
                    continue
                if tlo > covered:
                    raise ValueError('target tail has an uncovered source helper range at %08x' % covered)
                covered = max(covered, min(thi, seghi))
                if covered == seghi:
                    break
            if covered != seghi:
                raise ValueError('target tail does not cover native helper through %08x' % seghi)

        # Preserve canonical aliases for all existing and newly recovered
        # interior target addresses before deleting their target definitions.
        reverse_text = {name: va for va, name in prep.text_name.items()}
        for name in list(prep.objs):
            coff = prep.objs[name]
            vas = prep.objvas.get(name, [])
            for secno, sec in enumerate(coff.sections, 1):
                for off, symidx, _typ in sec.relocs:
                    sym = coff.syms[symidx]
                    target_va = None
                    if sym is not None and 0 < sym.sec <= len(vas):
                        target_va = vas[sym.sec - 1] + sym.value
                    elif sym is not None and sym.sec == 0:
                        target_va = reverse_text.get(sym.name)
                    if target_va is not None and lo <= target_va < hi and target_va not in prep.text_name:
                        prep.add_code_symbol(target_va)
                        reverse_text[prep.text_name[target_va]] = target_va

        # Relocations from retained target sections to a deleted tail section
        # become references to the canonical alias that the native object adds.
        for name in list(prep.objs):
            coff = prep.objs[name]
            vas = prep.objvas.get(name, [])
            remove_here = {sec for obj, sec in dropped if obj == name}
            if not remove_here:
                continue
            # Symbol-table indices are object-local; never reuse an index
            # allocated in an earlier target object.
            ext_symbols = {}
            for secno, sec in enumerate(coff.sections, 1):
                if secno in remove_here:
                    continue
                for rel in sec.relocs:
                    _off, symidx, typ = rel
                    sym = coff.syms[symidx]
                    if sym is None or sym.sec not in remove_here:
                        continue
                    target_va = vas[sym.sec - 1] + sym.value
                    if not (prep.orig.text_lo <= target_va < prep.orig.text_hi):
                        raise ValueError('%s section %d relocation into removed tail resolves outside text: %08x' %
                                         (name, secno, target_va))
                    canonical = prep.text_name.get(target_va)
                    if canonical is None:
                        prep.add_code_symbol(target_va)
                        canonical = prep.text_name.get(target_va)
                    if canonical is None:
                        raise ValueError('%s section %d relocation targets unnamed removed code %08x' %
                                         (name, secno, target_va))
                    key = (canonical, 0x20 if typ == 0x14 else 0)
                    if key not in ext_symbols:
                        ext_symbols[key] = len(coff.syms)
                        coff.syms.append(F.Sym(canonical, 0, 0, key[1], F.CLS_EXTERNAL))
                    rel[1] = ext_symbols[key]

        by_obj = {}
        for name, secno, _, _ in sections:
            by_obj.setdefault(name, set()).add(secno)
        for name, remove in by_obj.items():
            old_obj = prep.objs[name]
            old_vas = prep.objvas[name]
            keep = [i + 1 for i in range(len(old_obj.sections)) if i + 1 not in remove]
            if keep:
                new_obj = subset_sections(old_obj, keep)
                new_vas = [old_vas[i - 1] for i in keep]
                remap = {old: new for new, old in enumerate(keep, 1)}
                prep.objs[name], prep.objvas[name] = new_obj, new_vas
                prep.leader = {(obj, remap[sec]) if obj == name and sec in remap else (obj, sec): leader
                               for (obj, sec), leader in prep.leader.items()
                               if obj != name or sec in remap}
            else:
                del prep.objs[name]
                prep.objvas.pop(name, None)
                prep.objsym.pop(name, None)
                prep.leader = {key: leader for key, leader in prep.leader.items() if key[0] != name}
                prep.unit_of.pop(name, None)
                prep.orig_vas.pop(name, None)
        prep.order = sorted((min(vas), name) for name, vas in prep.objvas.items()
                            if vas and name in prep.objs)
        prep.source_eh_text_gaps = tail_gaps

    def verify_base_coverage(self, prep):
        missing = sorted(set(self.units) - set(prep.base))
        if missing:
            raise ValueError('source EH units fell back to target coverage after tail reservation: %s' %
                             ', '.join(missing))

    def subtract_xdata_spans(self, pieces):
        """Remove native metadata extents from generated .rdata stand-in pieces."""
        cuts = sorted((s['anchor'], s['anchor'] + s['size']) for s in self.xdata_sections)
        out = []
        for key, group, lo, hi, owner in pieces:
            if group != '.rdata':
                out.append((key, group, lo, hi, owner))
                continue
            cursor = lo
            for cut_lo, cut_hi in cuts:
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

    def _target_section_index(self, name, oldno):
        up = self.units[name]
        return up['output_section_numbers'].get(oldno)

    def _require_linked_names(self, name_to_vas, names):
        resolved = {}
        for name in names:
            vas = name_to_vas.get(name, set())
            if len(vas) != 1:
                raise ValueError('source EH linked map name %r resolves to %d addresses' % (name, len(vas)))
            resolved[name] = next(iter(vas))
        return resolved

    @staticmethod
    def _parse_map(path):
        names = {}
        with open(path, 'r', encoding='latin1') as f:
            for line in f:
                m = re.match(r'^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{8})(?:\s|$)', line)
                if m:
                    names.setdefault(m.group(1), set()).add(int(m.group(2), 16))
        return names

    def verify_linked_output(self, image_path, map_path, prep):
        """Check native anchors, exact bytes, padding, and relocated fields after LINK."""
        import mktarget

        image = mktarget._image(image_path)
        map_names = self._parse_map(map_path)
        section_aliases = {s['start_alias']: s['anchor'] for s in self.text_sections + self.xdata_sections}
        linked_starts = self._require_linked_names(map_names, section_aliases)
        for name, expected in section_aliases.items():
            if linked_starts[name] != expected:
                raise ValueError('source EH section alias %s is at %08x, expected %08x' %
                                 (name, linked_starts[name], expected))

        for ent in self.text_sections + self.xdata_sections:
            got = image.read(ent['anchor'], ent['size'])
            expected = bytearray(ent['section'].data)
            masked = set()
            for off, _, typ in ent['section'].relocs:
                width = 4
                if off < 0 or off + width > len(expected):
                    raise ValueError('%s section %d has relocation outside its payload' %
                                     (ent['unit'], ent['secno']))
                masked.update(range(off, off + width))
            bad = next((i for i, b in enumerate(expected) if i not in masked and got[i] != b), None)
            if bad is not None:
                raise ValueError('%s section %d nonrelocation bytes differ at +%x' %
                                 (ent['unit'], ent['secno'], bad))
            up = self.units[ent['unit']]
            outno = self._target_section_index(ent['unit'], ent['secno'])
            if outno is None:
                raise ValueError('%s section %d is absent from its retained base object' %
                                 (ent['unit'], ent['secno']))
            linked_obj = prep.base[ent['unit']][0]
            sec = linked_obj.sections[outno - 1]
            if sec.name != ent['name'] or sec.data != ent['section'].data or sec.flags != ent['section'].flags:
                raise ValueError('%s section %d changed name, payload, or flags in the prepared base object' %
                                 (ent['unit'], ent['secno']))
            section_sym = _section_symbol(linked_obj, outno)
            if len(section_sym.aux) < 18 or section_sym.aux[14] != 5:
                raise ValueError('%s section %d lost associative COMDAT selection 5' %
                                 (ent['unit'], ent['secno']))
            expected_parent = up['output_section_numbers'].get(ent['parent'])
            actual_parent = struct.unpack_from('<H', section_sym.aux, 12)[0]
            if expected_parent is None or actual_parent != expected_parent:
                raise ValueError('%s section %d associative parent changed: expected %s, got %d' %
                                 (ent['unit'], ent['secno'], expected_parent, actual_parent))
            expected_relocs = {(off, typ) for off, _sym, typ in ent['section'].relocs}
            actual_relocs = {(off, typ) for off, _sym, typ in sec.relocs}
            if len(sec.relocs) != len(ent['relocations']) or actual_relocs != expected_relocs:
                raise ValueError('%s section %d relocation offsets/types changed: expected %d, got %d' %
                                 (ent['unit'], ent['secno'], len(expected_relocs), len(actual_relocs)))
            for off, si, typ in sec.relocs:
                info = ent['relocations'].get(off)
                if info is None or typ != info['type']:
                    raise ValueError('%s section %d relocation +%x changed after preparation' %
                                     (ent['unit'], ent['secno'], off))
                target = linked_obj.syms[si]
                if target is None:
                    raise ValueError('%s section %d relocation +%x points to an auxiliary symbol' %
                                     (ent['unit'], ent['secno'], off))
                if target.sec == -1:
                    if info['symbol_sec'] != -1 or target.value != info['symbol_value']:
                        raise ValueError('%s section %d absolute relocation +%x changed its COFF symbol value' %
                                         (ent['unit'], ent['secno'], off))
                    target_va = target.value
                else:
                    canonical = info['target_name']
                    if not canonical:
                        raise ValueError('%s section %d relocation +%x has no canonical target name' %
                                         (ent['unit'], ent['secno'], off))
                    target_va = self._require_linked_names(map_names, [canonical])[canonical]
                addend = info['addend']
                expected_value = ((target_va + addend) if typ == 6 else
                                  (target_va + addend - (ent['anchor'] + off + 4)) & 0xFFFFFFFF) & 0xFFFFFFFF
                actual_value = int.from_bytes(got[off:off + 4], 'little')
                if actual_value != expected_value:
                    raise ValueError('%s section %d relocation +%x differs: expected %08x, got %08x' %
                                     (ent['unit'], ent['secno'], off, expected_value, actual_value))

        for unit_name, up in self.units.items():
            linked_obj = prep.base[unit_name][0]
            eh_output_sections = {up['output_section_numbers'][secno]
                                  for secno in up['sections']}
            for root in up['root_relocations']:
                key = (root['symbol'], int(root['va']))
                function_name = up['root_link_names'].get(key)
                if not function_name:
                    raise ValueError('%s root %s at %08x has no linked function name' %
                                     (unit_name, root['symbol'], root['va']))
                function_va = self._require_linked_names(map_names, [function_name])[function_name]
                outno = up['output_section_numbers'].get(root['section'])
                if outno is None:
                    raise ValueError('%s root %s was dropped from its base object' % (unit_name, root['symbol']))
                source_offset = root['symbol_value'] + root['offset']
                matches = [(off, si, typ) for off, si, typ in linked_obj.sections[outno - 1].relocs
                           if off == source_offset]
                if len(matches) != 1 or matches[0][2] != root['type']:
                    raise ValueError('%s root %s relocation +%x changed or was dropped' %
                                     (unit_name, root['symbol'], root['offset']))
                target_va = self._require_linked_names(map_names, [root['target_name']])[root['target_name']]
                site_va = function_va + root['offset']
                actual = int.from_bytes(image.read(site_va, 4), 'little')
                expected = ((target_va + root['addend']) if root['type'] == 6 else
                            (target_va + root['addend'] - (site_va + 4))) & 0xFFFFFFFF
                if actual != expected:
                    raise ValueError('%s root %s relocation +%x differs: expected %08x, got %08x' %
                                     (unit_name, root['symbol'], root['offset'], expected, actual))

            # Root-to-helper edges must remain exactly the verifier-planned
            # relocation set. Other ordinary root relocations are untouched by
            # this integration and are intentionally outside this comparison.
            roots_by_key = {}
            for rel in up['root_relocations']:
                key = (rel['symbol'], int(rel['va']))
                roots_by_key.setdefault(key, []).append(rel)
            for key, expected_relocs in roots_by_key.items():
                first = expected_relocs[0]
                outno = up['output_section_numbers'].get(first['section'])
                if outno is None:
                    raise ValueError('%s root %s was dropped from its base object' % (unit_name, key[0]))
                expected_edges = []
                for rel in expected_relocs:
                    target_outno = up['output_section_numbers'].get(rel['target_sec'])
                    if target_outno is None:
                        raise ValueError('%s root %s targets a dropped EH section' % (unit_name, key[0]))
                    expected_edges.append((rel['symbol_value'] + rel['offset'], rel['type'], target_outno))
                actual_edges = []
                source_section = linked_obj.sections[outno - 1]
                for off, si, typ in source_section.relocs:
                    target = linked_obj.syms[si]
                    if first['symbol_value'] <= off < first['symbol_value'] + first['size'] \
                            and target is not None and target.sec in eh_output_sections:
                        actual_edges.append((off, typ, target.sec))
                if sorted(actual_edges) != sorted(expected_edges):
                    raise ValueError('%s root %s helper relocation set changed: expected %r, got %r' %
                                     (unit_name, key[0], sorted(expected_edges), sorted(actual_edges)))

        for lo, hi in self.text_gaps:
            if image.read(lo, hi - lo) != b'\xCC' * (hi - lo):
                raise ValueError('linked source EH text padding differs at %08x-%08x' % (lo, hi))

        required = set()
        for up in self.units.values():
            for ent in up['sections'].values():
                required.update(r['target_name'] for r in ent['relocations'].values() if r['target_name'])
            required.update(r['target_name'] for r in up['root_relocations'])
        self._require_linked_names(map_names, required)

