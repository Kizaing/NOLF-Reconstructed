"""Conservative code-entry alias tests for prebuilt library symbols and relocations."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import build


SOURCE_VA = 0x10001000
LIBRARY_VA = 0x10002000
FUNCTION_BYTES = 6
CODE_FLAGS = 0x60500020       # CNT_CODE | MEM_EXECUTE, plus the usual linker flags


class FakeSymbol:
    def __init__(self, name, value, secno, typ=0x20, cls=2):
        self.name, self.value, self.secno, self.typ, self.cls = name, value, secno, typ, cls
        self.naux = 0

    @property
    def is_section_symbol(self):
        return self.cls == 3 and self.naux > 0 and self.value == 0

    @property
    def is_function(self):
        return self.secno > 0 and self.typ == 0x20


class FakeSection:
    def __init__(self, index, name, size, flags=CODE_FLAGS, symbols=()):
        self.index, self.name, self.data, self.flags = index, name, bytes(size), flags
        self.syms = [s for s in symbols if s.secno == index]


class FakeOriginalObject:
    def __init__(self, sections, symbols):
        self.sections = sections
        self.symbols = {i: s for i, s in enumerate(symbols)}

    def section_of(self, sym):
        return self.sections[sym.secno - 1]


class BodyObject:
    def __init__(self, reloc_name, target_va, reloc_target_override=None):
        self.function = FakeSymbol('_TestFunction', 0, 1)
        self.reloc_symbol = FakeSymbol(reloc_name, 0, 0, typ=0, cls=2)
        self.symbols = {0: self.function, 1: self.reloc_symbol}
        self.sec = SimpleNamespace(data=b'\xe8\0\0\0\0\xc3')
        actual = target_va if reloc_target_override is None else reloc_target_override
        self.target = actual

    def functions(self):
        return [self.function]

    def extent(self, symbol):
        return self.sec, 0, len(self.sec.data)

    def relocs_in(self, sec, start, end):
        return [(1, self.reloc_symbol, build.REL_REL32, 0)]


class MemoryImage:
    def __init__(self, target_va, target_reloc=None):
        self.va = SOURCE_VA
        target = target_va if target_reloc is None else target_reloc
        displacement = target - (SOURCE_VA + 5)
        self.data = b'\xe8' + displacement.to_bytes(4, 'little', signed=True) + b'\xc3'
        self.import_slots = {}

    def read(self, va, size):
        return self.data[va - self.va:va - self.va + size]


def metadata_unit(name='lib/test', obj='lib.obj', *, base=LIBRARY_VA, canonical='__alloca_probe',
                  alias='__chkstk', size=FUNCTION_BYTES, grade='exact', section_name='.text',
                  alias_offset=0, alias_class=2, alias_type=0x20, function_entries=None):
    rows = [[0, canonical, 2, 0x20], [alias_offset, alias, alias_class, alias_type]]
    functions = function_entries or {'%08x' % base: canonical}
    return {
        'name': name,
        'obj': obj,
        'lib': 'test',
        'functions': functions,
        'sections': {'%08x' % base: [1, size, grade, 'unique', section_name, rows]},
        'data': {},
    }


def original_object(unit, *, actual_name=None, actual_size=None, flags=CODE_FLAGS,
                    actual_alias='__chkstk', alias_value=None, alias_secno=1,
                    alias_class=2, alias_type=0x20, canonical_name='__alloca_probe',
                    canonical_value=0, canonical_secno=1, canonical_class=2,
                    canonical_type=0x20):
    section_meta = next(iter(unit['sections'].values()))
    secno, size, _, _, section_name, rows = section_meta
    size = size if actual_size is None else actual_size
    section_name = section_name if actual_name is None else actual_name
    alias_value = rows[1][0] if alias_value is None else alias_value
    symbols = [FakeSymbol(canonical_name, canonical_value, canonical_secno, canonical_type, canonical_class),
               FakeSymbol(actual_alias, alias_value, alias_secno, alias_type, alias_class)]
    sections = [FakeSection(secno, section_name, size, flags, symbols)]
    return FakeOriginalObject(sections, symbols)


class LibraryAliasTests(unittest.TestCase):
    def run_scenario(self, lib_units, canonical_names, original_objects, *, reloc_name='__chkstk',
                     target_va=LIBRARY_VA, actual_target=None, annotation_namemap=None,
                     symtab_names=None, scope=None):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src, inc, output = root / 'src', root / 'include', root / 'build'
            src.mkdir()
            inc.mkdir()
            output.mkdir()
            source = src / 'test.cpp'
            source.write_text('// FUNCTION: %s 0x%08x _TestFunction\nvoid TestFunction() {}\n' %
                              (build.modcfg.TAG, SOURCE_VA))
            config = root / 'libraries.json'
            config.write_text(json.dumps({'units': lib_units, 'names': canonical_names}))
            body = BodyObject(reloc_name, target_va, actual_target)
            original_by_path = dict(original_objects)

            def coff(path):
                return original_by_path.get(path, body)

            with patch.multiple(build, BUILD=str(output), SRC=str(src), INC=str(inc),
                                LIBRARIES_JSON=str(config)), \
                    patch.object(build, 'CoffObj', side_effect=coff), \
                    patch.object(build, 'load_icf', return_value={}), \
                    contextlib.redirect_stdout(io.StringIO()) as output_log:
                libs = build.Libraries()
                unit = build.Unit(str(source))
                Path(unit.base_obj).parent.mkdir(parents=True, exist_ok=True)
                Path(unit.base_obj).touch()
                image = MemoryImage(target_va, actual_target)
                symtab = SimpleNamespace(funcs={SOURCE_VA: (SOURCE_VA + FUNCTION_BYTES, 'TestFunction')},
                                         names=symtab_names or {})
                namemap_override = (patch.object(build, 'build_namemap', return_value=annotation_namemap)
                                    if annotation_namemap is not None else contextlib.nullcontext())
                with namemap_override:
                    results, namemap = build.run_check([unit], image, symtab, libs=libs, scope=scope)
                log = output_log.getvalue()
                return libs, results, namemap, log

    def test_library_alias_resolves_correct_rel32_and_rejects_wrong_target(self):
        for grade in ('exact', 'verified'):
            with self.subTest(grade=grade):
                unit = metadata_unit(grade=grade)
                original = original_object(unit)
                canonical_names = {'%08x' % LIBRARY_VA: '__alloca_probe'}
                original_objects = {unit['obj']: original}
                libs, results, namemap, _ = self.run_scenario(
                    [unit], canonical_names, original_objects, target_va=LIBRARY_VA)

                self.assertEqual(libs.names, {LIBRARY_VA: '__alloca_probe'})
                self.assertEqual(libs.code_aliases, {'__chkstk': LIBRARY_VA})
                self.assertEqual(len(results), 1)
                self.assertEqual(results[0].status, 'MATCH')
                self.assertEqual(results[0].unverified, [])
                self.assertEqual(build.matched_function_coverage(results), {SOURCE_VA: FUNCTION_BYTES})
                self.assertEqual(namemap[LIBRARY_VA], '__alloca_probe')
                self.assertNotIn('__chkstk', namemap.values())

                _, wrong_results, _, _ = self.run_scenario(
                    [unit], canonical_names, original_objects,
                    target_va=LIBRARY_VA, actual_target=LIBRARY_VA + 0x10)
                self.assertEqual(wrong_results[0].status, 'RELOC')
                self.assertEqual(wrong_results[0].bad_relocs,
                                 [(1, '__chkstk', LIBRARY_VA, LIBRARY_VA + 0x10)])

    def test_unverified_and_data_sections_do_not_supply_aliases(self):
        cases = [
            ('unverified', {'grade': 'unverified'}),
            ('data section', {'section_name': '.data'}),
        ]
        for name, options in cases:
            with self.subTest(name=name):
                unit = metadata_unit(**options)
                flags = 0xC0300040 if name == 'data section' else CODE_FLAGS
                original = original_object(unit, flags=flags)
                libs, results, _, _ = self.run_scenario(
                    [unit], {'%08x' % LIBRARY_VA: '__alloca_probe'}, {unit['obj']: original})
                self.assertNotIn('__chkstk', libs.code_aliases)
                self.assertEqual(results[0].status, 'MATCH')
                self.assertEqual(results[0].unverified, [(LIBRARY_VA, '__chkstk')])

    def test_only_actual_external_typed_in_bounds_canonical_entry_symbols_bind(self):
        cases = [
            ('static', {'alias_class': 3}, {'alias_class': 3}),
            ('undefined', {}, {'alias_secno': 0}),
            ('type-zero label', {'alias_type': 0}, {'alias_type': 0}),
            ('section-end offset', {'alias_offset': FUNCTION_BYTES}, {'alias_value': FUNCTION_BYTES}),
            ('not a canonical entry', {'alias_offset': 1}, {'alias_value': 1}),
        ]
        for name, meta_options, actual_options in cases:
            with self.subTest(name=name):
                unit = metadata_unit(**meta_options)
                original = original_object(unit, **actual_options)
                libs, _, _, _ = self.run_scenario(
                    [unit], {'%08x' % LIBRARY_VA: '__alloca_probe'}, {unit['obj']: original})
                self.assertNotIn('__chkstk', libs.code_aliases)

    def test_section_and_symbol_metadata_must_match_original_coff(self):
        cases = [
            ('wrong section name', {}, {'actual_name': '.fake'}),
            ('wrong section size', {}, {'actual_size': FUNCTION_BYTES + 1}),
            ('not code', {}, {'flags': 0x60000040}),
            ('missing alias symbol', {}, {'actual_alias': '__not_chkstk'}),
            ('wrong canonical symbol', {}, {'canonical_name': '__not_alloca_probe'}),
        ]
        for name, meta_options, actual_options in cases:
            with self.subTest(name=name):
                unit = metadata_unit(**meta_options)
                original = original_object(unit, **actual_options)
                libs, _, _, _ = self.run_scenario(
                    [unit], {'%08x' % LIBRARY_VA: '__alloca_probe'}, {unit['obj']: original})
                self.assertNotIn('__chkstk', libs.code_aliases)
                self.assertTrue(libs.code_alias_problems, 'expected a COFF mismatch diagnostic')

    def test_duplicate_same_address_candidates_deduplicate_and_distinct_candidates_are_rejected(self):
        first = metadata_unit(name='lib/first', obj='first.obj')
        second = metadata_unit(name='lib/second', obj='second.obj')
        originals = {'first.obj': original_object(first), 'second.obj': original_object(second)}
        canonical_names = {'%08x' % LIBRARY_VA: '__alloca_probe'}
        libs, _, _, _ = self.run_scenario([first, second], canonical_names, originals)
        self.assertEqual(libs.code_aliases, {'__chkstk': LIBRARY_VA})

        second = metadata_unit(name='lib/second', obj='second.obj', base=LIBRARY_VA + 0x20)
        originals['second.obj'] = original_object(second)
        names = {}
        libs, _, _, _ = self.run_scenario([first, second], names, originals)
        self.assertNotIn('__chkstk', libs.code_aliases)
        self.assertTrue(any('ambiguous library code alias __chkstk' in p for p in libs.code_alias_problems))

    def test_canonical_library_name_conflicts_and_annotation_conflicts_are_reported(self):
        unit = metadata_unit()
        original = original_object(unit)
        # The metadata canonical entry name itself cannot point elsewhere in the canonical map.
        canonical_mismatch = {'%08x' % (LIBRARY_VA + 0x40): '__alloca_probe'}
        libs, _, _, _ = self.run_scenario([unit], canonical_mismatch, {unit['obj']: original})
        self.assertNotIn('__chkstk', libs.code_aliases)
        self.assertTrue(any('library canonical entry' in p for p in libs.code_alias_problems))

        # A canonical library name cannot be rebound to an alias candidate at another address.
        names = {'%08x' % LIBRARY_VA: '__alloca_probe',
                 '%08x' % (LIBRARY_VA + 0x20): '__chkstk'}
        libs, _, _, _ = self.run_scenario([unit], names, {unit['obj']: original})
        self.assertNotIn('__chkstk', libs.code_aliases)
        self.assertTrue(any('conflicts with canonical library name' in p for p in libs.code_alias_problems))

        # A source/canonical map already using the alias at another VA remains intact and diagnosed.
        bad_va = LIBRARY_VA + 0x40
        namemap = ({SOURCE_VA: '_TestFunction'},
                   {'_TestFunction': SOURCE_VA, '__chkstk': bad_va}, {}, [])
        libs, results, _, log = self.run_scenario(
            [unit], {'%08x' % LIBRARY_VA: '__alloca_probe'}, {unit['obj']: original},
            annotation_namemap=namemap, scope={'test'})
        self.assertEqual(results[0].status, 'RELOC')
        self.assertIn('library code alias __chkstk', log)
        self.assertEqual(build.ALL_NAMES['__chkstk'], bad_va)

    def test_stricmp_does_not_bind_underscore_stricmp_and_learned_names_do_not_qualify(self):
        unit = metadata_unit(canonical='__strcmpi', alias='__stricmp')
        original = original_object(unit, canonical_name='__strcmpi', actual_alias='__stricmp')
        libs, results, _, _ = self.run_scenario(
            [unit], {'%08x' % LIBRARY_VA: '__strcmpi'}, {unit['obj']: original},
            reloc_name='_stricmp', symtab_names={LIBRARY_VA: '__stricmp'})
        self.assertEqual(libs.code_aliases, {'__stricmp': LIBRARY_VA})
        self.assertNotIn('_stricmp', libs.code_aliases)
        self.assertEqual(results[0].status, 'MATCH')
        self.assertEqual(results[0].unverified, [(LIBRARY_VA, '_stricmp')])
        self.assertEqual(build.ALL_NAMES.get('_stricmp'), LIBRARY_VA)


if __name__ == '__main__':
    unittest.main()
