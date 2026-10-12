#!/usr/bin/env python3
"""Audit a working checkout through its exact reviewed source inventory.

Build outputs and Git history stay outside the temporary source snapshot.
An optional --out retains that snapshot; it must name a new directory.
This does not refresh policy or approve changed source.
Git and Python 3 are required, including before the first git init.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

import public_review

ROOT = Path(__file__).resolve().parents[2]


def git(args, *, cwd=None):
    # Do not inherit an outer checkout, alternate index, or object directory.
    env = {key: value for key, value in os.environ.items() if not key.startswith('GIT_')}
    env['GIT_OPTIONAL_LOCKS'] = '0'
    result = subprocess.run(['git', *args], cwd=cwd, env=env, check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return result.stdout


def source_candidates(root):
    """Include tracked ignored files as well as normal git-add candidates.

    A temporary empty index also works before initialization. Only repository
    .gitignore files control untracked exclusions, not personal/global rules.
    """
    with tempfile.TemporaryDirectory(prefix='jgr-source-index-') as temporary:
        index = Path(temporary) / 'index.git'
        git(['init', '--bare', '--quiet', '--template=', str(index)])
        common = ['--git-dir=' + str(index), '--work-tree=' + str(root)]
        data = git(common + ['ls-files', '-z', '--others', '--exclude-per-directory=.gitignore'])
        names = set(os.fsdecode(name) for name in data.split(b'\0') if name)
    if (root / '.git').exists() or (root / '.git').is_symlink():
        top = Path(os.fsdecode(git(['rev-parse', '--show-toplevel'], cwd=root)).strip()).resolve()
        if top != root:
            raise ValueError('Git working tree differs from source root')
        data = git(['ls-files', '-z', '--cached'], cwd=root)
        names.update(os.fsdecode(name) for name in data.split(b'\0') if name)
    return names


def source_file(root, name):
    public_review.safe_path(name)
    path = root
    for part in Path(name).parts:
        path = path / part
        if path.is_symlink():
            raise ValueError('Linked source input: ' + name)
    if not path.is_file():
        raise ValueError('Missing or nonregular source input: ' + name)
    return path


def checked_identity(root, path):
    """The sole optional generated input is reproducible checksum metadata."""
    import package
    from replay_identity import select_catalog
    path = Path(path)
    if path.is_symlink() or any(p.is_symlink() for p in path.absolute().parents) or not path.is_file():
        raise ValueError('Identity input must be a regular unlinked file')
    catalog = json.loads(source_file(root, 'tools/portable/asset_catalog.json').read_text())
    native = json.loads(source_file(root, 'tools/portable/native_replay_identity.json').read_text())
    catalog = select_catalog(catalog, 'native-v1', native, root=root)
    with tempfile.TemporaryDirectory(prefix='jgr-source-identity-') as temporary:
        expected = Path(temporary) / 'identity.h'
        package.write_identity(package.asset_manifest(catalog['files']), len(catalog['files']), expected)
        data = path.read_bytes()
        if data != expected.read_bytes():
            raise ValueError('Identity differs from native-v1 catalog metadata')
    return data


def capture(root, identity=None):
    """Read reviewed bytes once, never copy by suffix or a recursive glob."""
    policy_bytes = source_file(root, public_review.POLICY_PATH).read_bytes()
    policy = json.loads(policy_bytes)
    if not isinstance(policy, dict):
        raise ValueError('Invalid public review policy')
    rows = public_review.file_rows(policy.get('reviewed_files', []))
    if not rows or rows.keys() & public_review.EXCLUDED_METADATA:
        raise ValueError('Empty or self-referential reviewed source inventory')
    admitted = set(rows) | public_review.POLICY_FILES
    files = {public_review.POLICY_PATH: policy_bytes}
    identity_name = 'psp/src/portable_asset_identity.h'
    if identity is not None:
        if identity_name not in rows:
            raise ValueError('Generated identity must remain hash-bound in reviewed_files')
        files[identity_name] = checked_identity(root, identity)
    for name in sorted(admitted - files.keys()):
        files[name] = source_file(root, name).read_bytes()
    for name, data in files.items():
        if name in rows:
            row = rows[name]
            if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
                raise ValueError('Reviewed source changed: ' + name)
    extras = source_candidates(root) - admitted
    if extras:
        raise ValueError('Unreviewed tracked or nonignored files:\n' + '\n'.join(sorted(extras)))
    return files


def export_receipt(files, report):
    # Carry the reviewed generated-input inventory, not a fresh scan for
    # absent original includes. No compiled-build or release claim is made.
    generated = json.loads(files.get('generated-inputs.json', b'{}'))
    inputs = json.loads(files['tools/portable/public_sources.json'])
    rows = generated.get('generated', [])
    if not isinstance(rows, list) or any(not isinstance(row, dict) or
        type(row.get('required_for_portable_build')) is not bool for row in rows):
        raise ValueError('Invalid reviewed generated-input inventory')
    return dict(schema=1, contains_git_history=False, distribution_ready=False,
        runtime_source_provenance_ready=generated.get('provenance_ready') is True,
        source_only_build_ready=generated.get('build_ready') is True,
        original_input_inventory_count=len(rows),
        portable_compile_original_input_count=sum(row['required_for_portable_build'] for row in rows),
        compiled_data_separation_verified=False, public_content_review_passed=report['passed'],
        reviewed_tool_build_and_metadata_inputs=len(inputs['files']),
        known_public_content_blockers=report['blockers'], build_audit=None,
        remaining_gates=['compiled embedded-data separation audit', 'complete disc conversion',
                         'third-party and code license choice/review', 'PSP hardware validation'],
        files=[dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
               for name, data in sorted(files.items())])


def check(root=ROOT, out=None, identity=None):
    root = Path(root).absolute()
    if root.is_symlink() or not root.is_dir():
        raise ValueError('Source root must be an existing directory, not a link')
    root = root.resolve()
    if out is not None:
        out = Path(out).absolute()
        if out.exists() or out.is_symlink():
            raise ValueError('Snapshot destination must be new')
        if any(parent.is_symlink() for parent in out.parents):
            raise ValueError('Snapshot destination parents must not be links')
    files = capture(root, identity)
    if out is not None:
        out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.jgr-source-check-', dir=out.parent if out else None) as temporary:
        tree = Path(temporary) / 'source'
        tree.mkdir()
        for name, data in files.items():
            path = tree / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        report = public_review.review(tree)
        public_review.require_clean(report)
        # Record the current review only, never reuse an old export receipt.
        files['PUBLIC-CONTENT-REVIEW.json'] = (json.dumps(report, indent=2) + '\n').encode()
        (tree / 'PUBLIC-CONTENT-REVIEW.json').write_bytes(files['PUBLIC-CONTENT-REVIEW.json'])
        receipt = export_receipt(files, report)
        (tree / 'EXPORT-REVIEW.json').write_text(json.dumps(receipt, indent=2) + '\n')
        public_review.require_clean(public_review.review(tree))
        if out is not None:
            if out.exists() or out.is_symlink():
                raise ValueError('Snapshot destination appeared during review')
            tree.rename(out)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--out', type=Path, help='Retain a checked source-only snapshot in a new directory')
    parser.add_argument('--identity', type=Path,
                        help='Optional generated header, checked against native-v1 and the reviewed hash')
    args = parser.parse_args()
    try:
        report = check(args.root, args.out, args.identity)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, str(exc) + '\n')
    print(json.dumps({key: report[key] for key in
                     ('passed', 'policy', 'files', 'bytes', 'source_tree_sha256', 'scope')}, indent=2))


if __name__ == '__main__':
    main()
