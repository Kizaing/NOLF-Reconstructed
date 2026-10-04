"""Read-only inventory of directly observed data-section placements.

Run ``python tools/data_inventory.py --module d3dren`` to print JSON, or pass
``--output PATH`` to write it. This tool does not compile or link objects and
does not infer placements through data-to-data chains.
"""
import argparse
import contextlib
import hashlib
import io
import json
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)

import modcfg  # noqa: E402  (consumes --module before the CLI is parsed)
from coffobj import (  # noqa: E402
    CoffObj,
    REL_DIR32,
    REL_REL32,
    REL_SIZES,
    undecorate,
)


SCN_CNT_CODE = 0x00000020
SCN_CNT_INITIALIZED_DATA = 0x00000040
SCN_CNT_UNINITIALIZED_DATA = 0x00000080
SCN_LNK_INFO = 0x00000200
SCN_LNK_REMOVE = 0x00000800

RELOC_NAMES = {
    0x0006: 'REL_DIR32',
    0x0007: 'REL_DIR32NB',
    0x000A: 'REL_SECTION',
    0x000B: 'REL_SECREL',
    0x0014: 'REL_REL32',
}


def hex_va(value):
    return None if value is None else '0x%08x' % (value & 0xffffffff)


def file_sha1(path):
    try:
        h = hashlib.sha1()
        with open(path, 'rb') as f:
            for block in iter(lambda: f.read(1024 * 1024), b''):
                h.update(block)
        return h.hexdigest()
    except OSError:
        return None


def relocation_name(reloc_type):
    return RELOC_NAMES.get(reloc_type, 'REL_0x%04x' % reloc_type)


def relocation_target(field_va, field_bytes, addend, reloc_type):
    """Solve a linked PE relocation field for the original COFF symbol VA."""
    if len(field_bytes) != 4:
        raise ValueError('relocation field must contain exactly four bytes')
    if reloc_type == REL_DIR32:
        linked = int.from_bytes(field_bytes, 'little', signed=False)
        return (linked - (addend & 0xffffffff)) & 0xffffffff
    if reloc_type == REL_REL32:
        linked_disp = int.from_bytes(field_bytes, 'little', signed=True)
        return (field_va + 4 + linked_disp - addend) & 0xffffffff
    raise ValueError('unsupported relocation type %s' % relocation_name(reloc_type))


def section_base_from_relocation(field_va, field_bytes, addend, reloc_type, symbol_value):
    target_va = relocation_target(field_va, field_bytes, addend, reloc_type)
    return (target_va - symbol_value) & 0xffffffff


def compare_non_relocation_bytes(section_data, target_data, relocs, relocation_sizes=None, valid_symbol_indices=None):
    """Compare bytes outside precisely sized COFF relocation fields.

    Relocations may be ``(offset, symbol_index, type)`` tuples from CoffObj or
    ``(offset, type)`` pairs used by focused tests. Unknown types and truncated
    fields make the comparison unavailable; they are never masked at a guessed
    four-byte width.
    """
    sizes = REL_SIZES if relocation_sizes is None else relocation_sizes
    n = len(section_data)
    if len(target_data) < n:
        return {
            'status': 'unavailable',
            'reason': 'mapped target range is truncated',
            'diagnostics': [{'reason': 'target_range_truncated', 'target_length': len(target_data), 'section_length': n}],
            'compared_bytes': 0,
            'masked_relocation_bytes': 0,
        }

    masked = bytearray(n)
    diagnostics = []
    relocation_count = 0
    for relocation in relocs:
        if len(relocation) == 2:
            off, typ = relocation
            symbol_index = None
        else:
            off, symbol_index, typ = relocation
        relocation_count += 1
        if valid_symbol_indices is not None and symbol_index not in valid_symbol_indices:
            diagnostics.append({
                'reason': 'relocation_symbol_index_out_of_table',
                'offset': off,
                'symbol_index': symbol_index,
                'type': typ,
                'type_name': relocation_name(typ),
            })
            continue
        width = sizes.get(typ)
        if width is None:
            diagnostics.append({
                'reason': 'unsupported_relocation_type',
                'offset': off,
                'type': typ,
                'type_name': relocation_name(typ),
            })
            continue
        if not isinstance(off, int) or off < 0 or off + width > n:
            diagnostics.append({
                'reason': 'relocation_field_out_of_section',
                'offset': off,
                'width': width,
                'section_length': n,
                'type': typ,
                'type_name': relocation_name(typ),
            })
            continue
        masked[off:off + width] = b'\x01' * width

    if diagnostics:
        return {
            'status': 'unavailable',
            'reason': 'one or more relocation fields are unsupported or malformed',
            'diagnostics': diagnostics,
            'compared_bytes': 0,
            'masked_relocation_bytes': sum(masked),
        }

    compared = 0
    for i in range(n):
        if masked[i]:
            continue
        compared += 1
        if section_data[i] != target_data[i]:
            return {
                'status': 'mismatch',
                'reason': 'non-relocation byte differs',
                'first_difference': i,
                'section_byte': section_data[i],
                'target_byte': target_data[i],
                'diagnostics': [],
                'compared_bytes': compared,
                'masked_relocation_bytes': sum(masked),
            }

    return {
        'status': 'exact_without_relocations' if relocation_count == 0 else 'equal_with_relocations_unchecked',
        'reason': None,
        'diagnostics': [],
        'compared_bytes': compared,
        'masked_relocation_bytes': sum(masked),
    }


def summarize_candidates(observations):
    """Group valid observations while counting code independence by source VA."""
    by_base = {}
    for observation in observations:
        if not observation.get('valid', True) or observation.get('inconclusive'):
            continue
        base = observation.get('candidate_base_value')
        if base is None:
            continue
        item = by_base.setdefault(base, {
            'candidate_base_value': base,
            'evidence_ids': [],
            'function_vas': set(),
            'global_observation_count': 0,
        })
        item['evidence_ids'].append(observation['id'])
        if observation.get('evidence_class') == 'code_function':
            source_va = observation.get('source_va_value')
            if source_va is not None:
                item['function_vas'].add(source_va)
        elif observation.get('evidence_class') == 'global':
            item['global_observation_count'] += 1

    candidates = []
    for base in sorted(by_base):
        item = by_base[base]
        candidates.append({
            'base': hex_va(base),
            'candidate_base_value': base,
            'evidence_ids': item['evidence_ids'],
            'distinct_function_va_count': len(item['function_vas']),
            'distinct_function_vas': [hex_va(va) for va in sorted(item['function_vas'])],
            'global_observation_count': item['global_observation_count'],
        })
    if not candidates:
        classification = 'unlocated'
    elif len(candidates) == 1:
        classification = 'observed'
    else:
        classification = 'conflicting'
    return classification, candidates


def is_data_section(section):
    """Whether a COFF section is a data candidate rather than code/metadata."""
    name = section.name
    if section.flags & SCN_CNT_CODE:
        return False
    if section.flags & (SCN_LNK_INFO | SCN_LNK_REMOVE):
        return False
    if name == '.drectve' or name.startswith('.debug') or name.startswith('.stab'):
        return False
    return True


def section_kind(section):
    if section.flags & SCN_CNT_UNINITIALIZED_DATA:
        return 'bss'
    if section.flags & SCN_CNT_INITIALIZED_DATA:
        return 'initialized_data'
    return 'other_data'


def mapped_pe_section(exe, base, size):
    """Return a PE section only when the entire range fits its VirtualSize."""
    if size < 0 or size == 0:
        return None, {'status': 'unavailable', 'reason': 'empty or negative section span'}
    matches = []
    end = base + size
    for pe_section in exe.pe.sections:
        section_base = exe.base + pe_section.VirtualAddress
        virtual_size = pe_section.Misc_VirtualSize
        section_end = section_base + virtual_size
        if base >= section_base and end <= section_end:
            matches.append((pe_section, section_base, section_end))
    if len(matches) != 1:
        return None, {
            'status': 'unavailable',
            'reason': 'candidate span is outside one full PE VirtualSize range' if not matches else 'candidate span overlaps multiple PE sections',
            'candidate_base': hex_va(base),
            'size': size,
        }
    pe_section, section_base, section_end = matches[0]
    raw_end = section_base + pe_section.SizeOfRawData
    raw_bytes = max(0, min(end, raw_end) - base)
    return pe_section, {
        'status': 'mapped',
        'pe_section': pe_section.Name.rstrip(b'\0').decode('latin1') if isinstance(pe_section.Name, bytes) else str(pe_section.Name),
        'virtual_range': [hex_va(section_base), hex_va(section_end)],
        'candidate_range': [hex_va(base), hex_va(end)],
        'raw_backed_bytes': raw_bytes,
        'virtual_only_bytes': size - raw_bytes,
    }


def highlow_relocations(exe):
    """Return the RVA set of PE HIGHLOW fields, or None if the directory is unreadable."""
    try:
        import pefile
        directory_index = pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_BASERELOC']
        directory = exe.pe.OPTIONAL_HEADER.DATA_DIRECTORY[directory_index]
        if not directory.VirtualAddress or not directory.Size:
            return set(), 'absent'
        if not hasattr(exe.pe, 'DIRECTORY_ENTRY_BASERELOC'):
            exe.pe.parse_data_directories(directories=[directory_index])
        entries = getattr(exe.pe, 'DIRECTORY_ENTRY_BASERELOC', None)
        if entries is None:
            return None, 'unavailable'
        highlow_type = pefile.RELOCATION_TYPE['IMAGE_REL_BASED_HIGHLOW']
        out = set()
        for block in entries:
            for entry in getattr(block, 'entries', []):
                if entry.type == highlow_type:
                    out.add(entry.rva)
        return out, 'available'
    except Exception as exc:  # malformed/unavailable PE relocation directory
        return None, 'unavailable: %s' % exc


def _highlow_at(highlow_rvas, image_base, field_va):
    if highlow_rvas is None:
        return None
    return field_va - image_base in highlow_rvas


def _read_mapped(exe, va, size):
    pe_section, mapping = mapped_pe_section(exe, va, size)
    if pe_section is None:
        return None, mapping
    try:
        data = exe.read(va, size)
    except Exception as exc:
        return None, {'status': 'unavailable', 'reason': 'PE read failed: %s' % exc}
    if len(data) != size:
        return None, {'status': 'unavailable', 'reason': 'PE read returned a truncated range', 'returned_bytes': len(data), 'required_bytes': size}
    return data, mapping


def _validate_candidate_spans(objects, exe, observations, diagnostics):
    """Keep raw observations, but accept a base only when the full section fits VirtualSize."""
    for observation in observations:
        unit_name = observation.get('definition_unit')
        section_index = observation.get('definition_section_index')
        obj = objects.get(unit_name)
        section = next((item for item in getattr(obj, 'sections', [])
                        if item.index == section_index), None)
        if section is None:
            target_data = None
            mapping = {'status': 'unavailable', 'reason': 'target data section is not present in the current object'}
        else:
            target_data, mapping = _read_mapped(
                exe, observation['candidate_base_value'], len(section.data))
        observation['candidate_span_valid'] = target_data is not None
        observation['candidate_span_mapping'] = mapping
        if target_data is not None:
            continue

        observation['valid'] = False
        diagnostics.append(_make_diagnostic(
            'candidate_span', 'implied base does not map the complete target section inside one PE VirtualSize',
            observation_id=observation['id'],
            candidate_base=observation.get('candidate_base'),
            candidate_base_value=observation.get('candidate_base_value'),
            definition=observation.get('definition'),
            evidence_class=observation.get('evidence_class'),
            source_unit=observation.get('source_unit'),
            source_va=observation.get('source_va'),
            function_name=observation.get('function_name'),
            function_symbol=observation.get('function_symbol'),
            field_va=observation.get('field_va'),
            coff_offset=observation.get('coff_offset'),
            relocation_type=observation.get('relocation_type'),
            addend=observation.get('addend'),
            pe_highlow=observation.get('pe_highlow'),
            annotation_source=observation.get('annotation_source'),
            annotation_where=observation.get('annotation_where'),
            annotation_name=observation.get('annotation_name'),
            mapping=mapping))


def _retain_snapshot_records(snapshots, records, header_digest, toolchain_digest):
    """Keep units that disappeared before source parsing visible as excluded records."""
    for name, record in snapshots.items():
        if name in records:
            continue
        record.update({
            'header_digest_before': header_digest,
            'compiler_digest_before': toolchain_digest,
            'stamp_current_before': False,
            'stamp_status': record.get('snapshot_status', 'excluded'),
            'stable_after_read': False,
        })
        records[name] = record
    return records


def _stamp_header_digest(build):
    # build.py caches this digest. Reset it at each fence so edits during the
    # inventory are visible to the final stamp recheck.
    if hasattr(build, '_HDR_DIGEST'):
        build._HDR_DIGEST[0] = None
    return build._header_digest()


def _snapshot_units(build):
    """Re-read source units once so parsed annotations and the source hash agree."""
    initial = build.find_units()
    units = []
    snapshots = {}
    for old in initial:
        before_hash = file_sha1(old.path)
        try:
            unit = build.Unit(old.path)
        except OSError as exc:
            snapshots[old.name] = {
                'unit': old.name,
                'source_path': old.rel,
                'source_sha1_before': before_hash,
                'source_sha1_after': None,
                'snapshot_status': 'missing_source',
                'object_path': os.path.relpath(old.base_obj, ROOT).replace('\\', '/'),
                'object_exists': os.path.isfile(old.base_obj),
                'object_stamp_before': build._read_stamp(old.base_obj),
                'diagnostic': str(exc),
            }
            continue
        after_hash = file_sha1(unit.path)
        snapshots[unit.name] = {
            'unit': unit.name,
            'source_path': unit.rel,
            'source_sha1_before': before_hash,
            'source_sha1_after': after_hash,
            'snapshot_status': 'stable' if before_hash is not None and before_hash == after_hash else 'changed_during_snapshot',
            'flags': list(unit.flags),
        }
        units.append(unit)
    return units, snapshots


def _preflight(build, units, snapshots, header_digest, toolchain_digest):
    records = {}
    current = []
    for unit in units:
        record = snapshots[unit.name]
        source_hash = file_sha1(unit.path)
        object_hash = file_sha1(unit.base_obj)
        object_stamp = build._read_stamp(unit.base_obj)
        expected_stamp = build._unit_stamp(unit)
        object_exists = os.path.isfile(unit.base_obj)
        snapshot_stable = record.get('snapshot_status') == 'stable'
        stamp_current = object_exists and object_stamp == expected_stamp and snapshot_stable and source_hash == record['source_sha1_after']
        record.update({
            'object_path': os.path.relpath(unit.base_obj, ROOT).replace('\\', '/'),
            'source_sha1_at_preflight': source_hash,
            'object_sha1_before': object_hash,
            'object_stamp_before': object_stamp,
            'expected_stamp_before': expected_stamp,
            'header_digest_before': header_digest,
            'compiler_digest_before': toolchain_digest,
            'object_exists': object_exists,
            'stamp_current_before': bool(stamp_current),
            'stamp_status': ('current' if stamp_current else
                             'missing_object' if not object_exists else
                             'changed_during_snapshot' if not snapshot_stable else 'stale'),
            'stable_after_read': False,
        })
        records[unit.name] = record
        if stamp_current:
            try:
                CoffObj(unit.base_obj)
            except Exception as exc:
                record['stamp_status'] = 'unreadable_object'
                record['diagnostic'] = str(exc)
                continue
            current.append(unit)
    return current, records


def _recheck_stamps(build, units, records):
    header_digest = _stamp_header_digest(build)
    toolchain_digest = build._toolchain_digest()
    for unit in units:
        record = records[unit.name]
        expected_after = build._unit_stamp(unit)
        object_stamp_after = build._read_stamp(unit.base_obj)
        source_sha1_after = file_sha1(unit.path)
        object_sha1_after = file_sha1(unit.base_obj)
        stable = (
            record.get('stamp_current_before')
            and expected_after == record.get('expected_stamp_before')
            and object_stamp_after == record.get('object_stamp_before')
            and object_sha1_after == record.get('object_sha1_before')
            and source_sha1_after == record.get('source_sha1_at_preflight')
        )
        record.update({
            'source_sha1_after_read': source_sha1_after,
            'object_sha1_after': object_sha1_after,
            'object_stamp_after': object_stamp_after,
            'expected_stamp_after': expected_after,
            'header_digest_after': header_digest,
            'compiler_digest_after': toolchain_digest,
            'stable_after_read': bool(stable),
        })
        if record.get('stamp_status') == 'current' and not stable:
            record['stamp_status'] = 'changed_during_read'
    return header_digest, toolchain_digest


def _is_data_definition(unit_name, obj, symbol):
    if symbol.secno <= 0 or symbol.secno > len(obj.sections):
        return None
    section = obj.section_of(symbol)
    if not is_data_section(section):
        return None
    return unit_name, obj, section, symbol


def _definition_map(objects):
    definitions = {}
    for unit_name, obj in objects.items():
        for symbol in obj.symbols.values():
            definition = _is_data_definition(unit_name, obj, symbol)
            if definition is not None:
                definitions.setdefault(symbol.name, []).append(definition)
    return definitions


def _definitions_for_relocation(source_unit, source_obj, symbol, definitions):
    if symbol.secno > 0:
        definition = _is_data_definition(source_unit, source_obj, symbol)
        return [definition] if definition is not None else []
    if symbol.secno == 0 and symbol.cls == 2:
        return [d for d in definitions.get(symbol.name, []) if d[3].cls == 2]
    return []


def _definition_record(unit_name, section, symbol):
    return {
        'unit': unit_name,
        'section_index': section.index,
        'section_name': section.name,
        'section_size': len(section.data),
        'symbol': symbol.name,
        'symbol_value': symbol.value,
        'symbol_value_hex': hex_va(symbol.value),
        'symbol_is_section_symbol': bool(symbol.is_section_symbol),
    }


def _make_diagnostic(kind, reason, **fields):
    out = {'kind': kind, 'valid': False, 'reason': reason}
    out.update(fields)
    return out


def _collect_global_observations(units, objects, definitions, observations, diagnostics):
    for unit in units:
        obj = objects.get(unit.name)
        if obj is None:
            continue
        for annotation in unit.annots:
            if annotation.kind != 'GLOBAL':
                continue
            bound_name = annotation.symbol if isinstance(annotation.symbol, str) else None
            common = {
                'evidence_class': 'global',
                'annotation_source': 'source',
                'annotation_unit': unit.name,
                'annotation_where': annotation.where(),
                'annotation_va': hex_va(annotation.va),
                'annotation_name': annotation.name,
                'symbol': bound_name,
            }
            if not bound_name:
                diagnostics.append(_make_diagnostic('global_binding', 'source GLOBAL did not bind to a COFF symbol', **common))
                continue
            found = definitions.get(bound_name, [])
            if getattr(annotation, 'static', False):
                found = [d for d in found if d[0] == unit.name]
            else:
                found = [d for d in found if d[3].cls == 2]
            if not found:
                diagnostics.append(_make_diagnostic('global_binding', 'bound source GLOBAL has no current defined data symbol', **common))
                continue
            for definition in found:
                definition_unit, _, section, symbol = definition
                if symbol.value < 0 or symbol.value >= len(section.data):
                    diagnostics.append(_make_diagnostic(
                        'global_binding', 'symbol value is outside its data section',
                        **dict(common, definition=_definition_record(definition_unit, section, symbol))))
                    continue
                observations.append({
                    'id': len(observations),
                    'valid': True,
                    'inconclusive': False,
                    **common,
                    'definition': _definition_record(definition_unit, section, symbol),
                    'definition_unit': definition_unit,
                    'definition_section_index': section.index,
                    'candidate_base_value': (annotation.va - symbol.value) & 0xffffffff,
                    'candidate_base': hex_va((annotation.va - symbol.value) & 0xffffffff),
                    'source_va_value': None,
                    'field_va': None,
                    'coff_offset': None,
                    'relocation_type': None,
                    'addend': None,
                    'pe_highlow': None,
                    'provenance_units': [unit.name, definition_unit],
                })

    # Header GLOBALs are explicit bindings too, but only when the current
    # source objects contain a matching defined data symbol.
    for va, mangled, name, where in __import__('build').header_globals():
        def matches_header_symbol(symbol):
            if symbol.cls != 2 or symbol.is_section_symbol:
                return False
            return symbol.name == mangled if mangled else undecorate(symbol.name) == name

        referenced = any(matches_header_symbol(symbol)
                         for obj in objects.values() for symbol in obj.symbols.values())
        if not referenced:
            continue
        if mangled:
            matches = [d for d in definitions.get(mangled, []) if d[3].cls == 2]
        else:
            matches = []
            for definition_list in definitions.values():
                for definition in definition_list:
                    if definition[3].cls == 2 and undecorate(definition[3].name) == name:
                        matches.append(definition)
        common = {
            'evidence_class': 'global',
            'annotation_source': 'header',
            'annotation_unit': None,
            'annotation_where': where,
            'annotation_va': hex_va(va),
            'annotation_name': name,
            'symbol': mangled or name,
        }
        if not matches:
            diagnostics.append(_make_diagnostic('global_binding', 'header GLOBAL has no current defined data symbol', **common))
            continue
        for definition_unit, _, section, symbol in matches:
            if symbol.value < 0 or symbol.value >= len(section.data):
                diagnostics.append(_make_diagnostic(
                    'global_binding', 'symbol value is outside its data section',
                    **dict(common, definition=_definition_record(definition_unit, section, symbol))))
                continue
            base = (va - symbol.value) & 0xffffffff
            observations.append({
                'id': len(observations),
                'valid': True,
                'inconclusive': False,
                **common,
                'definition': _definition_record(definition_unit, section, symbol),
                'definition_unit': definition_unit,
                'definition_section_index': section.index,
                'candidate_base_value': base,
                'candidate_base': hex_va(base),
                'source_va_value': None,
                'field_va': None,
                'coff_offset': None,
                'relocation_type': None,
                'addend': None,
                'pe_highlow': None,
                'provenance_units': [definition_unit],
            })


def _collect_code_observations(results, objects, definitions, exe, highlow_rvas, observations, diagnostics):
    build = __import__('build')
    for result in results:
        annotation = result.a
        if annotation.kind != 'FUNCTION' or result.status != 'MATCH' or not annotation.symbol:
            continue
        source_unit = annotation.unit.name
        source_obj = objects.get(source_unit)
        if source_obj is None:
            continue
        try:
            source_section, function_start, function_end = source_obj.extent(annotation.symbol)
        except Exception as exc:
            diagnostics.append(_make_diagnostic(
                'code_relocation', 'matched function extent could not be read',
                source_unit=source_unit, source_va=hex_va(annotation.va), function=annotation.symbol.name,
                detail=str(exc)))
            continue

        for object_offset, symbol_index, reloc_type in source_section.relocs:
            if not (function_start <= object_offset < function_end):
                continue
            reference = source_obj.symbols.get(symbol_index)
            if reference is None:
                function_offset = object_offset - function_start
                diagnostics.append(_make_diagnostic(
                    'code_relocation', 'relocation symbol index is not present in the COFF symbol table',
                    source_unit=source_unit, source_va=hex_va(annotation.va), source_va_value=annotation.va,
                    function_name=annotation.name, function_symbol=annotation.symbol.name,
                    field_va=hex_va(annotation.va + function_offset), function_offset=function_offset,
                    coff_offset=object_offset, symbol_index=symbol_index,
                    relocation_type=relocation_name(reloc_type), relocation_type_value=reloc_type,
                    addend=None, pe_highlow=None))
                continue
            target_definitions = _definitions_for_relocation(source_unit, source_obj, reference, definitions)
            if not target_definitions:
                continue

            function_offset = object_offset - function_start
            field_va = annotation.va + function_offset
            base_common = {
                'evidence_class': 'code_function',
                'source_unit': source_unit,
                'source_va': hex_va(annotation.va),
                'source_va_value': annotation.va,
                'function_name': annotation.name,
                'function_symbol': annotation.symbol.name,
                'field_va': hex_va(field_va),
                'field_va_value': field_va,
                'function_offset': function_offset,
                'coff_offset': object_offset,
                'symbol': reference.name,
                'relocation_type': relocation_name(reloc_type),
                'relocation_type_value': reloc_type,
            }
            if reloc_type not in (REL_DIR32, REL_REL32):
                diagnostics.append(_make_diagnostic(
                    'code_relocation', 'only DIR32 and REL32 can provide direct code-to-data observations',
                    **base_common, addend=None, pe_highlow=None,
                    target_definitions=[_definition_record(d[0], d[2], d[3]) for d in target_definitions]))
                continue
            width = REL_SIZES.get(reloc_type)
            if width != 4 or object_offset < 0 or object_offset + width > len(source_section.data) or object_offset + width > function_end:
                diagnostics.append(_make_diagnostic(
                    'code_relocation', 'relocation field is malformed or outside the matched function extent',
                    **base_common, addend=None, pe_highlow=None))
                continue

            addend = int.from_bytes(source_section.data[object_offset:object_offset + 4], 'little', signed=(reloc_type == REL_REL32))
            linked_field, field_mapping = _read_mapped(exe, field_va, width)
            if linked_field is None:
                diagnostics.append(_make_diagnostic(
                    'code_relocation', 'linked relocation field is not in a full PE VirtualSize range',
                    **base_common, addend=addend, pe_highlow=None, mapping=field_mapping))
                continue
            pe_highlow = _highlow_at(highlow_rvas, exe.base, field_va) if reloc_type == REL_DIR32 else None
            if reloc_type == REL_DIR32 and pe_highlow is not True:
                diagnostics.append(_make_diagnostic(
                    'code_relocation', 'DIR32 field is not confirmed as a PE HIGHLOW relocation',
                    **base_common, addend=addend, pe_highlow=pe_highlow,
                    linked_field_value=hex_va(int.from_bytes(linked_field, 'little'))))
                continue

            try:
                target_va = relocation_target(field_va, linked_field, addend, reloc_type)
            except ValueError as exc:
                diagnostics.append(_make_diagnostic(
                    'code_relocation', str(exc), **base_common, addend=addend, pe_highlow=pe_highlow))
                continue

            for definition_unit, _, target_section, target_symbol in target_definitions:
                if target_symbol.value < 0 or target_symbol.value >= len(target_section.data):
                    diagnostics.append(_make_diagnostic(
                        'code_relocation', 'target symbol value is outside its data section',
                        **base_common, addend=addend, pe_highlow=pe_highlow,
                        definition=_definition_record(definition_unit, target_section, target_symbol)))
                    continue
                base = (target_va - target_symbol.value) & 0xffffffff
                observations.append({
                    'id': len(observations),
                    'valid': True,
                    'inconclusive': False,
                    **base_common,
                    'addend': addend,
                    'pe_highlow': pe_highlow,
                    'linked_field_value': hex_va(int.from_bytes(linked_field, 'little', signed=False)),
                    'implied_target_va': hex_va(target_va),
                    'definition': _definition_record(definition_unit, target_section, target_symbol),
                    'definition_unit': definition_unit,
                    'definition_section_index': target_section.index,
                    'candidate_base_value': base,
                    'candidate_base': hex_va(base),
                    'provenance_units': [source_unit, definition_unit],
                })


def _sections_report(objects, unit_records, observations, diagnostics, exe):
    obs_by_section = {}
    diag_by_section = {}
    for observation in observations:
        key = (observation['definition_unit'], observation['definition_section_index'])
        obs_by_section.setdefault(key, []).append(observation)
    for i, diagnostic in enumerate(diagnostics):
        definition = diagnostic.get('definition')
        if definition:
            key = (definition['unit'], definition['section_index'])
            diag_by_section.setdefault(key, []).append(i)
        for definition in diagnostic.get('target_definitions', []):
            key = (definition['unit'], definition['section_index'])
            diag_by_section.setdefault(key, []).append(i)

    out = []
    for unit_name, obj in objects.items():
        unit_record = unit_records[unit_name]
        for section in obj.sections:
            if not is_data_section(section):
                continue
            key = (unit_name, section.index)
            evidence = obs_by_section.get(key, [])
            classification, candidates = summarize_candidates(evidence)
            candidate_reports = []
            for candidate in candidates:
                base = candidate['candidate_base_value']
                target_data, mapping = _read_mapped(exe, base, len(section.data))
                if target_data is None:
                    comparison = {
                        'status': 'unavailable',
                        'reason': mapping.get('reason'),
                        'diagnostics': [],
                        'compared_bytes': 0,
                        'masked_relocation_bytes': 0,
                    }
                else:
                    comparison = compare_non_relocation_bytes(
                        section.data, target_data, section.relocs,
                        REL_SIZES, set(objects[unit_name].symbols))
                candidate_reports.append({
                    **candidate,
                    'mapping': mapping,
                    'comparison': comparison,
                    'bss_zero_content_is_not_location_evidence': section_kind(section) == 'bss',
                })
            rejected_candidates = [
                {
                    'observation_id': item['id'],
                    'base': item.get('candidate_base'),
                    'candidate_base_value': item.get('candidate_base_value'),
                    'mapping': item.get('candidate_span_mapping'),
                }
                for item in evidence if item.get('candidate_span_valid') is False
            ]
            out.append({
                'unit': unit_name,
                'stamp_status': unit_record.get('stamp_status'),
                'section_index': section.index,
                'section_name': section.name,
                'size': len(section.data),
                'flags': '0x%08x' % section.flags,
                'kind': section_kind(section),
                'relocation_count': len(section.relocs),
                'symbol_names': sorted({s.name for s in section.syms}),
                'classification': classification,
                'candidate_bases': candidate_reports,
                'rejected_candidate_bases': rejected_candidates,
                'evidence_ids': [x['id'] for x in evidence],
                'diagnostic_ids': diag_by_section.get(key, []),
            })
    return out


def collect_report():
    """Collect a read-only direct-observation inventory for the active module."""
    import build

    header_before = _stamp_header_digest(build)
    compiler_before = build._toolchain_digest()
    units, snapshots = _snapshot_units(build)
    current_units, unit_records = _preflight(build, units, snapshots, header_before, compiler_before)
    _retain_snapshot_records(snapshots, unit_records, header_before, compiler_before)

    exe = build.Exe(build.EXE)
    symtab = build.SymTab()
    libraries = build.Libraries()
    scope = {unit.name for unit in current_units}
    with contextlib.redirect_stdout(io.StringIO()):
        results, _ = build.run_check(current_units, exe, symtab, libs=libraries, scope=scope)
    objects = {name: obj for name, obj in build.LAST_OBJS.items() if name in scope}

    observations = []
    diagnostics = []
    definitions = _definition_map(objects)
    highlow_rvas, highlow_status = highlow_relocations(exe)
    _collect_global_observations(current_units, objects, definitions, observations, diagnostics)
    _collect_code_observations(results, objects, definitions, exe, highlow_rvas, observations, diagnostics)
    _validate_candidate_spans(objects, exe, observations, diagnostics)

    header_after, compiler_after = _recheck_stamps(build, current_units, unit_records)
    stable_units = {
        name for name, record in unit_records.items()
        if record.get('stamp_status') == 'current' and record.get('stable_after_read')
    }
    for observation in observations:
        provenance_units = observation.get('provenance_units', [])
        observation['inconclusive'] = not all(name in stable_units for name in provenance_units)
        if observation['inconclusive']:
            observation['valid'] = False
            diagnostics.append(_make_diagnostic(
                'stamp_recheck', 'evidence depends on a source object whose stamp changed during collection',
                observation_id=observation['id'], provenance_units=provenance_units))

    sections = _sections_report(objects, unit_records, observations, diagnostics, exe)
    function_matches = [r for r in results if r.a.kind == 'FUNCTION' and r.status == 'MATCH']
    summary = {
        'source_units': len(unit_records),
        'current_source_units': len(current_units),
        'stable_source_units': len(stable_units),
        'excluded_source_units': len(unit_records) - len(current_units),
        'current_function_match_rows': len(function_matches),
        'data_sections': len(sections),
        'observations': len(observations),
        'inconclusive_observations': sum(1 for item in observations if item.get('inconclusive')),
        'diagnostics': len(diagnostics),
        'unlocated_sections': sum(1 for item in sections if item['classification'] == 'unlocated'),
        'observed_sections': sum(1 for item in sections if item['classification'] == 'observed'),
        'conflicting_sections': sum(1 for item in sections if item['classification'] == 'conflicting'),
    }
    return {
        'schema': 'd3dren-data-inventory-v1',
        'module': modcfg.NAME,
        'scope': 'direct observations only; no ownership, completeness, data-chain, or library-placement claims',
        'relocation_rules': {
            'source_rows': 'current-stamp FUNCTION rows with checker status MATCH only',
            'global_rows': 'explicit GLOBAL annotations from source and module headers, bound to current defined data symbols',
            'symbol_addresses': 'learned ALL_NAMES, SYMVA, namemap, and target-emission addresses are not independent evidence',
            'bss': 'zero bytes never establish a candidate section base',
            'mapped_extent': 'candidate ranges must fit one PE section VirtualSize in full',
            'comparison': 'only non-relocation bytes are compared; unsupported or malformed relocation fields make comparison unavailable',
            'code_dir32': 'a code DIR32 observation requires a confirmed PE HIGHLOW entry; REL32 does not',
        },
        'pe': {
            'image_path': build.EXE,
            'image_base': hex_va(exe.base),
            'base_relocations': highlow_status,
        },
        'stamp_fence': {
            'header_digest_before': header_before,
            'header_digest_after': header_after,
            'compiler_digest_before': compiler_before,
            'compiler_digest_after': compiler_after,
            'all_evidence_stable': all(record.get('stable_after_read') for record in unit_records.values() if record.get('stamp_current_before')),
        },
        'summary': summary,
        'units': [unit_records[name] for name in sorted(unit_records)],
        'sections': sections,
        'observations': observations,
        'diagnostics': diagnostics,
    }


def _compact_summary(report):
    summary = report['summary']
    return (
        'data inventory %s: %d/%d source objects current (%d stable), %d data sections '
        '[%d observed, %d conflicting, %d unlocated], %d observations, %d diagnostics'
    ) % (
        report['module'], summary['current_source_units'], summary['source_units'], summary['stable_source_units'],
        summary['data_sections'], summary['observed_sections'], summary['conflicting_sections'],
        summary['unlocated_sections'], summary['observations'], summary['diagnostics'])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', help='write JSON here; otherwise print JSON to stdout')
    args = parser.parse_args(argv)
    report = collect_report()
    payload = json.dumps(report, indent=2, sort_keys=True) + '\n'
    if args.output:
        destination = os.path.abspath(args.output)
        parent = os.path.dirname(destination)
        if parent and not os.path.isdir(parent):
            os.makedirs(parent)
        with open(destination, 'w', encoding='utf-8', newline='') as f:
            f.write(payload)
    else:
        sys.stdout.write(payload)
    print(_compact_summary(report), file=sys.stderr)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
