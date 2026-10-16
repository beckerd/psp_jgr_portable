#!/usr/bin/env python3
"""Check the compiled portable data boundary, not authorship or licensing.

Every reviewed external asset symbol must have zero-initialized BSS storage.
Compiler dependency files must not reference original generated includes.
Run against the unstripped ELF from a complete clean source-export build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile

from bundle import validate as validate_bundle, HEADER as BUNDLE_HEADER, ENTRY as BUNDLE_ENTRY
from build_device_setup import setup_sources

SOURCE_SUFFIXES = {'.c', '.h', '.inc', '.cc', '.cpp', '.cxx', '.hxx', '.hpp', '.hh', '.s', '.asm'}


def source_binding(build):
    """Bind a clean portable build to its source, ABI, and build instructions.

    The generated crash identifier is excluded because it is an output of the
    build. The generated asset identity is recorded separately in the audit;
    compatible packs share the same public source. Object, dependency, binary
    and private asset files are excluded. Run this only after a clean build.
    """
    build = Path(build)
    paths = [build / 'Makefile', build.parent / 'decompile/tools/crash_build_id.py']
    if split_build(build):
        paths.extend(build.parent / 'tools/portable' / name for name in ('build_device_setup.py', 'bundle.py'))
    for folder in ('src', 'tools/meprx', 'tools/crashprx'):
        base = build / folder
        if not base.is_dir() or base.is_symlink():
            raise ValueError('Missing or linked portable build source directory: ' + folder)
        for path in sorted(base.rglob('*')):
            if path.is_symlink():
                raise ValueError('Linked portable build input: ' + path.relative_to(build).as_posix())
            if path.is_file() and (path.suffix.lower() in SOURCE_SUFFIXES or path.name == 'Makefile'):
                if path.relative_to(build).as_posix() not in ('src/crash_build.inc','src/portable_asset_identity.h'):
                    paths.append(path)
    rows = []
    for path in sorted(paths):
        if not path.is_file() or path.is_symlink():
            raise ValueError('Missing or linked portable build input: ' + str(path))
        rows.append({'path': path.relative_to(build.parent).as_posix(),
                     'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    encoded = json.dumps(rows, sort_keys=True, separators=(',', ':')).encode('utf-8')
    return {'schema': 1, 'sha256': hashlib.sha256(encoded).hexdigest(), 'files': len(rows)}


def symbols(text):
    result = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 4:
            name, kind, address, size = fields[:4]
            result[name] = (kind, int(address, 16), int(size, 16))
    return result


def check_storage(expected, actual):
    missing = sorted(set(expected) - actual.keys())
    wrong = sorted(name for name in expected if name in actual and actual[name][0] not in ('B', 'b'))
    if missing or wrong:
        raise ValueError('Asset storage audit failed: missing=' + repr(missing) + '; not BSS=' + repr(wrong))
    return sum(actual[name][2] for name in expected)


def split_build(build):
    return 'build_device_setup.py' in (Path(build) / 'Makefile').read_text()


def digest(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError('Missing or linked audit input: ' + str(path))
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_dependencies(paths, private):
    if not paths:
        raise ValueError('Missing compiler dependency evidence')
    forbidden = set()
    for path in paths:
        if path.is_symlink():
            raise ValueError('Linked compiler dependency evidence')
        for dependency in shlex.split(path.read_text().replace('\\\n', ' ')):
            if 'generated/source_data/' in dependency or Path(dependency).name in private:
                forbidden.add(dependency)
    if forbidden:
        raise ValueError('Original data was compiled into the portable build: ' + repr(sorted(forbidden)))
    return len(paths)


def pbp_parts(data):
    if len(data) < 40 or data[:4] != b'\0PBP':
        raise ValueError('Invalid PSP executable container')
    offsets = struct.unpack_from('<8I', data, 8)
    if offsets[0] != 40 or list(offsets) != sorted(offsets) or offsets[-1] >= len(data):
        raise ValueError('Invalid PSP executable offsets')
    if len(set(offsets[1:7])) != 1:
        raise ValueError('Split executable contains unreviewed artwork or sound')
    return data[offsets[6]:offsets[7]], data[offsets[7]:]


def validate_split_metadata(row):
    hashes = {'setup_elf_sha256', 'setup_prx_sha256', 'setup_makefile_sha256',
              'game_prx_sha256', 'me_elf_sha256', 'crash_elf_sha256',
              'bundle_sha256', 'recipe_sha256', 'notice_sha256', 'receipt_sha256'}
    counts = {'setup_dependency_files', 'game_dependency_files', 'setup_source_files'}
    keys = hashes | counts | {'schema', 'bundle_roles', 'game_importer_symbols', 'setup_game_asset_symbols'}
    native_counts = {'setup_external_asset_symbols', 'setup_external_asset_bytes'}
    version = row.get('schema') if isinstance(row, dict) else None
    if version == 2:
        keys |= native_counts
    if (not isinstance(row, dict) or set(row) != keys or type(version) is not int or version not in (1, 2)
            or any(not isinstance(row[k], str) or not re.fullmatch('[0-9a-f]{64}', row[k]) for k in hashes)
            or any(type(row[k]) is not int or row[k] < 1 for k in counts)
            or row['bundle_roles'] != [1, 2, 3, 4, 5]
            or row['game_importer_symbols'] != [] or row['setup_game_asset_symbols'] != []):
        raise ValueError('Invalid split executable audit metadata')
    if version == 2 and any(type(row[k]) is not int or row[k] < 0 for k in native_counts):
        raise ValueError('Invalid native setup BSS counts')
    return row


def check_setup_storage(expected, setup_actual, allowed, game_actual):
    """Only the explicit native replay table subset may appear in setup BSS."""
    present = set(expected).intersection(setup_actual)
    unexpected = sorted(present - set(allowed))
    if unexpected:
        raise ValueError('Split executable memory boundary failed: setup=' + repr(unexpected))
    bytes_used = check_storage(present, setup_actual)
    wrong_size = sorted(name for name in present if name not in game_actual or
                        setup_actual[name][2] != game_actual[name][2])
    if wrong_size:
        raise ValueError('Setup external table ABI differs from gameplay: ' + repr(wrong_size))
    return len(present), bytes_used


def audit_split(build, nm, prxgen, expected, actual, private, game_dependencies):
    setup = build / '.setup'
    if setup.is_symlink():
        raise ValueError('Linked setup build directory')
    receipt_path = setup / 'build-receipt.json'
    receipt_hash = digest(receipt_path)
    receipt = json.loads(receipt_path.read_text())
    payload, packed = pbp_parts((build / 'EBOOT.PBP').read_bytes())
    bundle = validate_bundle(packed)
    if len(bundle['entries']) != 5 or receipt.get('bundle') != bundle:
        raise ValueError('Split build requires the exact five-role runtime bundle')
    if (receipt.get('schema') != 2 or receipt.get('architecture') != 'game-main-converter-module' or receipt.get('memory_model') != 'PSP-1000'
            or receipt.get('eboot_sha256') != digest(build / 'EBOOT.PBP')
            or receipt.get('setup_elf_sha256') != digest(setup / 'jgrsetup.elf')
            or receipt.get('setup_makefile_sha256') != digest(setup / 'Makefile')):
        raise ValueError('Split build receipt does not match compiled artifacts')
    _, names, table_sources = setup_sources(build)
    wanted = [{'path': 'psp/src/' + name, 'sha256': digest(build / 'src' / name)} for name in sorted(names)]
    rows = receipt.get('setup_sources')
    if (not isinstance(rows, list) or any(not isinstance(r, dict) or set(r) != {'path', 'sha256'}
            or not isinstance(r['path'], str) for r in rows) or sorted(rows, key=lambda r: r['path']) != wanted):
        raise ValueError('Setup source receipt does not match the current build source')
    for row in wanted:
        if digest(setup / Path(row['path']).name) != row['sha256']:
            raise ValueError('Setup compiled source copy differs: ' + row['path'])
    setup_actual = symbols(subprocess.check_output([nm, '--defined-only', '--format=posix', str(setup / 'jgrsetup.elf')], text=True))
    if not {'module_start', 'module_stop', '__library_exports'}.issubset(setup_actual):
        raise ValueError('Converter must export both lifecycle functions')
    if '__wrap___libcglue_init' not in actual:
        raise ValueError('Game must prepare data before reserving its heap')
    if any('LoadExecVSH' in name or name == 'sceKernelLoadExec' for name in actual | setup_actual):
        raise ValueError('Unexpected executable replacement in the game/module build')
    forbidden_game = sorted(name for name in actual if re.match(r'jgr_(?:disc_|install_|replay_|models_|native_)', name))
    if forbidden_game:
        raise ValueError('Split executable memory boundary failed: game=' + repr(forbidden_game))
    allowed = set()
    for name in table_sources:
        allowed.update(re.findall(r'__asm__\("(jgr_(?:data|ptr)_[A-Za-z_0-9]+)"\)',
                                 (build / 'src' / name).read_text()))
    native_count, native_bytes = check_setup_storage(expected, setup_actual, allowed, actual)
    if not {'jgr_disc_open', 'jgr_install_run', 'jgr_replay_session_generate'}.issubset(setup_actual):
        raise ValueError('Missing setup converter/replay implementation')
    count = check_dependencies(list(setup.glob('*.d')), private)
    entries = []
    for i in range(5):
        _, offset, size, _ = BUNDLE_ENTRY.unpack_from(packed, BUNDLE_HEADER.size + i * BUNDLE_ENTRY.size)
        entries.append(packed[offset:offset + size])
    paths = (build / 'jgr_me.prx', build / 'jgr_crash.prx', build / 'runtime-notices.txt', build / 'install.recipe', setup / 'jgrsetup.prx')
    for path, data in zip(paths, entries):
        if digest(path) != hashlib.sha256(data).hexdigest():
            raise ValueError('Bundled role differs from local build artifact: ' + path.name)
    if digest(build / 'jgrview.prx') != hashlib.sha256(payload).hexdigest():
        raise ValueError('EBOOT executable differs from the game PRX')
    if digest(build / 'portable_helpers.psar') != bundle['sha256']:
        raise ValueError('Packaged PSAR differs from built runtime bundle')
    # Recreate each module to bind the audited unstripped ELF to the exact
    # executable bytes shipped in DATA.PSP or the corresponding PSAR role.
    modules = ((build / 'jgrview.elf', payload), (setup / 'jgrsetup.elf', entries[4]),
               (build / 'tools/meprx/jgr_me.elf', entries[0]), (build / 'tools/crashprx/jgr_crash.elf', entries[1]))
    with tempfile.TemporaryDirectory(prefix='jgr-build-audit-') as temporary:
        output = Path(temporary) / 'audit.prx'
        for elf, data in modules:
            digest(elf)
            subprocess.run([prxgen, str(elf), str(output)], check=True, capture_output=True)
            if output.read_bytes() != data:
                raise ValueError('Packaged PRX does not derive from audited ELF: ' + elf.name)
    native_metadata = dict(setup_external_asset_symbols=native_count,
                           setup_external_asset_bytes=native_bytes) if table_sources else {}
    return validate_split_metadata(dict(schema=2 if table_sources else 1, setup_elf_sha256=digest(setup / 'jgrsetup.elf'),
        setup_prx_sha256=digest(setup / 'jgrsetup.prx'), setup_makefile_sha256=digest(setup / 'Makefile'),
        game_prx_sha256=digest(build / 'jgrview.prx'), me_elf_sha256=digest(build / 'tools/meprx/jgr_me.elf'),
        crash_elf_sha256=digest(build / 'tools/crashprx/jgr_crash.elf'), bundle_sha256=bundle['sha256'],
        recipe_sha256=bundle['entries'][3]['sha256'], notice_sha256=bundle['entries'][2]['sha256'],
        receipt_sha256=receipt_hash, setup_dependency_files=count, game_dependency_files=game_dependencies,
        setup_source_files=len(wanted), bundle_roles=[1, 2, 3, 4, 5],
        game_importer_symbols=[], setup_game_asset_symbols=[], **native_metadata))


def audit(build, nm, prxgen=None):
    root = build / 'src'
    expected = set()
    for path in root.glob('source_data_*.c'):
        expected.update(re.findall(r'__asm__\("(jgr_(?:data|ptr)_[A-Za-z_0-9]+)"\)', path.read_text()))
    expected.update('jgr_bubble_' + kind + str(i) for kind in ('v', 'i', 'b') for i in range(3))
    if len(expected) < 10:
        raise ValueError('Missing portable source-data interfaces')
    elf = build / 'jgrview.elf'
    actual = symbols(subprocess.check_output([nm, '--defined-only', '--format=posix', str(elf)], text=True))
    payload_bytes = check_storage(expected, actual)
    manifest = json.loads(Path(__file__).with_name('source_data_manifest.json').read_text())
    private = {Path(row['path']).name for row in manifest['files']
               if row['kind'] in ('game_data', 'third_party_numeric_data', 'translated_original_program')}
    dependencies = list(root.glob('*.d'))
    count = check_dependencies(dependencies, private)
    split = None
    if split_build(build):
        prxgen = prxgen or str(Path(nm).with_name('psp-prxgen'))
        split = audit_split(build, nm, prxgen, expected, actual, private, count)
        count += split['setup_dependency_files']
    report = dict(schema=1, passed=True, elf_sha256=digest(elf), eboot_sha256=digest(build/'EBOOT.PBP'),
                external_asset_symbols=len(expected), external_asset_bytes=payload_bytes,
                compiler_dependency_files=count, active_original_data_includes=[],
                source_binding=source_binding(build),
                asset_identity_sha256=digest(build/'src/portable_asset_identity.h'),
                scope='Reviewed external asset storage and active includes; not a licensing determination')
    if split is not None:
        report['split_executable'] = split
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--nm', default='psp-nm')
    parser.add_argument('--prxgen', help='PSP PRX generator; defaults to the tool next to --nm')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        report = audit(args.build.resolve(), args.nm, args.prxgen)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, indent=2))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error)+'\n')


if __name__ == '__main__':
    main()
