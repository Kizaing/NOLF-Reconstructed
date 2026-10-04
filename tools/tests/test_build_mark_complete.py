import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build


def result(unit, va, kind='FUNCTION', status='MATCH', symbol=True):
    annot = SimpleNamespace(kind=kind, va=va, symbol=object() if symbol else None,
                            unit=SimpleNamespace(name=unit))
    return SimpleNamespace(a=annot, status=status)


class MarkCompleteTests(unittest.TestCase):
    def mark(self, units, vas, results):
        with patch.dict(build.OBJVAS, vas, clear=True):
            build.mark_complete(units, results)

    def test_match_in_another_unit_does_not_complete_source_unit(self):
        units = [{'name': 'model/model', 'metadata': {'source_path': 'src/model/model.cpp'}}]
        self.mark(units, {'model/model': [0x44F8F0]}, [result('shared/objectmgr', 0x44F8F0)])
        self.assertFalse(units[0]['metadata']['complete'])

    def test_all_functions_matching_in_same_unit_completes(self):
        units = [{'name': 'client/example', 'metadata': {'source_path': 'src/client/example.cpp'}}]
        self.mark(units, {'client/example': [0x401000, 0x401010]}, [
            result('client/example', 0x401000), result('client/example', 0x401010),
        ])
        self.assertTrue(units[0]['metadata']['complete'])

    def test_empty_target_is_not_complete(self):
        units = [{'name': 'client/empty', 'metadata': {'source_path': 'src/client/empty.cpp'}}]
        self.mark(units, {'client/empty': []}, [])
        self.assertFalse(units[0]['metadata']['complete'])

    def test_matching_stub_does_not_complete_source_unit(self):
        units = [{'name': 'client/example', 'metadata': {'source_path': 'src/client/example.cpp'}}]
        self.mark(units, {'client/example': [0x401000]}, [
            result('client/example', 0x401000, kind='STUB'),
        ])
        self.assertFalse(units[0]['metadata']['complete'])

    def test_unbound_function_match_does_not_complete_source_unit(self):
        units = [{'name': 'client/example', 'metadata': {'source_path': 'src/client/example.cpp'}}]
        self.mark(units, {'client/example': [0x401000]}, [
            result('client/example', 0x401000, symbol=False),
        ])
        self.assertFalse(units[0]['metadata']['complete'])

    def test_library_completion_metadata_is_preserved(self):
        units = [{'name': 'lib/WONAPI', 'metadata': {'complete': True}}]
        self.mark(units, {}, [])
        self.assertTrue(units[0]['metadata']['complete'])


if __name__ == '__main__':
    unittest.main()
