#!/usr/bin/env python3
"""Build validated local asset packs and isolated PSP test installations.

Original game assets in these outputs are PRIVATE. This tool does not produce a
public release or certify an executable as free of embedded game content.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tempfile
import zlib
import re
import struct

LAYOUT_VERSION = 2
MANIFEST_NAME = 'assets-manifest.txt'
EXECUTABLES = {'EBOOT.PBP', 'jgr_me.prx', 'jgr_crash.prx'}
NON_ASSETS = EXECUTABLES | {'crash_handler_license.txt'}


def safe_name(name):
    path = PurePosixPath(name)
    if (not name or path.is_absolute() or str(path) != name or
            any(part in ('', '.', '..') for part in name.split('/')) or
            len(name) >= 192 or not re.fullmatch(r'[A-Za-z0-9_./-]+', name)):
        raise ValueError('Unsafe asset path: ' + repr(name))
    return name


def checksums(path):
    sha, crc, size = hashlib.sha256(), 0, 0
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            sha.update(block)
            crc = zlib.crc32(block, crc)
            size += len(block)
    return size, sha.hexdigest(), crc & 0xffffffff


def asset_manifest(rows):
    rows = sorted(rows, key=lambda row: row['path'])
    names = [safe_name(row['path']) for row in rows]
    if not rows or len(names) != len(set(names)) or MANIFEST_NAME in names:
        raise ValueError('Empty, duplicate or reserved manifest entry')
    if len(rows) > 20000:
        raise ValueError('Asset count exceeds runtime limit')
    for name in names:
        if (name.startswith(('data/', 'jgr_', 'shot_', 'jgr_story.sav', 'route_save_fixture.sav')) or
                name in ('level.txt', 'shot.raw', 'shot2.raw', 'shot3.raw', 'shot4.raw') or
                ('.' not in name and '/' not in name)):
            raise ValueError('Runtime-owned name cannot be an asset: ' + name)
    text = 'JGR_ASSETS 1\nlayout %d\ncount %d\n' % (LAYOUT_VERSION, len(rows))
    for row in rows:
        size, crc = row['bytes'], row['crc32']
        if not isinstance(size, int) or not 0 <= size <= 0x7fffffff or not 0 <= crc <= 0xffffffff:
            raise ValueError('Asset size/checksum out of range')
        text += '%d %08x %s\n' % (size, crc, row['path'])
    encoded = text.encode('ascii')
    if len(encoded) > 4 * 1024 * 1024:
        raise ValueError('Manifest exceeds runtime limit')
    return encoded


def write_identity(manifest, count, path):
    crc = zlib.crc32(manifest) & 0xffffffff
    content = ('/* Generated asset identity; no original game content. */\n'
               '#ifndef JGR_PORTABLE_ASSET_IDENTITY_H\n#define JGR_PORTABLE_ASSET_IDENTITY_H\n'
               '#define JGR_ASSET_LAYOUT_VERSION %du\n'
               '#define JGR_ASSET_MANIFEST_CRC32 0x%08xu\n'
               '#define JGR_ASSET_FILE_COUNT %du\n#endif\n') % (LAYOUT_VERSION, crc, count)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(content)
    temporary.replace(path)


def identity_from_catalog(catalog, path, replay_profile='original', native_identity=None):
    """Build the installed-pack identity using only public checksum metadata."""
    from replay_identity import select_catalog
    catalog = select_catalog(catalog, replay_profile, native_identity)
    rows = catalog['files']
    if any(type(row.get('crc32')) is not int for row in rows):
        raise ValueError('Asset catalog requires CRC32 metadata; no asset input is needed')
    manifest = asset_manifest(rows)
    write_identity(manifest, len(rows), path)
    return {'files': len(rows), 'replay_profile': replay_profile, 'manifest_crc32': '%08x' % (zlib.crc32(manifest) & 0xffffffff)}


def stage_assets(source, catalog, destination, identity=None, reference=False):
    if destination.exists():
        raise ValueError('Destination must not exist: ' + str(destination))
    if not reference and catalog.get('source') != 'user-disc':
        raise ValueError('Generated assets must identify their user-disc source; use --reference only for private baseline tests')
    if not reference and not catalog.get('complete', False):
        raise ValueError('Disc asset generation is incomplete')
    rows = [r for r in catalog['files'] if r['path'] not in NON_ASSETS]
    names = [safe_name(r['path']) for r in rows]
    if len(names) != len(set(names)):
        raise ValueError('Duplicate catalog path')
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=destination.name + '.partial-', dir=destination.parent))
    try:
        output = []
        for row in sorted(rows, key=lambda row: row['path']):
            name = row['path']
            src, dst = source / name, temporary / name
            if not reference and not src.resolve().is_relative_to(source.resolve()):
                raise ValueError('Asset escapes generated root: ' + name)
            size, sha, crc = checksums(src)
            if row.get('bytes') != size or row.get('sha256') != sha:
                raise ValueError('Asset differs from catalog: ' + name)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            if checksums(dst) != (size, sha, crc):
                raise ValueError('Copy verification failed: ' + name)
            output.append({'path': name, 'bytes': size, 'sha256': sha, 'crc32': crc})
        manifest = asset_manifest(output)
        (temporary / MANIFEST_NAME).write_bytes(manifest)
        temporary.rename(destination)
        if identity:
            write_identity(manifest, len(output), identity)
        report = {'schema': 1, 'source': catalog.get('source', 'private-reference'),
                  'reference_only': reference, 'complete': not reference,
                  'public_distribution': False, 'files': output,
                  'manifest_crc32': '%08x' % (zlib.crc32(manifest) & 0xffffffff)}
        destination.with_suffix('.catalog.json').write_text(json.dumps(report, indent=2) + '\n')
        return report
    except BaseException:
        if temporary.exists():
            shutil.rmtree(temporary)
        raise


def install_image(build, assets, catalog, destination, setup_background=None):
    if destination.exists():
        raise ValueError('Destination must not exist: ' + str(destination))
    expected = asset_manifest(catalog['files'])
    if (assets / MANIFEST_NAME).read_bytes() != expected:
        raise ValueError('Asset manifest does not match catalog')
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=destination.name + '.partial-', dir=destination.parent))
    rows = []
    try:
        paths = [(name, build / name) for name in sorted(EXECUTABLES)]
        paths += [('data/assets/' + row['path'], assets / safe_name(row['path'])) for row in catalog['files']]
        paths.append(('data/assets/' + MANIFEST_NAME, assets / MANIFEST_NAME))
        license_path = build / 'crash_handler_license.txt'
        if not license_path.is_file():
            raise ValueError('Missing crash-handler license notice')
        paths.append(('crash_handler_license.txt', license_path))
        for name, src in paths:
            dst = temporary / name
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            size, sha, crc = checksums(dst)
            if checksums(src) != (size, sha, crc):
                raise ValueError('Installation copy mismatch: ' + name)
            rows.append({'path': name, 'bytes': size, 'sha256': sha})
        for row in catalog['files']:
            size, sha, _ = checksums(temporary / 'data/assets' / row['path'])
            if (size, sha) != (row['bytes'], row['sha256']):
                raise ValueError('Installation asset mismatch: ' + row['path'])
        for name in ('log', 'cache'):
            (temporary / 'data' / name).mkdir()
        background = None
        if setup_background is not None:
            import setup_background as background_converter
            name = background_converter.INSTALL_PATH
            background = background_converter.convert(setup_background, temporary / name)
            rows.append({'path': name, 'bytes': background['bytes'], 'sha256': background['sha256']})
        report = {'schema': 1, 'public_distribution': False,
                  'reference_only': catalog.get('reference_only', False),
                  'hardware_validated': False, 'files': rows}
        if background is not None:
            report['setup_background'] = dict(background, path=name)
        (temporary / 'installation.json').write_text(json.dumps(report, indent=2) + '\n')
        temporary.rename(destination)
        return report
    except BaseException:
        if temporary.exists():
            shutil.rmtree(temporary)
        raise


def starter_image(build, destination, icon0=None, pic1=None, setup_background=None,
                  replay_profile='original', native_identity=None):
    """Stage an EBOOT-only folder; this checks layout, not publication approval."""
    from bundle import validate, module, HEADER as BUNDLE_HEADER, ENTRY
    from install_plan import make_plan
    if destination.exists() or destination.is_symlink():
        raise ValueError('Destination must not exist: ' + str(destination))
    executable = build / 'EBOOT.PBP'
    receipt_path = build / '.setup/build-receipt.json'
    if executable.is_symlink() or receipt_path.is_symlink():
        raise ValueError('Starter inputs must not be linked')
    data = executable.read_bytes()
    if len(data) < 40 or data[:4] != b'\0PBP':
        raise ValueError('Invalid PSP executable container')
    offsets = struct.unpack_from('<8I', data, 8)
    if offsets[0] != 40 or list(offsets) != sorted(offsets) or offsets[-1] >= len(data):
        raise ValueError('Invalid PSP executable offsets')
    # Public artwork has not been selected/reviewed. The starter contains no
    # ICON0, ICON1, PIC0, PIC1 or SND0 copied from the original game.
    if (offsets[2] != offsets[3] or offsets[3] != offsets[4] or offsets[5] != offsets[6] or
            (offsets[1] != offsets[2] and icon0 is None) or
            (offsets[4] != offsets[5] and pic1 is None)):
        raise ValueError('Starter contains unreviewed XMB artwork or sound')
    module(data[offsets[6]:offsets[7]])
    packed = data[offsets[7]:]
    bundle_report = validate(packed)
    if len(bundle_report['entries']) != 5:
        raise ValueError('Starter needs bundled helpers, notices, recipe and converter')
    metadata = Path(__file__).resolve().parent
    plan, coverage = make_plan(json.loads((metadata / 'asset_catalog.json').read_text()),
                               json.loads((metadata / 'raw_assets.json').read_text())['files'],
                               replay_profile=replay_profile,native_identity=native_identity)
    if not coverage['complete']:
        raise ValueError('Incomplete on-device conversion plan cannot be packaged')
    _, offset, size, _ = ENTRY.unpack_from(packed, BUNDLE_HEADER.size + 3 * ENTRY.size)
    if packed[offset:offset + size] != plan:
        raise ValueError('Bundled instructions differ from the complete reviewed conversion plan')
    digest = hashlib.sha256(data).hexdigest()
    receipt = json.loads(receipt_path.read_text())
    if receipt.get('eboot_sha256') != digest or receipt.get('bundle') != bundle_report:
        raise ValueError('Starter executable does not match its module-build receipt')
    from build_device_setup import with_artwork
    data, artwork = with_artwork(data, icon0, pic1)
    digest = hashlib.sha256(data).hexdigest()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=destination.name + '.partial-', dir=destination.parent))
    try:
        (temporary / 'EBOOT.PBP').write_bytes(data)
        rows = [{'path': 'EBOOT.PBP', 'bytes': len(data), 'sha256': digest}]
        background = None
        if setup_background is not None:
            import setup_background as background_converter
            name = background_converter.INSTALL_PATH
            background = background_converter.convert(setup_background, temporary / name)
            rows.append({'path': name, 'bytes': background['bytes'], 'sha256': background['sha256']})
        temporary.rename(destination)
    except BaseException:
        shutil.rmtree(temporary)
        raise
    report = {'files': rows, 'reference_only': False, 'public_distribution': False,
              'source_eboot_sha256': receipt['eboot_sha256'],
              'scope': 'Complete installer packaging; source/build audits and hardware validation remain separate'}
    if artwork:
        report['private_artwork'] = artwork
    if background is not None:
        report['setup_background'] = dict(background, path=name)
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='command', required=True)
    assets = sub.add_parser('assets')
    assets.add_argument('--source', type=Path, required=True)
    assets.add_argument('--catalog', type=Path, required=True)
    assets.add_argument('--out', type=Path, required=True)
    assets.add_argument('--identity', type=Path)
    assets.add_argument('--reference', action='store_true', help='Private baseline comparison only; never a reproducibility certificate')
    image = sub.add_parser('image')
    image.add_argument('--build', type=Path, required=True)
    image.add_argument('--assets', type=Path, required=True)
    image.add_argument('--catalog', type=Path, required=True)
    image.add_argument('--out', type=Path, required=True)
    image.add_argument('--setup-background', type=Path,
                       help='Optional explicitly supplied 480x272 PNG for the private setup screen')
    starter = sub.add_parser('starter', help='Stage only EBOOT with the complete bundled installer')
    starter.add_argument('--build', type=Path, required=True)
    starter.add_argument('--out', type=Path, required=True)
    starter.add_argument('--icon0', type=Path, help='Explicit private 144x80 PNG for the XMB icon')
    starter.add_argument('--pic1', type=Path, help='Explicit private 480x272 PNG for the XMB background')
    starter.add_argument('--setup-background', type=Path,
                         help='Optional explicitly supplied 480x272 PNG for the private setup screen')
    identity = sub.add_parser('identity', help='Generate build identity from checksum metadata, without game assets')
    identity.add_argument('--catalog', type=Path, default=Path(__file__).with_name('asset_catalog.json'))
    identity.add_argument('--out', type=Path, required=True)
    for command in (identity, starter):
        command.add_argument('--replay-profile', choices=('original', 'native-v1'), default='native-v1')
        command.add_argument('--native-identity', type=Path)
    args = ap.parse_args()
    native_identity = json.loads(args.native_identity.read_text()) if getattr(args, 'native_identity', None) else None
    if args.command == 'identity':
        print(json.dumps(identity_from_catalog(json.loads(args.catalog.read_text()), args.out, args.replay_profile, native_identity)))
        return
    if args.command == 'starter':
        result = starter_image(args.build, args.out, args.icon0, args.pic1, args.setup_background, args.replay_profile, native_identity)
    elif args.command == 'assets':
        catalog = json.loads(args.catalog.read_text())
        result = stage_assets(args.source, catalog, args.out, args.identity, args.reference)
    else:
        catalog = json.loads(args.catalog.read_text())
        result = install_image(args.build, args.assets, catalog, args.out, args.setup_background)
    print(json.dumps({'files': len(result['files']), 'public_distribution': False,
                      'reference_only': result['reference_only'], 'output': str(args.out)}))


if __name__ == '__main__':
    main()
