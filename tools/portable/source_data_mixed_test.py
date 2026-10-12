import copy
import hashlib
import json
from pathlib import Path
import struct
import unittest

import source_data_mixed as mixed


class MixedDeclarationTests(unittest.TestCase):
    def image(self):
        image = bytearray(256)
        struct.pack_into("<3f", image, 64, -0.25, 0.0, 12.5)
        struct.pack_into("<2I", image, 16, mixed.IMAGE_BASE + 64, mixed.IMAGE_BASE + 72)
        return image

    def recipe(self, result, **source):
        return {"output": "synthetic__values_0.inc", "source": source,
                "syntax": "static const float values[3]={@@,@@,@@};",
                "formats": [{"style": "decimal", "precision": 2, "omit_zero": True, "suffix": "f"},
                            {"style": "integer"},
                            {"style": "decimal", "precision": 1, "suffix": "f"}],
                "sha256": hashlib.sha256(result).hexdigest()}

    def extract(self, image, row):
        return mixed.extract(bytes(image), {"schema": 1, "declarations": [row]},
                             expected_sha256=hashlib.sha256(image).hexdigest())

    def test_exact_source_rounding_and_original_declaration_bytes(self):
        image = self.image()
        text = b"static const float values[3]={-.25f,0,12.5f};"
        row = self.recipe(text, format="f", address=hex(mixed.IMAGE_BASE + 64))
        self.assertEqual(self.extract(image, row)[row["output"]], text)

    def test_pointer_source_and_range_validation(self):
        image = self.image()
        row = self.recipe(b"static const float values[3]={-.25f,12.5f};", format="f",
                          mode="pointer_field", table=hex(mixed.IMAGE_BASE + 16), offset=0)
        row["syntax"] = "static const float values[3]={@@,@@};"
        row["formats"].pop(1)
        self.extract(image, row)
        struct.pack_into("<I", image, 16, mixed.IMAGE_BASE + len(image))
        with self.assertRaisesRegex(ValueError, "outside"):
            self.extract(image, row)

    def test_payload_and_whole_executable_fail_closed(self):
        image = self.image()
        row = self.recipe(b"static const float values[3]={-.25f,0,12.5f};", format="f",
                          address=hex(mixed.IMAGE_BASE + 64))
        with self.assertRaisesRegex(ValueError, "identity"):
            mixed.extract(bytes(image), {"schema": 1, "declarations": [row]})
        struct.pack_into("<f", image, 64, -0.75)
        with self.assertRaisesRegex(ValueError, "match review"):
            self.extract(image, row)
        struct.pack_into("<f", image, 64, float("nan"))
        with self.assertRaisesRegex(ValueError, "Non-finite"):
            self.extract(image, row)

    def test_mixed_integer_float_record_does_not_reinterpret_integer_bits(self):
        image = self.image()
        struct.pack_into("<ifi", image, 64, 8, -0.5, 3)
        row = self.recipe(b"static const float values[3]={8.,-.5f,3};", format="f",
                          mode="mixed_record", address=hex(mixed.IMAGE_BASE + 64), storage=["i", "f", "i"])
        row["formats"] = [{"style": "decimal", "precision": 0},
                          {"style": "decimal", "precision": 1, "omit_zero": True, "suffix": "f"},
                          {"style": "integer"}]
        self.extract(image, row)

    def test_invalid_destination_duplicate_and_placeholder_count(self):
        image = self.image()
        row = self.recipe(b"static const float values[3]={-.25f,0,12.5f};", format="f",
                          address=hex(mixed.IMAGE_BASE + 64))
        for bad in ("../escape.inc", "/absolute.inc", "a/b.inc"):
            copy_row = copy.deepcopy(row); copy_row["output"] = bad
            with self.assertRaisesRegex(ValueError, "Unsafe"):
                self.extract(image, copy_row)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            mixed.extract(bytes(image), {"schema": 1, "declarations": [row, row]},
                          expected_sha256=hashlib.sha256(image).hexdigest())
        row["syntax"] += "@@"
        with self.assertRaisesRegex(ValueError, "placeholder"):
            self.extract(image, row)

    def test_bgm_chain_retains_loop_and_bounds(self):
        image = self.image()
        struct.pack_into("<3I", image, 24, mixed.IMAGE_BASE + 112, 0, 0)
        struct.pack_into("<4I", image, 112, 2, mixed.IMAGE_BASE + 120, 1, mixed.IMAGE_BASE + 112)
        text = "  {0, 2, 0, {" + ",".join(map(str, [2, 1] + [0] * 30)) + "}},"
        row = {"output": "test_bgm.inc", "prefix": "", "suffix": "",
               "source": {"mode": "bgm_entries", "address": hex(mixed.IMAGE_BASE + 24), "count": 1, "names": 3},
               "sha256": hashlib.sha256(text.encode()).hexdigest()}
        self.assertEqual(self.extract(image, row)[row["output"]].decode(), text)
        struct.pack_into("<I", image, 112, 3)
        with self.assertRaisesRegex(ValueError, "filename index"):
            self.extract(image, row)

    def test_bgm_names_come_from_disc_and_validate_extension(self):
        image = self.image();image[160:168] = b"ONE.ADX\0"
        text = 'const char *names[]={"ONE"};'
        row = {"output": "test_names.inc", "prefix": 'const char *names[]={', "suffix": "};",
               "source": {"mode": "bgm_names", "address": hex(mixed.IMAGE_BASE + 160), "count": 1, "stride": 24, "width": 16},
               "sha256": hashlib.sha256(text.encode()).hexdigest()}
        self.assertEqual(self.extract(image, row)[row["output"]].decode(), text)
        image[165] = ord("X")
        with self.assertRaisesRegex(ValueError, "filename"):
            self.extract(image, row)

    def test_bank_identity_and_relocated_addresses(self):
        image = self.image();bank = struct.pack("<3H", 2, 4, 8)
        text = b"static const unsigned frames[3]={2,4,8};"
        row = {"output": "test_frames.inc", "syntax": "static const unsigned frames[3]={@@,@@,@@};",
               "formats": [{"style": "integer"}] * 3, "sha256": hashlib.sha256(text).hexdigest(),
               "source": {"file": "ENEMY/001_test.bin", "sha256": hashlib.sha256(bank).hexdigest(),
                          "base": "0x8ca00000", "address": "0x8ca00000", "format": "H"}}
        recipes = {"schema": 1, "declarations": [row]}
        digest = hashlib.sha256(image).hexdigest()
        with self.assertRaisesRegex(ValueError, "Missing original bank"):
            mixed.extract(bytes(image), recipes, expected_sha256=digest)
        self.assertEqual(mixed.extract(bytes(image), recipes, expected_sha256=digest,
                                       banks={"ENEMY/001_test.bin": bank})[row["output"]], text)
        with self.assertRaisesRegex(ValueError, "bank identity"):
            mixed.extract(bytes(image), recipes, expected_sha256=digest,
                          banks={"ENEMY/001_test.bin": bytes(len(bank))})

    def test_actual_user_executable_when_present(self):
        root = Path(__file__).resolve().parents[2]
        exe = root / "decompile/extracted/1ST_READ.BIN"
        if not exe.exists():
            self.skipTest("No private user executable in source-only checkout")
        recipes = json.loads(mixed.RECIPES.read_text())
        banks = {r["source"]["file"]: (root / "decompile/afs" / r["source"]["file"]).read_bytes()
                 for r in recipes["declarations"] if "file" in r["source"]}
        output = mixed.extract(exe.read_bytes(), recipes, banks=banks)
        manifest = json.loads((root / "tools/portable/source_data_manifest.json").read_text())
        expected = {Path(f["output"]).name: f["sha256"] for r in manifest["files"] for f in r.get("fragments", [])}
        self.assertGreaterEqual(len(output), 39)
        for name, data in output.items():
            self.assertEqual(hashlib.sha256(data).hexdigest(), expected[name], name)


if __name__ == "__main__":
    unittest.main()
