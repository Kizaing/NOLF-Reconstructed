import hashlib
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import coffedit as F
import source_eh


class SourceEHApiTests(unittest.TestCase):
    def test_unknown_unit_names_raise_value_error(self):
        with self.assertRaisesRegex(ValueError, 'unknown unit'):
            source_eh.verify_sources(['__source_eh_api_unknown_unit__'])

    def test_loaded_base_object_has_absolute_path_and_sha256(self):
        c = F.Coff()
        c.sections.append(F.Sec('.text$x', b'HELP', [], F.SCN_CNT_CODE))
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / 'fixture.obj'
            c.save(str(path))

            provenance, signature = source_eh._read_base_fingerprint(str(path))
            loaded = source_eh._load_base_object(str(path), 'fixture', F.Coff, signature)

            self.assertEqual(str(path.resolve()), provenance['base_obj'])
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), provenance['sha256'])
            self.assertEqual('fixture', loaded.name)
            self.assertEqual(b'HELP', loaded.sections[0].data)

    def test_object_changed_before_load_is_rejected(self):
        loader = type('Loader', (), {'load': lambda path: self.fail('stale object was loaded')})
        with patch.object(source_eh, '_stat_signature', return_value=(2,)):
            with self.assertRaisesRegex(RuntimeError, 'changed before loading'):
                source_eh._load_base_object('fixture.obj', 'fixture', loader, (1,))

    def test_object_changed_during_load_is_rejected(self):
        loader = type('Loader', (), {'load': lambda path: F.Coff()})
        with patch.object(source_eh, '_stat_signature', side_effect=[(1,), (2,)]):
            with self.assertRaisesRegex(RuntimeError, 'changed while loading'):
                source_eh._load_base_object('fixture.obj', 'fixture', loader, (1,))


if __name__ == '__main__':
    unittest.main()
