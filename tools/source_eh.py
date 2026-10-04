"""Original-guided verification of source-compiled VC6 EH helper sections.

This is deliberately root-guided: only .text$x/.xdata$x sections reached by a
caller-supplied MATCH function are considered.  It is not a byte-search linker
or a whole-image coverage claim.
"""
import argparse
import contextlib
import hashlib
import io
import json
import os
import sys

import coffedit as F

RELOC_DIR32 = 0x06
RELOC_REL32 = 0x14
RELOC_WIDTH = {RELOC_DIR32: 4, RELOC_REL32: 4}
EH_PREFIXES = ('.text$x', '.xdata$x')


class MalformedRelocation(ValueError):
    pass


class _UnknownUnitName(ValueError):
    pass


def _symbol(unit, ref):
    if isinstance(ref, int):
        s = unit.syms[ref]
        if s is None:
            raise ValueError('root symbol index points at an auxiliary slot')
        return s
    if isinstance(ref, str):
        hits = [s for s in unit.syms if s is not None and s.name == ref and s.sec > 0]
        if len(hits) != 1:
            raise ValueError('root symbol %r resolves to %d defined symbols' % (ref, len(hits)))
        return hits[0]
    return ref


def _secname(unit, secno):
    return unit.sections[secno - 1].name if 0 < secno <= len(unit.sections) else ''


def _is_eh_name(name):
    return any(name.startswith(p) for p in EH_PREFIXES)


def _range_counts(entries):
    """Count distinct section starts and unioned bytes, avoiding COMDAT double counts."""
    ranges = sorted({(va, va + size) for va, size in entries if va is not None and size > 0})
    total, end = 0, None
    for lo, hi in ranges:
        if end is None or lo > end:
            total += hi - lo
            end = hi
        elif hi > end:
            total += hi - end
            end = hi
    return len(ranges), total


def _relocations(unit, secno, lo, hi):
    """Yield (offset, symbol, type, addend) for relocations inside [lo, hi)."""
    sec = unit.sections[secno - 1]
    for off, symi, typ in sec.relocs:
        if lo <= off < hi:
            sym = unit.syms[symi]
            if sym is None:
                raise MalformedRelocation('relocation refers to an auxiliary symbol slot')
            width = RELOC_WIDTH.get(typ, 4)
            if off < 0 or off + width > hi or off + width > len(sec.data):
                raise MalformedRelocation('relocation field at +0x%x crosses checked extent 0x%x' % (off, hi))
            raw = sec.data[off:off + width]
            addend = int.from_bytes(raw, 'little', signed=(typ == RELOC_REL32))
            yield off, sym, typ, addend


def _image_target(image, site_va, typ, addend):
    width = RELOC_WIDTH.get(typ)
    if width is None:
        return None
    try:
        raw = image.read(site_va, width)
    except Exception:
        return None
    if len(raw) != width:
        return None
    field = int.from_bytes(raw, 'little', signed=(typ == RELOC_REL32))
    if typ == RELOC_DIR32:
        return field - addend
    return site_va + 4 + field - addend


def _root_extent(unit, roots):
    """Index matched function roots by symbol and section; validate supplied bodies."""
    by_name, by_sec = {}, {}
    for root in roots:
        sym = _symbol(unit, root['symbol'])
        va, size = int(root['va']), int(root['size'])
        if sym.sec <= 0 or not (0 <= sym.value <= len(unit.sections[sym.sec - 1].data)):
            raise ValueError('root %s is not defined in a raw section' % sym.name)
        if size < 0 or sym.value + size > len(unit.sections[sym.sec - 1].data):
            raise ValueError('root %s body exceeds its COFF section' % sym.name)
        entry = {'symbol': sym, 'va': va, 'size': size, 'base': va - sym.value}
        by_name.setdefault(sym.name, []).append(entry)
        by_sec.setdefault(sym.sec, []).append(entry)
    return by_name, by_sec


def verify_unit(unit, matched_function_roots, image, external_name_to_va):
    """Verify rooted EH helpers in a ``coffedit.Coff`` object.

    Each root is ``{'symbol': COFF symbol name or index, 'va': image VA,
    'size': matched body length}``.  The image must expose ``read(va, n)``.
    Return a JSON-serializable result.  Unknown relocation destinations remain
    UNVERIFIED and taint their local EH dependents rather than being guessed.
    """
    roots = list(matched_function_roots)
    by_name, by_root_sec = _root_extent(unit, roots)
    sec_sizes = {i + 1: len(s.data) for i, s in enumerate(unit.sections)}
    eh_secs = {i + 1 for i, s in enumerate(unit.sections) if _is_eh_name(s.name)}
    root_relocs = []
    anchors = {}       # secno -> {'va', 'evidence': [...]}
    errors = []
    statuses = {}
    section_relocs = {}
    for secno in eh_secs:
        try:
            section_relocs[secno] = list(_relocations(unit, secno, 0, sec_sizes[secno]))
        except MalformedRelocation as exc:
            section_relocs[secno] = []
            statuses[secno] = 'UNVERIFIED'
            errors.append({'section': secno, 'kind': 'malformed_relocation', 'detail': str(exc)})

    def add_anchor(secno, va, provenance):
        if secno not in eh_secs:
            return
        ent = anchors.get(secno)
        if ent is None:
            anchors[secno] = {'va': va, 'evidence': [provenance]}
        elif ent['va'] != va:
            statuses[secno] = 'CONFLICT'
            errors.append({'section': secno, 'kind': 'anchor_conflict', 'existing': ent['va'],
                           'candidate': va, 'evidence': provenance})
            if provenance not in ent['evidence']:
                ent['evidence'].append(provenance)
        else:
            if provenance not in ent['evidence']:
                ent['evidence'].append(provenance)

    # Only matched caller bodies are roots; no global byte search is performed.
    root_keys = {}
    malformed_roots = set()
    for root in roots:
        sym = _symbol(unit, root['symbol'])
        key = '%s@%08x' % (sym.name, int(root['va']))
        root_keys[id(root)] = key
        try:
            relocs = list(_relocations(unit, sym.sec, sym.value,
                                       sym.value + int(root['size'])))
        except MalformedRelocation as exc:
            malformed_roots.add(key)
            errors.append({'root': key, 'kind': 'malformed_relocation', 'detail': str(exc)})
            continue
        for off, target, typ, addend in relocs:
            if target.sec not in eh_secs:
                continue
            item = {'root': key, 'va': int(root['va']), 'offset': off - sym.value,
                    'target': target.name, 'target_sec': target.sec, 'type': typ, 'addend': addend}
            root_relocs.append(item)
            target_va = _image_target(image, int(root['va']) + off - sym.value, typ, addend)
            if target_va is None:
                errors.append({'root': key, 'kind': 'unreadable_or_unsupported_root_reloc',
                               'offset': item['offset'], 'target': target.name})
                continue
            add_anchor(target.sec, target_va - target.value,
                       {'from': 'root', 'symbol': key, 'offset': item['offset'],
                        'target': target.name})

    def payload_status(secno, va):
        sec = unit.sections[secno - 1]
        n = len(sec.data)
        try:
            original = image.read(va, n)
        except Exception as exc:
            return 'MISMATCH', {'section': secno, 'kind': 'image_read', 'detail': str(exc)}
        if len(original) != n:
            return 'MISMATCH', {'section': secno, 'kind': 'short_image_read', 'expected': n,
                                'actual': len(original)}
        mask = bytearray(n)
        for off, _, typ, _ in section_relocs[secno]:
            width = RELOC_WIDTH.get(typ)
            if width:
                mask[off:min(n, off + width)] = b'\1' * min(width, max(0, n - off))
        diffs = [i for i in range(n) if not mask[i] and sec.data[i] != original[i]]
        if diffs:
            return 'MISMATCH', {'section': secno, 'kind': 'nonrelocation_bytes', 'count': len(diffs),
                                'first_offset': diffs[0]}
        return 'MATCH', None

    # Anchors discovered from roots are checked before they can propagate.
    dirty = True
    while dirty:
        dirty = False
        for secno in sorted(eh_secs):
            anchor = anchors.get(secno)
            if not anchor or statuses.get(secno) == 'CONFLICT':
                continue
            current = statuses.get(secno)
            if current is None:
                state, error = payload_status(secno, anchor['va'])
                statuses[secno] = state
                if error:
                    errors.append(error)
                dirty = True
                current = state
            if current != 'MATCH':
                continue       # never propagate from an unproven payload
            for off, target, typ, addend in section_relocs[secno]:
                if target.sec not in eh_secs:
                    continue
                target_va = _image_target(image, anchor['va'] + off, typ, addend)
                if target_va is None:
                    errors.append({'section': secno, 'kind': 'unreadable_or_unsupported_eh_reloc',
                                   'offset': off, 'target': target.name})
                    continue
                before = anchors.get(target.sec, {}).get('va')
                add_anchor(target.sec, target_va - target.value,
                           {'from': 'section', 'section': secno, 'offset': off, 'target': target.name})
                if before is None and target.sec in anchors:
                    dirty = True

    # Check relocations on every anchored helper and caller. Unknowns are never
    # counted as verified; a bad or unknown EH edge taints its originating root.
    eh_root_keys = {item['root'] for item in root_relocs}
    root_states = {key: 'MATCH' for key in eh_root_keys}
    root_errors = {key: [] for key in eh_root_keys}
    section_edges = {secno: [] for secno in eh_secs}
    verified_relocs = 0
    unverified_relocs = []

    def expected_target(sym):
        if sym.sec in anchors and statuses.get(sym.sec) == 'MATCH':
            return anchors[sym.sec]['va'] + sym.value, 'eh_section'
        if sym.sec in by_root_sec:
            candidates = [r for r in by_root_sec[sym.sec] if r['symbol'].name == sym.name]
            if len(candidates) == 1:
                return candidates[0]['va'], 'matched_root'
            interior = [r for r in by_root_sec[sym.sec]
                        if r['symbol'].value <= sym.value < r['symbol'].value + r['size']]
            if len(interior) == 1:
                return interior[0]['va'] + sym.value - interior[0]['symbol'].value, 'matched_root_label'
            # A section-symbol relocation to the parent's own function section
            # can be resolved from the matched function's exact base.
            if sym.cls == F.CLS_STATIC and sym.value == 0:
                owners = by_root_sec[sym.sec]
                if len(owners) == 1:
                    return owners[0]['base'] + sym.value, 'matched_root_section'
        if sym.sec == 0:
            if sym.name in external_name_to_va:
                return int(external_name_to_va[sym.name]), 'named_external'
            return None, 'unknown_external'
        if sym.sec == -1:
            return int(sym.value), 'absolute'
        if sym.name in external_name_to_va:
            return int(external_name_to_va[sym.name]), 'named_symbol'
        return None, 'unmapped_defined_symbol'

    def verify_reloc(section_no, site_va, off, sym, typ, addend, root_key=None):
        nonlocal verified_relocs
        actual = _image_target(image, site_va, typ, addend)
        want, why = expected_target(sym)
        if actual is None or want is None:
            unverified_relocs.append({'section': section_no, 'offset': off, 'symbol': sym.name,
                                      'symbol_index': unit.syms.index(sym),
                                      'reason': why, 'actual_va': actual,
                                      'payload_passed': section_no in eh_secs and
                                                        statuses.get(section_no) == 'MATCH' and
                                                        anchors.get(section_no) is not None})
            if root_key:
                root_errors[root_key].append({'kind': 'unverified_relocation', 'symbol': sym.name,
                                              'reason': why, 'offset': off})
            elif section_no in section_edges:
                section_edges[section_no].append(('UNVERIFIED', sym.sec if sym.sec in eh_secs else None))
            return
        if actual != want:
            err = {'section': section_no, 'offset': off, 'symbol': sym.name, 'kind': 'relocation_target',
                   'expected': want, 'actual': actual}
            errors.append(err)
            if root_key:
                root_errors[root_key].append(err)
            elif section_no in section_edges:
                section_edges[section_no].append(('MISMATCH', sym.sec if sym.sec in eh_secs else None))
            return
        verified_relocs += 1
        if section_no in section_edges and sym.sec in eh_secs:
            section_edges[section_no].append(('MATCH', sym.sec))

    for root in roots:
        sym = _symbol(unit, root['symbol'])
        key = root_keys[id(root)]
        if key not in eh_root_keys or key in malformed_roots:
            continue
        for off, target, typ, addend in _relocations(unit, sym.sec, sym.value,
                                                       sym.value + int(root['size'])):
            if target.sec in eh_secs:
                verify_reloc(None, int(root['va']) + off - sym.value, off - sym.value,
                             target, typ, addend, key)
    for secno, anchor in anchors.items():
        if statuses.get(secno) != 'MATCH':
            continue
        for off, target, typ, addend in section_relocs[secno]:
            verify_reloc(secno, anchor['va'] + off, off, target, typ, addend)

    # Local dependency status is transitive: a section whose payload is sound
    # still cannot be trusted if one of its referenced EH sections is bad or
    # unknown. Iterate to a fixed point for cycles.
    effective = dict(statuses)
    changed = True
    while changed:
        changed = False
        for secno, edges in section_edges.items():
            state = effective.get(secno)
            if state not in ('MATCH', 'UNVERIFIED'):
                continue
            depstates = [effective.get(dep, 'UNVERIFIED') for _, dep in edges if dep is not None]
            edge_states = [s for s, _ in edges]
            new = 'MISMATCH' if 'MISMATCH' in edge_states or 'MISMATCH' in depstates or 'CONFLICT' in depstates else \
                  'UNVERIFIED' if 'UNVERIFIED' in edge_states or 'UNVERIFIED' in depstates else state
            if new != state:
                effective[secno] = new
                changed = True
    for root_key, problems in root_errors.items():
        if root_key in malformed_roots:
            root_states[root_key] = 'UNVERIFIED'
        for problem in problems:
            if problem['kind'] == 'relocation_target':
                root_states[root_key] = 'MISMATCH'
            elif root_states[root_key] != 'MISMATCH':
                root_states[root_key] = 'UNVERIFIED'
        # Direct references to helper sections propagate their final state.
        for item in root_relocs:
            if item['root'] != root_key or item['target_sec'] not in eh_secs:
                continue
            dep = effective.get(item['target_sec'], 'UNVERIFIED')
            if dep in ('MISMATCH', 'CONFLICT'):
                root_states[root_key] = 'MISMATCH'
            elif dep == 'UNVERIFIED' and root_states[root_key] != 'MISMATCH':
                root_states[root_key] = 'UNVERIFIED'

    # Count only helpers reachable through final MATCH edges from a supplied
    # MATCH function. A valid child behind a bad helper is not verified coverage.
    reachable = set()
    pending = [item['target_sec'] for item in root_relocs if item['target_sec'] in eh_secs
               and effective.get(item['target_sec']) == 'MATCH']
    while pending:
        secno = pending.pop()
        if secno in reachable or effective.get(secno) != 'MATCH':
            continue
        reachable.add(secno)
        for _, target, _, _ in section_relocs.get(secno, []):
            if target.sec in eh_secs and effective.get(target.sec) == 'MATCH' and target.sec not in reachable:
                pending.append(target.sec)

    section_results = {}
    for secno in sorted(eh_secs):
        sec = unit.sections[secno - 1]
        ent = {'name': sec.name, 'size': len(sec.data), 'status': effective.get(secno, 'UNPLACED'),
               'anchor': anchors.get(secno, {}).get('va'), 'reachable_from_match_root': secno in reachable,
               'provenance': anchors.get(secno, {}).get('evidence', [])}
        section_results[str(secno)] = ent
    roots_out = [{'symbol': _symbol(unit, r['symbol']).name, 'va': int(r['va']), 'size': int(r['size']),
                  'status': root_states[root_keys[id(r)]],
                  'errors': root_errors[root_keys[id(r)]]}
                 for r in roots if root_keys[id(r)] in eh_root_keys]
    matched = [(unit.sections[int(k) - 1].name, v) for k, v in section_results.items()
               if v['status'] == 'MATCH' and v['reachable_from_match_root']]
    text_matched = [(n, v) for n, v in matched if n.startswith('.text$x')]
    xdata_matched = [(n, v) for n, v in matched if n.startswith('.xdata$x')]
    text_count, text_bytes = _range_counts((v['anchor'], v['size']) for _, v in text_matched)
    xdata_count, xdata_bytes = _range_counts((v['anchor'], v['size']) for _, v in xdata_matched)
    _, all_matched_bytes = _range_counts((v['anchor'], v['size']) for _, v in matched)
    counts = {'anchored_sections': sum(x['anchor'] is not None for x in section_results.values()),
              'matched_sections': text_count + xdata_count,
              'matched_payload_bytes': all_matched_bytes,
              'text_helper_source_instances': len(text_matched),
              'text_helper_sections': text_count,
              'text_helper_bytes': text_bytes,
              'xdata_source_instances': len(xdata_matched),
              'xdata_sections': xdata_count,
              'xdata_bytes': xdata_bytes,
              'verified_relocations': verified_relocs, 'unverified_relocations': len(unverified_relocs)}
    reachable_states = [section_results[str(secno)]['status'] for secno in reachable]
    root_statuses = [r['status'] for r in roots_out]
    if any(s in ('MISMATCH', 'CONFLICT') for s in reachable_states + root_statuses):
        overall = 'MISMATCH'
    elif any(s == 'UNVERIFIED' for s in reachable_states + root_statuses):
        overall = 'UNVERIFIED'
    elif reachable:
        overall = 'MATCH'
    else:
        overall = 'NO_REACHED_EH'
    return {'unit': getattr(unit, 'name', ''), 'scope': 'root-guided original placement; not independent whole-image coverage',
            'status': overall, 'roots': roots_out, 'sections': section_results, 'counts': counts,
            'unverified': unverified_relocs, 'errors': errors}


def resolve_function_aliases(unit, verification, target_obj, matched_by_va, validate):
    """Prove same-address aliases from rooted EH relocations only.

    ``matched_by_va`` maps an original VA to one or more already-MATCH function
    records. ``validate(symbol, va, canonical_records)`` must perform the strict
    ordinary function check and return an object/dict with status and
    unverified relocations. The helper never invents an address or searches
    bytes; each candidate VA comes from a relocated field in a rooted,
    payload-matched EH section.
    """
    groups = {}
    evidence = []
    conflicts = []
    for rec in verification.get('unverified', []):
        if not (isinstance(rec.get('section'), int) and rec.get('payload_passed') and
                isinstance(rec.get('actual_va'), int) and isinstance(rec.get('symbol_index'), int)):
            continue
        idx = rec['symbol_index']
        if idx < 0 or idx >= len(unit.syms):
            continue
        sym = unit.syms[idx]
        if sym is None or sym.sec <= 0 or sym.typ != 0x20:
            continue
        if not (unit.sections[sym.sec - 1].flags & F.SCN_CNT_CODE):
            continue
        if _is_eh_name(_secname(unit, sym.sec)):
            continue
        va = rec['actual_va']
        groups.setdefault(idx, {'symbol': sym, 'candidates': {}})['candidates'].setdefault(va, []).append(rec)

    proposed = {}
    for idx, group in groups.items():
        sym = group['symbol']
        candidates = group['candidates']
        if len(candidates) != 1:
            entry = {'symbol': sym.name, 'symbol_index': idx,
                     'candidate_vas': sorted(candidates), 'status': 'CONFLICT'}
            conflicts.append(entry)
            evidence.append(entry)
            continue
        va, records = next(iter(candidates.items()))
        canonical = matched_by_va.get(va)
        if not canonical:
            evidence.append({'symbol': sym.name, 'symbol_index': idx, 'target_va': va,
                             'status': 'REJECTED', 'reason': 'target VA has no MATCH function root'})
            continue
        symbol_hits = [s for s in target_obj.symbols.values()
                       if not s.is_section_symbol and s.name == sym.name and s.is_function]
        if len(symbol_hits) != 1:
            evidence.append({'symbol': sym.name, 'symbol_index': idx, 'target_va': va,
                             'status': 'REJECTED', 'reason': 'target object has %d function symbols' % len(symbol_hits)})
            continue
        validation = validate(symbol_hits[0], va, canonical)
        status = validation.get('status') if isinstance(validation, dict) else getattr(validation, 'status', None)
        unknown = validation.get('unverified', []) if isinstance(validation, dict) else getattr(validation, 'unverified', [])
        accepted = status == 'MATCH' and not unknown
        entry = {'symbol': sym.name, 'symbol_index': idx, 'target_va': va,
                 'status': 'MATCH' if accepted else 'REJECTED',
                 'validation_status': status, 'unverified_targets': len(unknown),
                 'evidence': [{'section': r['section'], 'offset': r['offset'],
                               'root_target_va': va} for r in records],
                 'canonical_roots': canonical}
        evidence.append(entry)
        if accepted:
            proposed.setdefault(sym.name, set()).add(va)

    aliases = {}
    for name, vas in proposed.items():
        if len(vas) == 1:
            aliases[name] = next(iter(vas))
        else:
            entry = {'symbol': name, 'candidate_vas': sorted(vas), 'status': 'CONFLICT'}
            conflicts.append(entry)
            evidence.append(entry)
    return aliases, evidence, conflicts


def _stat_signature(path):
    st = os.stat(path)
    return (st.st_dev, st.st_ino, st.st_size,
            getattr(st, 'st_mtime_ns', int(st.st_mtime * 1000000000)),
            getattr(st, 'st_ctime_ns', int(st.st_ctime * 1000000000)))


def _read_base_fingerprint(path):
    """Hash a base object and ensure the file stayed stable during the read."""
    absolute_path = os.path.abspath(path)
    before = _stat_signature(absolute_path)
    with open(absolute_path, 'rb') as f:
        object_bytes = f.read()
    after = _stat_signature(absolute_path)
    if before != after or len(object_bytes) != after[2]:
        raise RuntimeError('base object changed while verifying: %s' % absolute_path)
    return {'base_obj': absolute_path,
            'sha256': hashlib.sha256(object_bytes).hexdigest()}, before


def _load_base_object(path, unit_name, Coff, expected_signature):
    """Load one base object while checking it still matches its pre-check snapshot."""
    absolute_path = os.path.abspath(path)
    before = _stat_signature(absolute_path)
    if before != expected_signature:
        raise RuntimeError('base object changed before loading for verification: %s' % absolute_path)
    c = Coff.load(absolute_path)
    after = _stat_signature(absolute_path)
    if before != after or after != expected_signature:
        raise RuntimeError('base object changed while loading for verification: %s' % absolute_path)
    c.name = unit_name
    return c


def verify_sources(unit_names=None):
    """Freshly verify rooted source EH helpers and return the JSON report payload.

    ``unit_names`` optionally limits report units by their names under ``src/``.
    Unknown names raise ``ValueError``. Strict function matching and the local
    alias proof use the same build checker and inputs as the command-line tool.
    """
    import build as B
    from coffedit import Coff

    inventory = B.find_units()
    selected_names = ({u.name for u in inventory} if unit_names is None else
                      {unit_names} if isinstance(unit_names, str) else set(unit_names))
    units = [u for u in inventory if u.name in selected_names]
    missing = selected_names - {u.name for u in units}
    if missing:
        raise _UnknownUnitName('unknown unit(s): %s' % ', '.join(sorted(missing)))
    exe = B.Exe(B.EXE)
    symtab = B.SymTab()
    source_objects = {}
    for u in units:
        source_objects[u.name] = None
        if os.path.exists(u.base_obj):
            fingerprint, signature = _read_base_fingerprint(u.base_obj)
            source_objects[u.name] = {'fingerprint': fingerprint, 'signature': signature}
    with contextlib.redirect_stdout(io.StringIO()):
        found, _ = B.run_check(inventory, exe, symtab, libs=B.Libraries())
    matched = {}
    matched_by_va = {}
    for r in found:
        if r.a.unit.name in selected_names and r.status == 'MATCH' and r.a.kind == 'FUNCTION' and r.a.symbol:
            sec, start, end = B.LAST_OBJS[r.a.unit.name].extent(r.a.symbol)
            matched.setdefault(r.a.unit.name, []).append({'symbol': r.a.symbol.index, 'va': r.a.va, 'size': end - start})
        if r.status == 'MATCH' and r.a.kind == 'FUNCTION' and r.a.symbol:
            matched_by_va.setdefault(r.a.va, []).append({'unit': r.a.unit.name,
                                                         'function': r.a.name or r.a.symbol.name,
                                                         'symbol': r.a.symbol.name})
    external = {}
    for name, va in B.ALL_NAMES.items():
        external[name] = va
    _, _, local_names, _ = B.build_namemap(inventory, symtab)
    results = []
    for u in units:
        if not matched.get(u.name) or not os.path.exists(u.base_obj):
            continue
        source_object = source_objects[u.name]
        if source_object is None:
            raise RuntimeError('base object appeared during verification: %s' % os.path.abspath(u.base_obj))
        c = _load_base_object(u.base_obj, u.name, Coff, source_object['signature'])
        if not any(_is_eh_name(sec.name) for sec in c.sections):
            continue
        base_verification = verify_unit(c, matched[u.name], exe, external)
        target_obj = B.LAST_OBJS.get(u.name)

        def validate_alias(symbol, va, canonical):
            # The bytes and relocations must pass the same strict checker used
            # for annotated functions, and every call target must already be
            # named by MATCH functions or the existing global name map.
            anno = B.Annot('FUNCTION', va, symbol.name, u, 0,
                           canonical[0]['function'] if canonical else None)
            anno.symbol = symbol
            return B.check_function(anno, target_obj, exe, symtab, external, [],
                                    local_names.get(u.name))

        aliases, alias_evidence, alias_conflicts = resolve_function_aliases(
            c, base_verification, target_obj, matched_by_va, validate_alias)
        # An explicit address already assigned to this symbol under another
        # name is a contradiction, never an override opportunity.
        for name, va in list(aliases.items()):
            if name in external and int(external[name]) != va:
                del aliases[name]
                conflict = {'symbol': name, 'candidate_vas': sorted({int(external[name]), va}),
                            'status': 'CONFLICT', 'reason': 'existing global name map disagrees'}
                alias_conflicts.append(conflict)
                alias_evidence.append(conflict)
        unit_external = dict(external)
        unit_external.update(aliases)
        verified = verify_unit(c, matched[u.name], exe, unit_external)
        verified['function_aliases'] = [{'symbol': n, 'va': va} for n, va in sorted(aliases.items())]
        verified['alias_evidence'] = alias_evidence
        verified['alias_conflicts'] = alias_conflicts
        verified['source_object'] = source_object['fingerprint']
        results.append(verified)
    for u in units:
        source_object = source_objects.get(u.name)
        if source_object is None:
            if os.path.exists(u.base_obj):
                raise RuntimeError('base object appeared during verification: %s' % os.path.abspath(u.base_obj))
            continue
        try:
            fingerprint, signature = _read_base_fingerprint(u.base_obj)
        except OSError as exc:
            raise RuntimeError('base object changed during verification: %s' % os.path.abspath(u.base_obj)) from exc
        if fingerprint != source_object['fingerprint'] or signature != source_object['signature']:
            raise RuntimeError('base object changed during verification: %s' % fingerprint['base_obj'])
    payload = {'units': results,
               'scope': 'only existing MATCH function roots; original-guided helper placement; no unrooted byte search'}
    text_ranges, xdata_ranges = [], []
    for result in results:
        for sec in result['sections'].values():
            if sec['status'] != 'MATCH' or not sec['reachable_from_match_root'] or sec['anchor'] is None:
                continue
            dest = text_ranges if sec['name'].startswith('.text$x') else xdata_ranges
            dest.append((sec['anchor'], sec['size']))
    text_count, text_bytes = _range_counts(text_ranges)
    xdata_count, xdata_bytes = _range_counts(xdata_ranges)
    payload['totals'] = {
        'text_helper_sections': text_count,
        'text_helper_bytes': text_bytes,
        'text_helper_source_instances': len(text_ranges),
        'xdata_sections': xdata_count,
        'xdata_bytes': xdata_bytes,
        'xdata_source_instances': len(xdata_ranges),
        'verified_relocations': sum(r['counts']['verified_relocations'] for r in results),
        'unverified_relocations': sum(r['counts']['unverified_relocations'] for r in results),
    }
    return payload


def _cli():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.dirname(__file__)), 'build', 'source_eh.json'))
    ap.add_argument('--unit', action='append', help='unit name under src/ (repeatable)')
    args = ap.parse_args()
    try:
        payload = verify_sources(args.unit)
    except _UnknownUnitName as exc:
        ap.error(str(exc))
    results = payload['units']
    totals = payload['totals']
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'w') as f:
        json.dump(payload, f, indent=2)
    print('wrote %s (%d rooted units)' % (args.out, len(results)))
    print('source EH: %d helpers (%d bytes), %d metadata sections (%d bytes), '
          '%d verified and %d unverified relocations' % (
              totals['text_helper_sections'], totals['text_helper_bytes'],
              totals['xdata_sections'], totals['xdata_bytes'],
              totals['verified_relocations'], totals['unverified_relocations']))
    return int(any(r['status'] in ('MISMATCH', 'CONFLICT', 'UNVERIFIED') or r['alias_conflicts']
                   for r in results))


if __name__ == '__main__':
    sys.exit(_cli())
