"""Disc-backed binding equivalence and fail-closed extraction checks."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import runtime_bindings as bindings


class RuntimeBindingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[2]
        cls.tree = cls.root/'build/portable/clean-disc-proof/tree'
        if not (cls.tree/'decompile/extracted/1ST_READ.BIN').is_file():
            cls.tree = cls.root
        if not (cls.tree/'decompile/extracted/1ST_READ.BIN').is_file():
            raise unittest.SkipTest('Private user-disc extraction absent')
        cls.metadata = json.loads(bindings.METADATA.read_text())

    def test_every_output_matches_reviewed_runtime_declarations(self):
        outputs = bindings.extract(self.tree)
        self.assertEqual(set(outputs), set(self.metadata['outputs']))
        for name, data in outputs.items():
            self.assertEqual(hashlib.sha256(data).hexdigest(), self.metadata['outputs'][name])

    def test_changed_executable_is_rejected_before_banks(self):
        with patch.object(bindings, 'EXECUTABLE_SHA256', '0'*64):
            with self.assertRaisesRegex(ValueError, 'Unsupported executable'):
                bindings.extract(self.tree)

    def test_changed_bank_identity_is_rejected(self):
        metadata = copy.deepcopy(self.metadata)
        next(iter(metadata['banks'].values()))['sha256'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'Unsupported original enemy bank'):
            bindings.extract(self.tree, metadata)

    def test_unknown_schema_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'metadata schema'):
            bindings.extract(self.tree, {'schema': 2})

    def test_bank_path_cannot_leave_supplied_bank_directory(self):
        metadata = copy.deepcopy(self.metadata)
        next(iter(metadata['banks'].values()))['path'] = '../001_keikandat.bin'
        with self.assertRaisesRegex(ValueError, 'Unsafe original enemy bank path'):
            bindings.extract(self.tree, metadata)

    def test_changed_output_identity_is_rejected(self):
        metadata = copy.deepcopy(self.metadata)
        metadata['outputs']['main_enemy_maps.inc'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'differs from reviewed declaration'):
            bindings.extract(self.tree, metadata)

    def test_missing_disc_has_no_development_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError):
                bindings.extract(Path(directory))

    def test_changed_original_emitter_bank_is_rejected(self):
        metadata=copy.deepcopy(self.metadata)
        for name in metadata['sources']:
            metadata['sources'][name]='0'*64
            break
        with self.assertRaisesRegex(ValueError,'Unsupported original emitter source'):
            bindings.extract(self.tree,metadata)

    def test_missing_original_emitter_identity_cannot_skip_source_validation(self):
        metadata=copy.deepcopy(self.metadata);metadata.pop('sources')
        with self.assertRaisesRegex(ValueError,'source inventory differs'):
            bindings.extract(self.tree,metadata)


if __name__ == '__main__':
    unittest.main()
