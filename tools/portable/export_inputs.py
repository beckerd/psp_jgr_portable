"""Content-addressed boundary for exporter inputs outside psp/src.

Runtime transformations have their own source_data manifest. This inventory
covers tools, extraction recipes, tests, notices, build files and helpers.
Hashes preserve a review; they do not establish authorship by themselves.
"""
import hashlib
import json
from pathlib import Path
import re

MANIFEST = 'tools/portable/public_sources.json'
# These reviewed harnesses contain native test code and authored ISA fixtures.
# Observed RAM, disc bytes and converted outputs remain local test inputs.
# Other C files are not admitted by extension alone.
NATIVE_HARNESSES = {
    'psp/tests/replay_pack_sanitize.c',
    'tools/portable/install_probe.c',
    'tools/portable/replay/api_probe.c',
    'tools/portable/replay/bootstrap_probe.c',
    'tools/portable/replay/callback_probe.c',
    'tools/portable/replay/probe.c',
    'tools/portable/replay/session_probe.c',
    'tools/portable/replay/staff_probe.c',
    'tools/portable/replay/staff_pose_psp_probe.c',
    'tools/portable/replay/staff_reuse_api_probe.c',
    'tools/portable/replay/test_arm.c',
    'tools/portable/replay/test_maple.c',
    'tools/portable/replay/test_memory_loops.c',
    'tools/portable/replay/test_scanline.c',
    'tools/portable/replay/test_staff.c',
    'tools/portable/replay/test_staff_reuse.c',
    'tools/portable/replay/test_vm.c',
}
# Superseded experiments remain local. Public proofs use the bounded production
# API, and accept their generated observations only as explicit local inputs.
PRIVATE_PROTOTYPES = {
    'tools/portable/replay/build_native_probe.py',
    'tools/portable/replay/compare_native_rivals.py',
    'tools/portable/replay/check_native_body.py',
    'tools/portable/replay/check_native_rail.py',
    'tools/portable/replay/native_rival_trace.c',
    'tools/portable/replay/native_demo_probe.c',
    'tools/portable/replay/native_follow_adapter.h',
    'tools/portable/replay/native_solid_assets.py',
    'tools/portable/replay/run_native_demo_probe.py',
    'tools/portable/replay/native_staff_probe.c',
    'tools/portable/replay/native_staff_probe.py',
    'tools/portable/replay/native_staff_pose.c',
    'tools/portable/replay/native_staff_curve.c',
    'tools/portable/replay/native_staff_curve.h',
    'tools/portable/replay/native_staff_curve_test.c',
    'tools/portable/replay/native_staff_world.c',
    'tools/portable/replay/native_staff_world.py',
    'tools/portable/replay/native_staff_objects.c',
    'tools/portable/replay/native_staff_objects.h',
    'tools/portable/replay/native_staff_objects.py',
    'tools/portable/replay/native_world_bridge.c',
    'tools/portable/replay/remainder_cache_probe.inc',
    'tools/portable/replay/test_remainder_cache_private.c',
    'tools/portable/replay/staff_pose_probe.inc',
    'tools/portable/replay/staff_reuse_probe.c',
    'tools/portable/replay/staff_reuse_probe.inc',
    'tools/portable/replay/verify_staff_reuse.py',
}


def names(root):
    from converter_sources import select
    from pipeline import SPECS
    root = Path(root)
    result = {'psp/Makefile.portable', 'psp/crash_handler_license.txt',
              'decompile/tools/crash_build_id.py'}
    for directory in ('psp/tools/meprx', 'psp/tools/crashprx'):
        for path in (root / directory).iterdir():
            if path.suffix in {'.c', '.h', '.S', '.inc'} or path.name == 'Makefile':
                result.add(path.relative_to(root).as_posix())
    result.update('decompile/tools/' + name for name in select(root))
    result.update('decompile/' + name for name in SPECS)
    result.update(NATIVE_HARNESSES)
    for path in (root / 'tools/portable').rglob('*'):
        if path.is_file() and path.suffix in {'.py', '.lua', '.mjs', '.json', '.md', '.txt', '.patch'} and '__pycache__' not in path.parts:
            result.add(path.relative_to(root).as_posix())
    for name in ('README.md', 'THIRD_PARTY_NOTICES.md', 'requirements-portable.txt'):
        if (root / name).is_file():
            result.add(name)
    return sorted(result - {MANIFEST} - PRIVATE_PROTOTYPES)


def validate(policy):
    if not isinstance(policy, dict) or type(policy.get('schema')) is not int or policy['schema'] != 1 or not isinstance(policy.get('files'), list):
        raise ValueError('Invalid public export input inventory')
    if set(policy) - {'schema', 'files', 'review_note'}:
        raise ValueError('Unknown public export input inventory fields')
    if 'review_note' in policy and not isinstance(policy['review_note'], str):
        raise ValueError('Invalid public export input review note')
    expected = {}
    from source_data import relative_path
    for row in policy['files']:
        if not isinstance(row, dict) or set(row) != {'path', 'sha256', 'review'}:
            raise ValueError('Invalid public export input entry')
        name = str(relative_path(row['path']))
        if name == MANIFEST or name in expected or not isinstance(row['review'], str) or not row['review'].strip():
            raise ValueError('Duplicate/unreviewed public export input: ' + name)
        if not isinstance(row['sha256'], str) or not re.fullmatch('[0-9a-f]{64}', row['sha256']):
            raise ValueError('Invalid public export input hash: ' + name)
        expected[name] = row['sha256']
    return expected


def snapshot(root, policy=None):
    """Read each admitted input once; exported copies use these verified bytes.

    The manifest cannot hash itself. Its strictly validated original bytes are
    included so the file shipped is the same document used to check inputs.
    """
    from source_data import contained
    root = Path(root)
    if not root.is_dir() or root.is_symlink():
        raise ValueError('Export inputs require an existing unlinked directory')
    manifest_data = None
    if policy is None:
        manifest_data = contained(root, MANIFEST).read_bytes()
        policy = json.loads(manifest_data)
    expected = validate(policy)
    for name in expected:
        contained(root, name)
    actual = set(names(root))
    failures = ['Unreviewed public export input: ' + name for name in sorted(actual - expected.keys())]
    failures += ['Reviewed export input no longer selected: ' + name for name in sorted(expected.keys() - actual)]
    data = {}
    for name in sorted(actual & expected.keys()):
        data[name] = contained(root, name).read_bytes()
        if hashlib.sha256(data[name]).hexdigest() != expected[name]:
            failures.append('Reviewed public export input changed: ' + name)
    if failures:
        raise ValueError('\n'.join(failures))
    if manifest_data is not None:
        data[MANIFEST] = manifest_data
    return data


def verify(root, policy=None):
    return len(snapshot(root, policy)) - (1 if policy is None else 0)
