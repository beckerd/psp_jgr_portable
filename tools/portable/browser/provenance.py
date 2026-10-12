#!/usr/bin/env python3
"""Audit browser build inputs and external asset storage without policy edits.

Run against completed build.py output and its reviewed source-only PSP tree.
This is compiled-data evidence, not a licensing or independent-authorship claim.
It does not approve unknown changes or refresh the public source inventory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ASSET_NAME = re.compile(r'jgr_(?:data|ptr)_[A-Za-z_0-9]+\Z')


def digest(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError('Missing or linked audit input: ' + str(path))
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative(value):
    path = Path(value)
    if not value or path.is_absolute() or '\\' in value or any(p in ('', '.', '..') for p in value.split('/')):
        raise ValueError('Unsafe reviewed path: ' + str(value))
    return path


def dependencies(path, obj):
    digest(path)
    words = shlex.split(path.read_text().replace('\\\n', ' '))
    if len(words) < 2 or words[0] != str(obj) + ':':
        raise ValueError('Dependency target differs from linked object: ' + str(path))
    result = [Path(name) for name in words[1:]]
    if any(not name.is_absolute() for name in result):
        raise ValueError('Expected absolute compiler dependencies: ' + str(path))
    for name in result:
        digest(name)
    return result


def leb(data, offset, signed=False):
    value = 0
    for shift in range(0, 70, 7):
        if offset >= len(data):
            raise ValueError('Truncated WebAssembly integer')
        byte = data[offset]; offset += 1
        value |= (byte & 127) << shift
        if not byte & 128:
            if signed and byte & 64:
                value -= 1 << (shift + 7)
            return value, offset
    raise ValueError('Oversized WebAssembly integer')


def wasm_data(data):
    """Return raw data segments, including object-file zero-filled BSS."""
    if data[:8] != b'\0asm\1\0\0\0':
        raise ValueError('Not a WebAssembly object')
    at = 8
    while at < len(data):
        kind = data[at]; size, start = leb(data, at + 1); end = start + size
        if end > len(data):
            raise ValueError('Truncated WebAssembly section')
        at = end
        if kind != 11:
            continue
        section = data[start:end]; count, cursor = leb(section, 0); segments = []
        for _ in range(count):
            flags, cursor = leb(section, cursor)
            if flags not in (0, 1, 2):
                raise ValueError('Unsupported WebAssembly data segment flags')
            if flags == 2:
                _, cursor = leb(section, cursor)
            if flags != 1:
                if cursor >= len(section) or section[cursor] not in (0x41, 0x23):
                    raise ValueError('Unsupported WebAssembly offset expression')
                signed = section[cursor] == 0x41
                _, cursor = leb(section, cursor + 1, signed)
                if cursor >= len(section) or section[cursor] != 0x0b:
                    raise ValueError('Unsupported WebAssembly offset expression')
                cursor += 1
            length, cursor = leb(section, cursor)
            if cursor + length > len(section):
                raise ValueError('Truncated WebAssembly data segment')
            segments.append(section[cursor:cursor + length]); cursor += length
        if cursor != len(section):
            raise ValueError('Unexpected WebAssembly data section suffix')
        return segments
    return []


def field(block, name):
    match = re.search(r'^\s+' + re.escape(name) + r': (.+)$', block, re.M)
    if not match:
        raise ValueError('Missing llvm-readobj field: ' + name)
    return match[1]


def zero_asset_storage(obj, text):
    """Wasm llvm-nm labels BSS as D; check segment identity and actual bytes."""
    segments = re.findall(r'^      Segment \{\n(.*?)^      \}', text, re.M | re.S)
    data = wasm_data(obj.read_bytes())
    if len(segments) != len(data):
        raise ValueError('Data segment evidence differs: ' + obj.name)
    found = {}
    for symbol in re.findall(r'^  Symbol \{\n(.*?)^  \}', text, re.M | re.S):
        name = field(symbol, 'Name')
        if not ASSET_NAME.fullmatch(name) or 'UNDEFINED (' in symbol:
            continue
        if field(symbol, 'Type') != 'DATA (0x1)':
            raise ValueError('External asset is not data: ' + name)
        index = int(field(symbol, 'Segment'), 0)
        offset = int(field(symbol, 'Offset'), 0)
        size = int(field(symbol, 'Size'), 0)
        if not 0 <= index < len(data) or offset < 0 or size < 1 or offset + size > len(data[index]):
            raise ValueError('External asset data range is invalid: ' + name)
        segment = field(segments[index], 'Name')
        if not segment.startswith('.bss.') or any(data[index][offset:offset + size]):
            raise ValueError('External asset contains initialized values: ' + name)
        if name in found:
            raise ValueError('Duplicate external asset definition: ' + name)
        found[name] = {'symbol': name, 'bytes': size, 'segment': segment}
    return found


def audit(build, psp, accepted, review, readobj):
    build, psp, accepted = build.resolve(), psp.resolve(), accepted.resolve()
    report_path = build / 'build.json'
    report = json.loads(report_path.read_text())
    if report.get('html_sha256') != digest(build/'prepare.html') or report.get('converter_sha256') != digest(build/'converter.js'):
        raise ValueError('Build receipt differs from emitted browser artifacts')
    for row in report.get('source_files', []) + report.get('frontend_files', []):
        path = Path(row['path'])
        if not path.is_absolute():
            path = ROOT / relative(row['path'])
        if digest(path) != row['sha256']:
            raise ValueError('Recorded build input changed: ' + row['path'])
    export_path = accepted/'EXPORT-REVIEW.json'
    public_path = accepted/'PUBLIC-CONTENT-REVIEW.json'
    export = json.loads(export_path.read_text())
    public = json.loads(public_path.read_text())
    if export.get('public_content_review_passed') is not True or public.get('passed') is not True:
        raise ValueError('Accepted source lacks a passing content review')
    accepted_rows = {row['path']: row['sha256'] for row in export['files']}
    overlay = json.loads(review.read_text())
    if (overlay.get('schema') != 1 or overlay.get('accepted_export_sha256') != digest(export_path)
            or overlay.get('accepted_public_review_sha256') != digest(public_path)):
        raise ValueError('Browser source review does not bind this accepted export')
    overlays = {row['path']: row for row in overlay.get('overlays', [])}
    for name, row in overlays.items():
        relative(name)
        if not row.get('review') or digest(accepted/name) != row['accepted_sha256']:
            raise ValueError('Browser source delta lacks its exact reviewed baseline: ' + name)
    manifest_path = accepted/'tools/portable/source_data_manifest.json'
    if digest(manifest_path) != accepted_rows.get('tools/portable/source_data_manifest.json'):
        raise ValueError('Accepted source-data manifest changed')
    manifest = json.loads(manifest_path.read_text())
    private = {Path(row['path']).name for row in manifest['files']
               if row['kind'] in ('game_data', 'third_party_numeric_data', 'translated_original_program', 'private_backup')}
    identity = psp/'src/portable_asset_identity.h'
    if (digest(identity) != digest(accepted/'psp/src/portable_asset_identity.h')
            or digest(identity) != report.get('expected_identity_header_sha256')
            or digest(identity) != overlay.get('identity_sha256')):
        raise ValueError('Generated identity header does not match the reviewed release')
    objects = [Path(arg) for arg in report.get('link_command', []) if arg.endswith('.o')]
    if not objects or len(objects) != len(set(objects)):
        raise ValueError('No complete unique linked-object inventory')
    if set(objects) != set((build/'objects').glob('*.o')):
        raise ValueError('Build directory and linked-object inventories differ')
    inputs = {}; units = []; object_rows = []
    for obj in objects:
        if obj.parent != build/'objects':
            raise ValueError('Object outside browser build: ' + str(obj))
        used = dependencies(obj.with_suffix('.d'), obj)
        command = json.loads(obj.with_suffix('.command.json').read_text())
        if isinstance(command, str):
            command = json.loads(command)
        source = Path(command[command.index('-c') + 1])
        if source not in used:
            raise ValueError('Translation unit missing from dependencies: ' + str(source))
        units.append(source)
        object_rows.append({'path': obj.name, 'sha256': digest(obj),
                            'dependencies_sha256': digest(obj.with_suffix('.d')),
                            'command_sha256': digest(obj.with_suffix('.command.json'))})
        for path in used:
            if path.name in private or 'generated/source_data/' in path.as_posix():
                raise ValueError('Original data is an active compiler dependency: ' + str(path))
            if path.is_relative_to(psp):
                name = 'psp/' + path.relative_to(psp).as_posix()
                if name == 'psp/src/portable_asset_identity.h':
                    kind = 'generated_release_identity'
                elif name in overlays:
                    if digest(path) != overlays[name]['browser_sha256']:
                        raise ValueError('Reviewed browser source changed: ' + name)
                    kind = 'reviewed_browser_delta'
                else:
                    if digest(path) != accepted_rows.get(name):
                        raise ValueError('Source is not the accepted exported input: ' + name)
                    kind = 'accepted_export_source'
            elif path.is_relative_to(ROOT/'psp/tests'):
                name = path.relative_to(ROOT).as_posix()
                if digest(path) != accepted_rows.get(name):
                    raise ValueError('Compatibility header differs from accepted export: ' + name)
                kind = 'accepted_compatibility_header'
            elif path.parent == HERE and path.name in ('prepare.c', 'io.c', 'math.c'):
                name = path.relative_to(ROOT).as_posix(); kind = 'browser_adapter'
            else:
                raise ValueError('Unaccounted compiler dependency: ' + str(path))
            inputs[name] = {'path': name, 'sha256': digest(path), 'classification': kind}
    expected = set()
    for source in units:
        if source.name.startswith('source_data_'):
            expected.update(re.findall(r'__asm__\("(jgr_(?:data|ptr)_[A-Za-z_0-9]+)"\)', source.read_text()))
    if not expected:
        raise ValueError('No reviewed external asset storage declarations')
    output = subprocess.check_output([str(readobj), '--sections', '--symbols', *map(str, objects)], text=True)
    chunks = re.split(r'\n(?=File: )', '\n' + output)
    evidence = {}
    for chunk in chunks:
        match = re.match(r'File: (.+)\n', chunk)
        if match:
            evidence[Path(match[1])] = chunk
    if set(evidence) != set(objects):
        raise ValueError('llvm-readobj did not inspect every linked object')
    storage = {}
    for obj in objects:
        found = zero_asset_storage(obj, evidence[obj])
        if storage.keys() & found.keys():
            raise ValueError('External asset is defined by more than one object')
        storage.update(found)
    if set(storage) != expected:
        raise ValueError('External asset inventory differs: missing=' + repr(sorted(expected-storage.keys()))
                         + '; unexpected=' + repr(sorted(storage.keys()-expected)))
    return dict(schema=1, passed=True,
        scope='Exact accepted-source dependencies plus explicit browser adaptations; compiled external asset placeholders contain zero bytes. Not an authorship, licensing, full-conversion or browser-compatibility certification.',
        html_sha256=digest(build/'prepare.html'), converter_sha256=digest(build/'converter.js'),
        build_receipt_sha256=digest(report_path), source_review_sha256=digest(review),
        accepted_export_sha256=digest(export_path), accepted_public_review_sha256=digest(public_path),
        source_data_manifest_sha256=digest(manifest_path), asset_identity_sha256=digest(identity),
        dependency_files=len(objects), active_original_data_includes=[],
        external_asset_symbols=len(storage), external_asset_bytes=sum(row['bytes'] for row in storage.values()),
        inputs=sorted(inputs.values(), key=lambda row: row['path']), objects=object_rows,
        storage=sorted(storage.values(), key=lambda row: row['symbol']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--psp', type=Path, required=True)
    parser.add_argument('--accepted-source', type=Path, required=True)
    parser.add_argument('--source-review', type=Path, required=True)
    parser.add_argument('--llvm-readobj', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = audit(args.build, args.psp, args.accepted_source, args.source_review, args.llvm_readobj)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps({key: result[key] for key in ('passed', 'dependency_files', 'external_asset_symbols', 'external_asset_bytes', 'html_sha256')}, indent=2))
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error)+'\n')


if __name__ == '__main__':
    main()
