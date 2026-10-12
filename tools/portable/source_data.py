#!/usr/bin/env python3
"""Separate reviewed runtime source from locally generated original inputs.

This is an engineering provenance boundary, not a copyright determination.
The manifest is an explicit, content-addressed review of psp/src. Unknown or
changed source is never silently admitted. No original data is stored in the
manifest, and the public stage never contains missing-data stand-ins.

``plan(root)`` is read-only. ``stage(root, out)`` creates a new source-only tree.
``materialize_private_inputs`` is ONLY a migration/round-trip facility for an
existing private checkout; it is not the user-disc regeneration pipeline.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tempfile
import source_data_reverb

SCHEMA = 1
MANIFEST = Path(__file__).with_name("source_data_manifest.json")
SOURCE_SUFFIXES = {".c", ".h", ".inc", ".cc", ".cpp", ".cxx", ".hxx", ".hpp", ".hh", ".s", ".asm"}
PUBLIC = {"portable_implementation", "mixed_implementation", "port_numeric_calibration",
          "translated_implementation", "third_party_implementation"}
PRIVATE = {"game_data", "translated_original_program", "third_party_numeric_data", "private_backup"}
KINDS = PUBLIC | PRIVATE | {"build_metadata", "unresolved"}
GENERATED_ROOT = "psp/src/generated/source_data"


class SourceDataError(ValueError):
    """Review or transformation precondition failed; nothing may be published."""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def relative_path(value: str) -> PurePosixPath:
    if not isinstance(value, str) or not value or "\\" in value:
        raise SourceDataError(f"Invalid relative path: {value!r}")
    path = PurePosixPath(value)
    if path.is_absolute() or any(p in ("", ".", "..") for p in value.split("/")):
        raise SourceDataError(f"Unsafe relative path: {value!r}")
    return path


def contained(root: Path, relative: str) -> Path:
    path = root.joinpath(*relative_path(relative).parts)
    # Reject links at every component, including a link that resolves inside root.
    current = root
    for part in relative_path(relative).parts:
        current = current / part
        if current.is_symlink():
            raise SourceDataError(f"Symlink is not a reviewed source: {relative}")
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError as exc:
        raise SourceDataError(f"Path leaves source root: {relative}") from exc
    return path


def load_manifest(path: Path | str | None = None) -> dict:
    manifest = json.loads(Path(path or MANIFEST).read_text())
    return validate_manifest(manifest)


def validate_manifest(manifest: dict) -> dict:
    if manifest.get("schema") != SCHEMA or not isinstance(manifest.get("files"), list):
        raise SourceDataError("Unsupported source-data manifest")
    seen = set()
    outputs = set()
    for row in manifest["files"]:
        name = str(relative_path(row["path"]))
        if not name.startswith("psp/src/") or name in seen:
            raise SourceDataError(f"Duplicate or out-of-scope source: {name}")
        seen.add(name)
        if row.get("kind") not in KINDS:
            raise SourceDataError(f"Unknown provenance kind for {name}")
        if not re.fullmatch(r"[0-9a-f]{64}", row.get("sha256", "")):
            raise SourceDataError(f"Missing exact source hash: {name}")
        if not row.get("review"):
            raise SourceDataError(f"Missing provenance rationale: {name}")
        for fragment in row.get("fragments", []):
            output = str(relative_path(fragment["output"]))
            if not output.startswith(GENERATED_ROOT + "/") or output in outputs:
                raise SourceDataError(f"Duplicate or unsafe generated output: {output}")
            outputs.add(output)
            if not re.fullmatch(r"[0-9a-f]{64}", fragment.get("sha256", "")):
                raise SourceDataError(f"Missing fragment hash: {output}")
            if not fragment.get("provenance"):
                raise SourceDataError(f"Missing fragment provenance: {output}")
        if row.get("fragments") and row["kind"] != "mixed_implementation":
            raise SourceDataError(f"Fragments on non-mixed source: {name}")
        if row.get("parameter_split"):
            split = row["parameter_split"]
            output = str(relative_path(split["output"]))
            if row["kind"] != "mixed_implementation" or split.get("kind") != "reverb_parameters_v1":
                raise SourceDataError(f"Unknown parameter splitting transform: {name}")
            if not output.startswith(GENERATED_ROOT + "/") or output in outputs:
                raise SourceDataError(f"Duplicate or unsafe generated output: {output}")
            if not re.fullmatch(r"[0-9a-f]{64}", split.get("sha256", "")) or not split.get("provenance"):
                raise SourceDataError(f"Missing parameter provenance/hash: {name}")
            outputs.add(output)
        if row["kind"] in PRIVATE - {"private_backup"}:
            output = str(relative_path(row["output"]))
            if not output.startswith(GENERATED_ROOT + "/") or output in outputs:
                raise SourceDataError(f"Duplicate or unsafe generated output: {output}")
            outputs.add(output)
    return manifest


def source_files(root: Path) -> set[str]:
    base = contained(root, "psp/src")
    files = set()
    for path in base.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if relative.startswith(GENERATED_ROOT + "/"):
            continue
        if path.is_symlink():
            raise SourceDataError(f"Unreviewed symlink in runtime source: {relative}")
        if path.is_file() and (path.suffix.lower() in SOURCE_SUFFIXES or path.suffix == ".bak"):
            files.add(relative)
    return files


def _fragments(row: dict, data: bytes) -> list[tuple[int, int, dict]]:
    result = []
    previous = 0
    for fragment in sorted(row.get("fragments", []), key=lambda f: f["start"]):
        start, end = fragment["start"], fragment["end"]
        if type(start) is not int or type(end) is not int or not (previous <= start < end <= len(data)):
            raise SourceDataError(f"Invalid/overlapping fragment bounds: {row['path']}")
        if digest(data[start:end]) != fragment["sha256"]:
            raise SourceDataError(f"Changed embedded declaration: {fragment['output']}")
        result.append((start, end, fragment))
        previous = end
    return result


def _split_parameters(row: dict, data: bytes) -> tuple[bytes, bytes | None]:
    spec = row.get("parameter_split")
    if not spec:
        return data, None
    include = str(PurePosixPath(spec["output"]).relative_to("psp/src"))
    try:
        code, parameters = source_data_reverb.split(data, include)
    except ValueError as exc:
        raise SourceDataError(f"Unrecognized preset declarations: {row['path']}: {exc}") from exc
    if digest(parameters) != spec["sha256"]:
        raise SourceDataError(f"Changed generated preset data: {spec['output']}")
    return code, parameters


def plan(root: Path | str, manifest: Path | str | dict | None = None) -> dict:
    """Return public files, generated requirements, and actionable blockers.

    Provenance and portable compilation requirements are separate checks.
    ``build_ready`` covers required generated source inputs only; it never
    certifies a complete runtime asset pack or a successful compiler invocation.
    """
    root = Path(root).resolve()
    manifest = validate_manifest(manifest) if isinstance(manifest, dict) else load_manifest(manifest)
    rows = {row["path"]: row for row in manifest["files"]}
    blockers = []
    try:
        actual = source_files(root)
    except SourceDataError as exc:
        actual = set()
        blockers.append(str(exc))
    for name in sorted(actual - rows.keys()):
        blockers.append(f"Unreviewed runtime source: {name}; add an explicit reviewed manifest entry")
    for name in sorted(rows.keys() - actual):
        blockers.append(f"Reviewed source missing: {name}")
    generated, public, private = [], [], []
    for name, row in sorted(rows.items()):
        try:
            data = contained(root, name).read_bytes()
            if row["kind"] != "build_metadata" and digest(data) != row["sha256"]:
                raise SourceDataError(f"Reviewed source changed: {name}; re-review before updating its hash")
            fragments = _fragments(row, data)
            _split_parameters(row, data)
        except (OSError, SourceDataError) as exc:
            blockers.append(str(exc))
            continue
        if row["kind"] == "unresolved":
            blockers.append(f"Unresolved provenance: {name}: {row['review']}")
        elif row["kind"] in PUBLIC:
            public.append(name)
        else:
            private.append(name)
        if row["kind"] in PRIVATE - {"private_backup"}:
            generated.append({"source": name, "output": row["output"], "kind": row["kind"],
                              "sha256": row["sha256"], "generator": row.get("generator"),
                              "regeneration_status": row.get("regeneration_status", "unverified"),
                              "required_for_portable_build": row.get("required_for_portable_build", True),
                              "provenance": row["review"]})
        for start, end, fragment in fragments:
            generated.append({"source": name, "output": fragment["output"],
                              "kind": "embedded_game_data", "sha256": fragment["sha256"],
                              "symbol": fragment["symbol"], "source_line": data[:start].count(b"\n")+1,
                              "generator": fragment.get("generator"),
                              "regeneration_status": fragment.get("regeneration_status", "missing"),
                              "required_for_portable_build": fragment.get("required_for_portable_build", True),
                              "provenance": fragment["provenance"]})
        if row.get("parameter_split"):
            spec = row["parameter_split"]
            generated.append({"source": name, "output": spec["output"], "kind": "embedded_game_data",
                              "sha256": spec["sha256"], "generator": spec.get("generator"),
                              "regeneration_status": spec.get("regeneration_status", "unverified"),
                              "required_for_portable_build": spec.get("required_for_portable_build", True),
                              "provenance": spec["provenance"]})
    gaps = [{"output": row["output"], "source": row["source"], "generator": row["generator"],
             "status": row["regeneration_status"]}
            for row in generated if row.get("required_for_portable_build", True) and row["regeneration_status"] != "verified_from_disc"]
    return {"schema": SCHEMA, "manifest_sha256": digest(json.dumps(manifest, sort_keys=True).encode()),
            "provenance_ready": not blockers, "build_ready": not blockers and not gaps,
            "public": public, "excluded": private, "generated": generated,
            "regeneration_gaps": gaps, "blockers": blockers,
            "counts": dict(Counter(row["kind"] for row in rows.values()))}


def transform(data: bytes, row: dict, include_map: dict[str, str]) -> bytes:
    """Replace exact reviewed declarations in-place; keep their lexical scope."""
    if digest(data) != row["sha256"]:
        raise SourceDataError(f"Source hash changed before transformation: {row['path']}")
    chunks, previous = [], 0
    for start, end, fragment in _fragments(row, data):
        chunks.append(data[previous:start])
        relative = PurePosixPath(fragment["output"]).relative_to("psp/src").as_posix()
        # Directives must start on their own line, including function-local data.
        chunks.append(f'\n#include "{relative}"\n'.encode())
        previous = end
    chunks.append(data[previous:])
    result = b"".join(chunks)
    result, _ = _split_parameters(row, result)
    include = re.compile(rb'(^[ \t]*#[ \t]*include[ \t]+")([^"\r\n]+)(")', re.M)
    def replace(match):
        name = match[2].decode("utf-8")
        return match[1] + include_map.get(name, name).encode() + match[3]
    return include.sub(replace, result)


def _new_directory(out: Path) -> None:
    if out.exists() or out.is_symlink():
        raise SourceDataError(f"Output must be new: {out}")
    if any(parent.is_symlink() for parent in out.parents):
        raise SourceDataError(f"Output parent contains a symlink: {out}")
    out.parent.mkdir(parents=True, exist_ok=True)


def stage(root: Path | str, out: Path | str, manifest: Path | str | dict | None = None) -> dict:
    """Stage reviewed transformations; public_review checks broader findings.

    The known-data inventory alone does not certify absence of other payloads.
    """
    root, out = Path(root).resolve(), Path(out).absolute()
    document = manifest if isinstance(manifest, dict) else load_manifest(manifest)
    report = plan(root, document)
    if report["blockers"]:
        raise SourceDataError("Source export refused:\n" + "\n".join(report["blockers"]))
    _new_directory(out)
    include_map = {PurePosixPath(row["path"]).name: str(PurePosixPath(row["output"]).relative_to("psp/src"))
                   for row in document["files"] if row["kind"] in PRIVATE - {"private_backup"}}
    rows = {row["path"]: row for row in document["files"]}
    # A sibling temporary tree makes partial output impossible after a failure.
    with tempfile.TemporaryDirectory(prefix=".source-data-stage-", dir=out.parent) as temp:
        tree = Path(temp) / "tree"
        tree.mkdir()
        for name in report["public"]:
            row = rows[name]
            data = transform(contained(root, name).read_bytes(), row, include_map)
            target = tree / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (tree / "generated-inputs.json").write_text(json.dumps(report, indent=2, sort_keys=True)+"\n")
        # The generated directory intentionally has no placeholder source files.
        tree.rename(out)
    return report


def materialize_private_inputs(root: Path | str, out: Path | str,
                               manifest: Path | str | dict | None = None) -> dict:
    """Copy reviewed private inputs to a NEW LOCAL migration directory.

    These files contain original game data/programs. This operation neither
    certifies regeneration from disc nor belongs in a public release archive.
    """
    root, out = Path(root).resolve(), Path(out).absolute()
    document = manifest if isinstance(manifest, dict) else load_manifest(manifest)
    report = plan(root, document)
    if report["blockers"]:
        raise SourceDataError("Private migration refused:\n"+"\n".join(report["blockers"]))
    _new_directory(out)
    with tempfile.TemporaryDirectory(prefix=".source-data-private-", dir=out.parent) as temp:
        tree = Path(temp)/"tree"
        tree.mkdir()
        for row in document["files"]:
            data = contained(root, row["path"]).read_bytes()
            payloads = [(f["output"], data[start:end]) for start, end, f in _fragments(row, data)]
            if row.get("parameter_split"):
                _, parameters = _split_parameters(row, data)
                payloads.append((row["parameter_split"]["output"], parameters))
            if row["kind"] in PRIVATE - {"private_backup"}:
                if digest(data) != row["sha256"]:
                    raise SourceDataError(f"Private source changed: {row['path']}")
                payloads.append((row["output"], data))
            for name, payload in payloads:
                target = tree/name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(payload)
        (tree/"PRIVATE-GENERATED-INPUTS.json").write_text(json.dumps({
            "contains_original_game_content": True, "not_for_public_distribution": True,
            "regenerated_from_disc": False, "generated": report["generated"]}, indent=2)+"\n")
        tree.rename(out)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--stage", type=Path, help="NEW source-only output directory")
    actions.add_argument("--private-generated-out", type=Path, help="NEW LOCAL migration directory; contains original content")
    parser.add_argument("--json", action="store_true", help="Print the data-free review/generation plan")
    args = parser.parse_args()
    try:
        if args.stage:
            report = stage(args.root, args.stage, args.manifest)
        elif args.private_generated_out:
            report = materialize_private_inputs(args.root, args.private_generated_out, args.manifest)
        else:
            report = plan(args.root, args.manifest)
        if args.json:
            print(json.dumps(report, indent=2, sort_keys=True))
        else:
            print(f"Reviewed public source: {len(report['public'])}; generated inputs: {len(report['generated'])}")
            print(f"Provenance ready: {report['provenance_ready']}; disc-only build ready: {report['build_ready']}")
            for problem in report["blockers"]:
                print(problem)
            if report["regeneration_gaps"]:
                print(f"Unverified/missing disc regeneration: {len(report['regeneration_gaps'])}; inspect --json")
        return 1 if report["blockers"] else 0
    except (SourceDataError, OSError, json.JSONDecodeError) as exc:
        parser.exit(1, f"source-data: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
