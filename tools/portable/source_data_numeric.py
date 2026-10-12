#!/usr/bin/env python3
"""Convert reviewed numeric generated include groups using the PSP compiler.

Run after user-disc generators have produced the .inc files in --source-dir.
source/ holds only BSS ABI declarations and loaders; PRIVATE-assets/ holds the
original data blobs to install locally. Missing groups are errors, not stubs.
"""
import argparse
import json
import hashlib
import re
from pathlib import Path
import tempfile
import source_data_sections

RECIPES = Path(__file__).with_name("source_data_numeric.json")


def generate(source_dir, out, compiler, recipes=RECIPES):
    source_dir, out = Path(source_dir), Path(out).absolute()
    document = json.loads(Path(recipes).read_text())
    if document.get("schema") != 1: raise ValueError("Unknown numeric source-data recipe")
    if out.exists() or out.is_symlink() or any(p.is_symlink() for p in out.parents):
        raise ValueError("Numeric output must be a new directory without linked parents")
    missing = [row["input"] for row in document["groups"] if not (source_dir / row["input"]).is_file()]
    if missing: raise ValueError("Missing generated disc inputs: " + ", ".join(missing))
    out.parent.mkdir(parents=True, exist_ok=True)
    reports = []
    with tempfile.TemporaryDirectory(prefix=".numeric-assets-", dir=out.parent) as temp:
        tree = Path(temp)/"tree"; public = tree/"source/psp/src"; private = tree/"PRIVATE-assets"
        public.mkdir(parents=True); private.mkdir()
        calls = ['#include "source_data_numeric.h"', '#if JGR_PORTABLE_ASSETS']
        for row in document["groups"]:
            original = (source_dir/row["input"]).read_text()
            text = original
            definitions = []
            if row.get("strip_directives"):
                text = re.sub(r"(?m)^[ \t]*#[^\n]*\n?", "", text)
            for name, value in row.get("defines", {}).items():
                if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name) or type(value) is not int or value < 1:
                    raise ValueError("Invalid reviewed array-bound metadata")
                text = re.sub(r"\[\s*" + re.escape(name) + r"\s*\]", "[" + str(value) + "]", text)
                definitions.append(f"#define {name} {value}\n")
            result = source_data_sections.generate(text, row["preamble"],
                                                   compiler, [], row["name"])
            include = "source_data_" + row["name"] + ".inc"
            result["source"][include] = "".join(definitions).encode() + result["source"][include]
            for name, data in result["source"].items(): (public/name).write_bytes(data)
            (private/("source_data_"+row["name"]+".jsd")).write_bytes(result["blob"])
            calls.append(f'int source_data_{row["name"]}_load(void);')
            reports.append(result["manifest"] | {"input": row["input"], "input_sha256": hashlib.sha256(original.encode()).hexdigest(),
                                                "layout": result["layout"]})
        calls.append("int source_data_numeric_load(void)\n{")
        for row in document["groups"]:
            calls.append(f'    if(source_data_{row["name"]}_load()<0)return -1;')
        calls += ["    return 0;", "}", "#endif"]
        (public/"source_data_numeric.c").write_text("\n".join(calls)+"\n")
        (public/"source_data_numeric.h").write_text("#ifndef JGR_SOURCE_DATA_NUMERIC_H\n#define JGR_SOURCE_DATA_NUMERIC_H\nint source_data_numeric_load(void);\n#endif\n")
        receipt = {"schema": 1, "groups": reports, "contains_private_game_data": True,
                   "source_is_payload_free": True, "upstream_disc_provenance": "requires a successful calling extraction pipeline"}
        (tree/"receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")
        tree.rename(out)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    try:
        result = generate(args.source_dir, args.out, args.cc)
        print(f"Generated {len(result['groups'])} numeric asset blobs and payload-free BSS interfaces")
    except (ValueError, OSError) as exc:
        parser.exit(1, f"source-data-numeric: {exc}\n")


if __name__ == "__main__": main()
