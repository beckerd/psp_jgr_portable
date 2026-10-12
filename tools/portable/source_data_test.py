#!/usr/bin/env python3
"""Asset-independent export safety and C-scope equivalence tests."""
from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("source_data", Path(__file__).with_name("source_data.py"))
source_data = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(source_data)


class SourceDataTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="jgr-source-boundary-")
        self.base = Path(self.temp.name).resolve()
        self.root = self.base / "checkout"
        (self.root / "psp/src").mkdir(parents=True)
        self.manifest = {"schema": 1, "files": []}

    def tearDown(self):
        self.temp.cleanup()

    def add(self, name, text, kind="portable_implementation", fragments=()):
        path = "psp/src/" + name
        data = text.encode()
        (self.root / path).write_bytes(data)
        row = {"path": path, "kind": kind, "sha256": source_data.digest(data),
               "review": "Synthetic test implementation or data; not a game asset."}
        if fragments:
            row["fragments"] = []
            for symbol, snippet in fragments:
                payload = snippet.encode()
                start = data.index(payload)
                row["fragments"].append({"symbol": symbol, "start": start,
                    "end": start + len(payload), "sha256": source_data.digest(payload),
                    "output": f"{source_data.GENERATED_ROOT}/{name}__{symbol}.inc",
                    "provenance": "Synthetic table fixture", "generator": None})
        if kind in source_data.PRIVATE - {"private_backup"}:
            row["output"] = f"{source_data.GENERATED_ROOT}/{name}"
        self.manifest["files"].append(row)
        return row

    def test_stage_omits_original_content_without_stubs(self):
        secret = "static const char *secret = \"PRIVATE PAYLOAD\";\n"
        self.add("payload.inc", secret, "game_data")
        self.add("engine.c", '#include "payload.inc"\nint answer(void){return 4;}\n')
        original = (self.root / "psp/src/engine.c").read_bytes()
        out = self.base / "public"
        report = source_data.stage(self.root, out, self.manifest)
        self.assertTrue(report["provenance_ready"])
        self.assertFalse(report["build_ready"])
        self.assertEqual(original, (self.root / "psp/src/engine.c").read_bytes())
        self.assertFalse((out / "psp/src/payload.inc").exists())
        self.assertFalse((out / source_data.GENERATED_ROOT).exists())
        self.assertIn(b'"generated/source_data/payload.inc"', (out / "psp/src/engine.c").read_bytes())
        for file in out.rglob("*"):
            if file.is_file():
                self.assertNotIn(b"PRIVATE PAYLOAD", file.read_bytes())

    def test_unknown_source_fails_without_partial_output(self):
        self.add("a.h", "int a(void);\n")
        (self.root / "psp/src/new.h").write_text("int new_function(void);\n")
        out = self.base / "export"
        with self.assertRaisesRegex(source_data.SourceDataError, "Unreviewed runtime source"):
            source_data.stage(self.root, out, self.manifest)
        self.assertFalse(out.exists())

    def test_explicit_third_party_source_is_public_but_numeric_inputs_stay_private(self):
        self.add("licensed.inc", "/* Authored license fixture. */\nconst int hardware=7;\n",
                 "third_party_implementation")
        self.add("local_numeric.inc", "const int local_input=9;\n", "third_party_numeric_data")
        out = self.base / "public"
        report = source_data.stage(self.root, out, self.manifest)
        self.assertTrue(report["provenance_ready"])
        self.assertTrue((out / "psp/src/licensed.inc").is_file())
        self.assertFalse((out / "psp/src/local_numeric.inc").exists())

    def test_runtime_blob_interface_builds_without_legacy_initializer(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        original = "static const int data[2]={713,919};"
        row = self.add("game.c", '#include <stdio.h>\n#if JGR_PORTABLE_ASSETS\n'
            '#include "data_abi.h"\n#else\n' + original + '\n#endif\n'
            'int main(void){data[0]=8;data[1]=3;printf("%d\\n",data[0]+data[1]);}\n',
            "mixed_implementation", [("data", original)])
        row["fragments"][0].update(required_for_portable_build=False,
            runtime_blob="data.jsd", regeneration_status="unverified")
        self.add("data_abi.h", "extern int data[2];\n")
        self.add("data_abi.c", "int data[2];\n")
        out = self.base / "source-only"
        report = source_data.stage(self.root, out, self.manifest)
        self.assertTrue(report["build_ready"])
        self.assertEqual(report["generated"][0]["regeneration_status"], "unverified")
        self.assertFalse((out / source_data.GENERATED_ROOT).exists())
        self.assertNotIn(b"713", (out / "psp/src/game.c").read_bytes())
        executable = self.base / "portable"
        subprocess.run([compiler, "-DJGR_PORTABLE_ASSETS=1", "-std=c99", "-Wall", "-Werror",
            str(out / "psp/src/game.c"), str(out / "psp/src/data_abi.c"),
            "-o", str(executable)], check=True, capture_output=True)
        self.assertEqual(subprocess.check_output([executable]), b"11\n")

    def test_new_source_extension_does_not_default_to_public(self):
        self.add("a.h", "int a(void);\n")
        for extension in ("inc", "cpp", "S"):
            (self.root / ("psp/src/new." + extension)).write_text("const int secret=99;\n")
        report = source_data.plan(self.root, self.manifest)
        self.assertFalse(report["provenance_ready"])
        self.assertNotIn("psp/src/new.inc", report["public"])
        self.assertEqual(len(report["blockers"]), 3)

    def test_reverb_keeps_code_and_splits_preset_parameters(self):
        text = "static int calculate(unsigned cursor,int x){unsigned at7=(cursor+12345)&SOURCE_REVERB_MASK;return source_reverb_multiply((x),-2345)+at7;}\n"
        row = self.add("program.inc", text, "mixed_implementation")
        code, parameters = source_data.source_data_reverb.split(text.encode())
        row["parameter_split"] = {"kind": "reverb_parameters_v1",
            "output": source_data.GENERATED_ROOT + "/reverb_parameters.inc",
            "sha256": source_data.digest(parameters), "provenance": "Synthetic effect preset"}
        public, private = self.base / "public", self.base / "private"
        source_data.stage(self.root, public, self.manifest)
        source_data.materialize_private_inputs(self.root, private, self.manifest)
        exported = (public / row["path"]).read_bytes()
        self.assertEqual(exported, code)
        self.assertIn(b"return source_reverb_multiply", exported)
        self.assertNotIn(b"-2345", exported)
        self.assertNotIn(b"12345", exported)
        self.assertEqual((private / row["parameter_split"]["output"]).read_bytes(), parameters)
        compiler = shutil.which("cc")
        if compiler:
            helper = "#include <stdio.h>\n#define SOURCE_REVERB_MASK 32767u\nstatic int source_reverb_multiply(int x,int c){return x*c;}\n#include \"program.inc\"\nint main(void){printf(\"%d\\n\",calculate(50,7));}\n"
            (self.root / "psp/src/check.c").write_text(helper)
            (public / "psp/src/check.c").write_text(helper)
            destination = public / row["parameter_split"]["output"]
            destination.parent.mkdir(parents=True)
            shutil.copyfile(private / row["parameter_split"]["output"], destination)
            outputs = []
            for tree in (self.root, public):
                executable = tree / "check"
                subprocess.run([compiler, "-std=c99", "-O2", str(tree / "psp/src/check.c"), "-o", str(executable)], check=True)
                outputs.append(subprocess.check_output([executable]))
            self.assertEqual(outputs[0], outputs[1])

    def test_changed_source_needs_review(self):
        self.add("a.c", "int a(void){return 1;}\n")
        (self.root / "psp/src/a.c").write_text("int a(void){return 2;}\n")
        with self.assertRaisesRegex(source_data.SourceDataError, "Reviewed source changed"):
            source_data.stage(self.root, self.base / "out", self.manifest)

    def test_changed_private_data_is_not_silently_replaced(self):
        self.add("a.inc", "const int a[]={1,2};\n", "game_data")
        (self.root / "psp/src/a.inc").write_text("const int a[]={1,3};\n")
        with self.assertRaisesRegex(source_data.SourceDataError, "Reviewed source changed"):
            source_data.materialize_private_inputs(self.root, self.base / "private", self.manifest)

    def test_unresolved_provenance_is_a_hard_export_blocker(self):
        self.add("unknown.h", "int mystery;\n", "unresolved")
        with self.assertRaisesRegex(source_data.SourceDataError, "Unresolved provenance"):
            source_data.stage(self.root, self.base / "out", self.manifest)

    def test_dictionary_manifest_is_validated_too(self):
        self.add("a.h", "int a;\n")
        bad = copy.deepcopy(self.manifest)
        bad["files"][0]["path"] = "../a.h"
        with self.assertRaisesRegex(source_data.SourceDataError, "Unsafe relative path"):
            source_data.plan(self.root, bad)
        bad = copy.deepcopy(self.manifest)
        bad["files"].append(bad["files"][0])
        with self.assertRaisesRegex(source_data.SourceDataError, "Duplicate"):
            source_data.plan(self.root, bad)

    def test_rejects_symlink_source_even_if_target_is_inside_checkout(self):
        self.add("a.h", "int a;\n")
        (self.root / "psp/src/b.h").symlink_to("a.h")
        report = source_data.plan(self.root, self.manifest)
        self.assertFalse(report["provenance_ready"])
        self.assertTrue(any("symlink" in p.lower() for p in report["blockers"]))

    def test_does_not_merge_into_existing_output(self):
        self.add("a.h", "int a;\n")
        out = self.base / "out"
        out.mkdir()
        (out / "preserve").write_text("preserve")
        with self.assertRaisesRegex(source_data.SourceDataError, "Output must be new"):
            source_data.stage(self.root, out, self.manifest)
        self.assertEqual((out / "preserve").read_text(), "preserve")

    def test_duplicate_outputs_and_overlap_are_rejected(self):
        a = self.add("a.inc", "int a;\n", "game_data")
        b = self.add("b.inc", "int b;\n", "game_data")
        b["output"] = a["output"]
        with self.assertRaisesRegex(source_data.SourceDataError, "Duplicate"):
            source_data.plan(self.root, self.manifest)
        self.manifest["files"].clear()
        row = self.add("c.c", "const int values[]={1,2};\n", "mixed_implementation",
                       [("values", "const int values[]={1,2};")])
        duplicate = copy.deepcopy(row["fragments"][0])
        duplicate["output"] += "2"
        row["fragments"].append(duplicate)
        report = source_data.plan(self.root, self.manifest)
        self.assertTrue(any("overlapping" in p for p in report["blockers"]))

    def test_translated_program_is_not_mislabeled_as_data(self):
        self.add("program.inc", "int translated(int a){return a*5;}\n", "translated_original_program")
        report = source_data.plan(self.root, self.manifest)
        self.assertEqual(report["generated"][0]["kind"], "translated_original_program")
        self.assertFalse(report["build_ready"])
        self.assertFalse(report["public"])

    def test_metadata_is_regenerated_not_copied(self):
        self.add("crash_build.inc", '#define BUILD "old"\n', "build_metadata")
        (self.root / "psp/src/crash_build.inc").write_text('#define BUILD "new"\n')
        out = self.base / "out"
        report = source_data.stage(self.root, out, self.manifest)
        self.assertTrue(report["provenance_ready"])
        self.assertFalse((out / "psp/src/crash_build.inc").exists())

    def test_mixed_declarations_keep_c_scope_and_behavior(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        local = 'static const char *label[]={"a; b", "/* text */"};'
        outer = "static const int values[2][2]={{2,3},{5,7}};"
        program = ('/* Unicode before offsets: café */\n#include <stdio.h>\n' + outer +
                   '\nint main(void){' + local +
                   'printf("%d:%s:%s\\n",values[0][1]+values[1][0],label[0],label[1]);return 0;}\n')
        self.add("engine.c", program, "mixed_implementation", [("values", outer), ("label", local)])
        public, private = self.base / "public", self.base / "private"
        source_data.stage(self.root, public, self.manifest)
        source_data.materialize_private_inputs(self.root, private, self.manifest)
        for path in (private / source_data.GENERATED_ROOT).iterdir():
            target = public / source_data.GENERATED_ROOT / path.name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(path.read_bytes())
        for directory, name in ((self.root, "before"), (public, "after")):
            subprocess.run([compiler, "-std=c99", "-Wall", "-Werror", str(directory / "psp/src/engine.c"),
                            "-o", str(self.base / name)], check=True, capture_output=True)
        before = subprocess.check_output([self.base / "before"])
        after = subprocess.check_output([self.base / "after"])
        self.assertEqual(before, b"8:a; b:/* text */\n")
        self.assertEqual(before, after)
        metadata = json.loads((private / "PRIVATE-GENERATED-INPUTS.json").read_text())
        self.assertTrue(metadata["not_for_public_distribution"])
        self.assertFalse(metadata["regenerated_from_disc"])


if __name__ == "__main__":
    unittest.main()
