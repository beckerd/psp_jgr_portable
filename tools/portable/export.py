#!/usr/bin/env python3
"""Create a new source preparation directory without copying Git history/assets.

This is source preparation, not a release approval. An optional build audit
must match the exported source before its embedded-data gate can be cleared.
Disc conversion, licensing and physical PSP validation remain separate gates.
Local baseline inputs are never materialized here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import re

import source_data
import public_review
import export_inputs
from audit_build import source_binding, split_build, validate_split_metadata

ROOT = Path(__file__).resolve().parents[2]
BUILD_AUDIT_SCOPE = 'Local compiled-data audit of these public source/build inputs; artifact and asset-identity hashes refer to the audited local build only, not a new build, disc, license or hardware certification'


def copy_file(inputs, tree, name, destination=None):
    """Copy only the already verified snapshot, including Lua rewrite input."""
    data = inputs[name]
    if name.startswith('decompile/tools/') and name.endswith('.lua'):
        from converter_sources import exported_lua
        data = exported_lua(data.decode('utf-8')).encode('utf-8')
    target = tree / (destination or name)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def verified_build_audit(path, staged_build):
    """Accept only a passing local audit bound to this exact source export."""
    data = Path(path).read_bytes()
    report = json.loads(data)
    if not isinstance(report, dict) or type(report.get('schema')) is not int or report['schema'] != 1 or report.get('passed') is not True:
        raise ValueError('Build audit did not pass or has an unsupported schema')
    if report.get('active_original_data_includes') != []:
        raise ValueError('Build audit contains active original-data includes')
    for field in ('elf_sha256', 'eboot_sha256', 'asset_identity_sha256'):
        if not isinstance(report.get(field), str) or not re.fullmatch(r'[0-9a-f]{64}', report[field]):
            raise ValueError('Build audit lacks an exact artifact identity: ' + field)
    for field, minimum in (('external_asset_symbols', 10), ('external_asset_bytes', 1),
                           ('compiler_dependency_files', 1)):
        if type(report.get(field)) is not int or report[field] < minimum:
            raise ValueError('Build audit lacks compiled-storage evidence: ' + field)
    binding = source_binding(staged_build)
    if report.get('source_binding') != binding:
        raise ValueError('Build audit source does not match this export; rebuild and re-audit the current source')
    extra = {}
    if split_build(staged_build) or 'split_executable' in report:
        split = validate_split_metadata(report.get('split_executable'))
        if report['compiler_dependency_files'] != split['setup_dependency_files'] + split['game_dependency_files']:
            raise ValueError('Split dependency count does not match compiled audit')
        extra['split_executable'] = split
    return {key: report[key] for key in ('elf_sha256', 'eboot_sha256', 'source_binding', 'asset_identity_sha256',
        'external_asset_symbols', 'external_asset_bytes', 'compiler_dependency_files')} | {
        'audit_sha256': hashlib.sha256(data).hexdigest(),
        'active_original_data_includes': [],
        'scope': BUILD_AUDIT_SCOPE} | extra


def export(root, destination, build_audit=None, require_public_clean=True):
    root, destination = root.resolve(), destination.absolute()
    inputs = export_inputs.snapshot(root)
    reviewed_tool_inputs = len(inputs) - 1  # the inventory cannot hash itself
    if destination.exists() or destination.is_symlink():
        raise ValueError('Export destination must be new')
    if any(p.is_symlink() for p in destination.parents):
        raise ValueError('Export parents must not be symlinks')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.portable-export-', dir=destination.parent) as temp:
        tree = Path(temp) / 'source'
        manifest = json.loads(inputs['tools/portable/source_data_manifest.json'])
        report = source_data.stage(root, tree, manifest)
        # The selection and all copy bytes come from the same checked snapshot;
        # a file added or changed after verification cannot enter this export.
        for name in sorted(inputs):
            destination_name = 'psp/Makefile' if name == 'psp/Makefile.portable' else name
            copy_file(inputs, tree, name, destination_name)
        (tree / '.gitignore').write_text('/build/\n/psp/src/generated/\n/psp/src/portable_asset_identity.h\n'
            '/psp/src/crash_build.inc\n/psp/src/*.o\n/psp/src/*.d\n/psp/src/*.su\n'
            '/psp/.portable-config\n/psp/EBOOT.PBP\n/psp/PARAM.SFO\n/psp/*.elf\n/psp/*.prx\n'
            '/psp/.setup/\n/psp/game-preview.PBP\n/psp/portable_helpers.psar\n'
            '/psp/install.recipe\n/psp/install-coverage.json\n/psp/runtime-notices.txt\n'
            '/psp/*.jsd\n/psp/data/\n'
            '/decompile/ROM/\n/decompile/extracted/\n/decompile/afs/\n__pycache__/\n')
        try:
            content_review = public_review.review(tree)
        except ValueError as exc:
            if require_public_clean:
                raise
            # Keep malformed/incomplete review policy inspectable in private
            # candidates. This fallback remains an explicitly failed review.
            content_review = public_review.review(tree, dict(schema=1, findings=[],
                whole_tree_review_complete=False, review_note='Invalid public policy: ' + str(exc)))
        if require_public_clean:
            public_review.require_clean(content_review)
        (tree / 'PUBLIC-CONTENT-REVIEW.json').write_text(json.dumps(content_review, indent=2) + '\n')
        audit = verified_build_audit(build_audit, tree/'psp') if build_audit is not None else None
        rows = []
        for path in sorted(tree.rglob('*')):
            if path.is_file():
                data = path.read_bytes()
                rows.append({'path': path.relative_to(tree).as_posix(), 'bytes': len(data),
                             'sha256': hashlib.sha256(data).hexdigest()})
        gates = ['complete disc conversion', 'third-party and code license choice/review', 'PSP hardware validation']
        if not content_review['passed']:
            gates.insert(0, 'public source/content review')
        if audit is None:
            gates.insert(int(not content_review['passed']), 'compiled embedded-data separation audit')
        receipt = {'schema': 1, 'contains_git_history': False, 'distribution_ready': False,
                   'runtime_source_provenance_ready': report['provenance_ready'],
                   'source_only_build_ready': report['build_ready'],
                   'original_input_inventory_count': len(report['generated']),
                   'portable_compile_original_input_count': sum(bool(r['required_for_portable_build']) for r in report['generated']),
                   'compiled_data_separation_verified': audit is not None,
                   'public_content_review_passed': content_review['passed'],
                   'reviewed_tool_build_and_metadata_inputs': reviewed_tool_inputs,
                   'known_public_content_blockers': content_review['blockers'],
                   'build_audit': audit,
                   'remaining_gates': gates,
                   'files': rows}
        (tree / 'EXPORT-REVIEW.json').write_text(json.dumps(receipt, indent=2) + '\n')
        tree.rename(destination)
        return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--build-audit', type=Path, help='Passing audit_build.py JSON bound to this exact clean source/build tree')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--require-public-clean', action='store_true', help='Default: refuse export while original-content findings or whole-tree review remain open')
    mode.add_argument('--private-candidate', action='store_true', help='Create a private test/review tree with unresolved content findings; not for publication')
    args = parser.parse_args()
    try:
        report = export(args.root, args.out, args.build_audit, not args.private_candidate)
    except (ValueError, OSError) as exc:
        parser.exit(1, str(exc) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'files'}, indent=2))


if __name__ == '__main__':
    main()
