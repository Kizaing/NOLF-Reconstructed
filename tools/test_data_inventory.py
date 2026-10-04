"""Pure helper tests for the read-only data inventory evidence collector."""
import os
import sys
import unittest
from types import SimpleNamespace
from unittest import mock

TOOLS = os.path.dirname(os.path.abspath(__file__))
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)

import data_inventory as inventory
from coffobj import REL_DIR32, REL_REL32, REL_SECTION


class RelocationArithmeticTests(unittest.TestCase):
    def test_rel32_uses_signed_field_and_negative_addend(self):
        field_va = 0x10001000
        field = (-8).to_bytes(4, 'little', signed=True)
        target = inventory.relocation_target(field_va, field, -4, REL_REL32)
        self.assertEqual(target, 0x10001000)
        self.assertEqual(
            inventory.section_base_from_relocation(field_va, field, -4, REL_REL32, 0x10),
            0x10000ff0)

    def test_dir32_subtracts_original_coff_addend(self):
        field = (0x10002008).to_bytes(4, 'little')
        self.assertEqual(inventory.relocation_target(0x10000000, field, 0x18, REL_DIR32), 0x10001ff0)


class CandidateAggregationTests(unittest.TestCase):
    def test_icf_alias_observations_count_one_original_function_va(self):
        observations = [
            {'id': 1, 'valid': True, 'evidence_class': 'code_function', 'source_va_value': 0x10001000,
             'candidate_base_value': 0x10048000},
            {'id': 2, 'valid': True, 'evidence_class': 'code_function', 'source_va_value': 0x10001000,
             'candidate_base_value': 0x10048000},
            {'id': 3, 'valid': True, 'evidence_class': 'code_function', 'source_va_value': 0x10002000,
             'candidate_base_value': 0x10048000},
        ]
        classification, candidates = inventory.summarize_candidates(observations)
        self.assertEqual(classification, 'observed')
        self.assertEqual(candidates[0]['distinct_function_va_count'], 2)

    def test_conflicting_bases_are_preserved(self):
        observations = [
            {'id': 1, 'valid': True, 'evidence_class': 'global', 'candidate_base_value': 0x10048000},
            {'id': 2, 'valid': True, 'evidence_class': 'code_function', 'source_va_value': 0x10001000,
             'candidate_base_value': 0x10048100},
        ]
        classification, candidates = inventory.summarize_candidates(observations)
        self.assertEqual(classification, 'conflicting')
        self.assertEqual([item['candidate_base_value'] for item in candidates], [0x10048000, 0x10048100])

    def test_zero_filled_bss_content_cannot_anchor_a_section(self):
        content = bytes(16)
        comparison = inventory.compare_non_relocation_bytes(content, content, [])
        self.assertEqual(comparison['status'], 'exact_without_relocations')
        classification, candidates = inventory.summarize_candidates([])
        self.assertEqual(classification, 'unlocated')
        self.assertEqual(candidates, [])


class RelocationComparisonTests(unittest.TestCase):
    def test_truncated_relocation_range_is_unavailable(self):
        result = inventory.compare_non_relocation_bytes(bytes(8), bytes(8), [(7, 1, REL_DIR32)])
        self.assertEqual(result['status'], 'unavailable')
        self.assertEqual(result['diagnostics'][0]['reason'], 'relocation_field_out_of_section')

    def test_truncated_target_span_is_unavailable(self):
        result = inventory.compare_non_relocation_bytes(bytes(8), bytes(7), [])
        self.assertEqual(result['status'], 'unavailable')
        self.assertEqual(result['diagnostics'][0]['reason'], 'target_range_truncated')

    def test_unsupported_relocation_is_not_masked_at_four_bytes(self):
        result = inventory.compare_non_relocation_bytes(bytes(8), bytes(8), [(2, 1, 0x7777)])
        self.assertEqual(result['status'], 'unavailable')
        self.assertEqual(result['masked_relocation_bytes'], 0)
        self.assertEqual(result['diagnostics'][0]['reason'], 'unsupported_relocation_type')

    def test_supported_width_comes_from_coff_relocation_table(self):
        section = bytes(range(8))
        target = bytearray(section)
        target[1:3] = b'\xff\xee'
        result = inventory.compare_non_relocation_bytes(section, bytes(target), [(1, 0, REL_SECTION)])
        self.assertEqual(result['status'], 'equal_with_relocations_unchecked')
        self.assertEqual(result['masked_relocation_bytes'], 2)


class PreflightTests(unittest.TestCase):
    def test_each_unit_uses_its_own_snapshot_record(self):
        first = SimpleNamespace(name='first', path='first.cpp', base_obj='first.obj', rel='first.cpp')
        second = SimpleNamespace(name='second', path='second.cpp', base_obj='second.obj', rel='second.cpp')
        units = [first, second]
        snapshots = {
            'first': {'snapshot_status': 'stable', 'source_sha1_after': 'source-first'},
            'second': {'snapshot_status': 'changed_during_snapshot', 'source_sha1_after': 'source-second'},
        }
        hashes = {
            'first.cpp': 'source-first', 'first.obj': 'object-first',
            'second.cpp': 'source-second', 'second.obj': 'object-second',
        }
        fake_build = SimpleNamespace(
            _read_stamp=lambda path: 'stamp-' + path,
            _unit_stamp=lambda unit: 'stamp-' + unit.base_obj,
        )
        with mock.patch.object(inventory, 'file_sha1', side_effect=lambda path: hashes[path]), \
                mock.patch.object(inventory.os.path, 'isfile', return_value=True), \
                mock.patch.object(inventory, 'CoffObj'):
            current, records = inventory._preflight(fake_build, units, snapshots, 'headers', 'compiler')

        self.assertEqual(current, [first])
        self.assertEqual(records['first']['stamp_status'], 'current')
        self.assertEqual(records['second']['stamp_status'], 'changed_during_snapshot')
        self.assertIs(records['first'], snapshots['first'])
        self.assertIs(records['second'], snapshots['second'])

    def test_missing_source_snapshot_is_kept_as_excluded_record(self):
        snapshots = {'gone': {'unit': 'gone', 'snapshot_status': 'missing_source'}}
        records = inventory._retain_snapshot_records(snapshots, {}, 'headers', 'compiler')
        self.assertIn('gone', records)
        self.assertEqual(records['gone']['stamp_status'], 'missing_source')
        self.assertFalse(records['gone']['stamp_current_before'])


class CandidateSpanTests(unittest.TestCase):
    def test_raw_padding_does_not_make_a_full_virtual_span_valid(self):
        pe_section = SimpleNamespace(
            VirtualAddress=0x1000,
            Misc_VirtualSize=8,
            SizeOfRawData=16,
            Name=b'.data\0\0\0',
        )
        fake_exe = SimpleNamespace(
            base=0x400000,
            pe=SimpleNamespace(sections=[pe_section]),
            read=lambda va, size: bytes(size),
        )
        data_section = SimpleNamespace(index=1, data=bytes(16))
        obj = SimpleNamespace(sections=[data_section])
        observation = {
            'id': 7, 'valid': True, 'definition_unit': 'unit', 'definition_section_index': 1,
            'candidate_base_value': 0x401000, 'candidate_base': '0x00401000',
            'source_va': '0x00402000', 'field_va': '0x00402004', 'coff_offset': 4,
            'evidence_class': 'code_function',
            'definition': {'unit': 'unit', 'section_index': 1, 'section_size': 16},
        }
        diagnostics = []
        inventory._validate_candidate_spans({'unit': obj}, fake_exe, [observation], diagnostics)
        self.assertFalse(observation['valid'])
        self.assertFalse(observation['candidate_span_valid'])
        self.assertEqual(observation['candidate_base_value'], 0x401000)
        self.assertEqual(observation['candidate_span_mapping']['reason'],
                         'candidate span is outside one full PE VirtualSize range')
        self.assertEqual(diagnostics[0]['candidate_base'], '0x00401000')
        self.assertEqual(diagnostics[0]['source_va'], '0x00402000')
        classification, candidates = inventory.summarize_candidates([observation])
        self.assertEqual(classification, 'unlocated')
        self.assertEqual(candidates, [])


if __name__ == '__main__':
    unittest.main()
