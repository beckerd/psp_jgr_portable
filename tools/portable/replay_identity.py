"""Versioned, payload-free identities for reviewed native replay generation.

The original catalog remains the private comparison baseline. A native profile
is available only with a complete PSP validation ledger and matching generator
sources; changing implementation cannot silently reuse old expected hashes.
"""
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib
from package import asset_manifest, safe_name

ROOT = Path(__file__).resolve().parents[2]
PROFILE = 'native-v1'
DEFAULT_IDENTITY = Path(__file__).with_name('native_replay_identity.json')

def native_outputs():
    from replay_recipes import jobs, outputs
    names = {name for job in jobs() for name in outputs(job)}
    names.add('source_data_ptr_challenge_demo_data.jsd')
    return names

def fingerprint(sources):
    return hashlib.sha256(json.dumps(sources, sort_keys=True, separators=(',', ':')).encode('ascii')).hexdigest()

def required_sources(root):
    from build_device_setup import NATIVE_CORE, setup_sources
    import source_data
    names = set(NATIVE_CORE) | {'player.c', 'portable_replay_pack.c', 'portable_replay_pack.h',
                               'portable_replay_session.c', 'portable_replay_session.h',
                               'source_data_ptr_challenge_demo_data.c', 'source_fmac.h',
                               'source_float_fast.h', 'stage_texture_usage.inc'}
    names.update(p.name for p in (Path(root) / 'psp/src').glob('portable_native_*')
                 if p.suffix in ('.c', '.h', '.inc'))
    required = {'psp/src/' + name for name in names}
    manifest = Path(root) / 'tools/portable/source_data_manifest.json'
    if manifest.is_file():
        # Headers and generated loaders participate in output just as much
        # as translation units. Match the setup builder's include closure,
        # excluding only absent private inputs and the cyclic identity header.
        public = {r['path'] for r in source_data.load_manifest(manifest)['files']
                  if r['kind'] in source_data.PUBLIC}
        _, closure, _ = setup_sources(Path(root) / 'psp')
        required.update({'psp/src/' + name for name in closure}.intersection(public))
    required.discard('psp/src/portable_asset_identity.h')
    return required

def hex_digest(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None

def canonical_sources(root, names):
    """Hash reviewed exported bytes in both private and clean source trees.

    Inactive legacy includes are rewritten by source_data.transform too. Hashing
    raw private files would therefore reject the identical clean build. The
    public review ledger admits already-transformed source; neither policy file
    nor the generated asset-identity header is itself part of this fingerprint.
    """
    import source_data
    root = Path(root)
    document = source_data.load_manifest(root / 'tools/portable/source_data_manifest.json')
    rows = {r['path']: r for r in document['files']}
    # public_sources binds exporter/tool inputs; the exported runtime
    # inventory is public_review.reviewed_files, including rewritten headers.
    from public_review import file_rows
    public = root / 'tools/portable/public_review.json'
    published = {}
    if public.is_file():
        policy = json.loads(public.read_text())
        published = {name: row['sha256'] for name, row in
                     file_rows(policy.get('reviewed_files', [])).items()}
    includes = {Path(r['path']).name: str(Path(r['output']).relative_to('psp/src'))
                for r in document['files'] if r['kind'] in source_data.PRIVATE - {'private_backup'}}
    result = {}
    for name in sorted(names):
        safe_name(name)
        if (name == 'psp/src/portable_asset_identity.h' or not name.startswith('psp/src/') or
                len(Path(name).parts) != 3 or Path(name).suffix not in ('.c', '.h', '.inc')):
            raise ValueError('Invalid native replay generator input: ' + name)
        path = source_data.contained(root, name)
        row = rows.get(name)
        if not row or row['kind'] not in source_data.PUBLIC:
            raise ValueError('Unreviewed native replay generator input: ' + name)
        data = path.read_bytes();actual = hashlib.sha256(data).hexdigest()
        if actual == row['sha256']:
            data = source_data.transform(data, row, includes)
        elif actual != published.get(name):
            raise ValueError('Native replay generator source changed: ' + name)
        result[name] = hashlib.sha256(data).hexdigest()
    return result

def cue_layout(root):
    code = (Path(root) / 'psp/src/source_data_ptr_challenge_demo_data.c').read_text()
    match = re.search(r'source_data_pointers_load_file\("source_data_ptr_challenge_demo_data\.jsd",\s*sections,\s*(\d+)u,\s*(\d+)u,\s*(0x[0-9a-fA-F]+)u,\s*(\d+)u\)', code)
    if not match:
        raise ValueError('Native replay cue loader signature is unavailable')
    sections, size, crc, fixups = (int(v, 0) for v in match.groups())
    return dict(sections=sections, bytes=size, crc32=crc, fixups=fixups)

def apply_native_catalog(catalog, identity, root=ROOT):
    """Return a new catalog; never mutate or replace the original baseline."""
    if not isinstance(identity, dict) or identity.get('schema') != 1 or identity.get('profile') != PROFILE:
        raise ValueError('Unsupported native replay identity profile')
    rows = catalog['files']
    if any(type(row.get('crc32')) is not int for row in rows):
        raise ValueError('Native replay profile requires a complete CRC32 catalog')
    baseline = '%08x' % (zlib.crc32(asset_manifest(rows)) & 0xffffffff)
    if identity.get('baseline_manifest_crc32') != baseline:
        raise ValueError('Native replay identities belong to a different baseline catalog')
    validation = identity.get('validation', {})
    if (not isinstance(validation, dict) or validation.get('target') != 'psp' or
            validation.get('method') not in ('hardware', 'ppsspp') or
            validation.get('fidelity_reviewed') is not True or
            not hex_digest(validation.get('receipt_sha256'))):
        raise ValueError('Native replay identities require a reviewed PSP validation receipt')
    sources = identity.get('generator_sources', {})
    if (not isinstance(sources, dict) or not required_sources(root).issubset(sources) or
            identity.get('generator_sha256') != fingerprint(sources)):
        raise ValueError('Native replay generator fingerprint is incomplete or invalid')
    if canonical_sources(root, sources) != sources:
        raise ValueError('Native replay generator source changed')
    layout = cue_layout(root)
    if validation.get('cue_layout') != layout:
        raise ValueError('Native replay cue layout differs from the runtime loader')
    native = identity.get('files')
    if not isinstance(native, list):
        raise ValueError('Native replay identities are missing')
    expected = native_outputs()
    names = [row.get('path') for row in native if isinstance(row, dict)]
    if len(names) != len(native) or len(names) != len(set(names)) or set(names) != expected:
        raise ValueError('Native replay identity set must cover every native route and cue output')
    baseline_names = {row['path'] for row in rows}
    if not expected.issubset(baseline_names):
        raise ValueError('Native replay catalog is missing required outputs')
    replacements = {}
    for row in native:
        name = safe_name(row['path'])
        if (set(row) != {'path', 'bytes', 'crc32', 'sha256'} or type(row['bytes']) is not int or
                not 0 < row['bytes'] <= 0x7fffffff or type(row['crc32']) is not int or
                not 0 <= row['crc32'] <= 0xffffffff or not hex_digest(row['sha256'])):
            raise ValueError('Invalid native replay checksum metadata: ' + name)
        if name == 'source_data_ptr_challenge_demo_data.jsd' and row['bytes'] != layout['bytes']:
            raise ValueError('Native replay cue size differs from the runtime loader')
        replacements[name] = dict(row)
    result = dict(catalog)
    result['files'] = [replacements.get(row['path'], dict(row)) for row in rows]
    result['replay_profile'] = PROFILE
    result['native_generator_sha256'] = identity['generator_sha256']
    return result

def select_catalog(catalog, profile='original', identity=None, root=ROOT):
    if profile == 'original':
        if catalog.get('replay_profile', 'original') != 'original':
            raise ValueError('A native catalog cannot use original replay recipes')
        return catalog
    if profile != PROFILE:
        raise ValueError('Unsupported replay profile: ' + profile)
    if identity is None:
        if not DEFAULT_IDENTITY.is_file():
            raise ValueError('Native replay identities are not yet validated; profile unavailable')
        identity = json.loads(DEFAULT_IDENTITY.read_text())
    return apply_native_catalog(catalog, identity, root)

def output_identity(path, name):
    sha = hashlib.sha256();crc = 0;size = 0
    with path.open('rb') as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:break
            sha.update(block);crc = zlib.crc32(block, crc);size += len(block)
    return dict(path=name, bytes=size, crc32=crc, sha256=sha.hexdigest())

def checked_cue_layout(path):
    """Check the actual pointer-table directory, not a claimed receipt field."""
    data = path.read_bytes()
    if len(data) < 40 or len(data) > 4 * 1024 * 1024:
        raise ValueError('Invalid native replay cue image size')
    magic, version, sections, size, crc, payload_crc, header, fixups, width, reserved = struct.unpack_from('<4s9I', data)
    if (magic != b'JGSD' or version != 2 or size != len(data) or width != 4 or reserved or
            not 0 < sections <= 4096 or fixups > 65536 or header != 40 + 16 * (sections + fixups) or
            header > size or zlib.crc32(data[40:header]) != crc or zlib.crc32(data[header:]) != payload_crc):
        raise ValueError('Invalid native replay cue image layout or checksum')
    return dict(sections=sections, bytes=size, crc32=crc, fixups=fixups)

def collect_native_identity(catalog, output, receipt_path, root=ROOT):
    """Collect hashes only after an explicit, source-bound PSP fidelity review.

    The review receipt must name the exact generated file hashes and generator
    fingerprint. This function checks them; it does not declare fidelity from
    successful execution or quietly bless the current output directory.
    """
    import source_data
    output = Path(output);receipt_bytes = Path(receipt_path).read_bytes()
    receipt = json.loads(receipt_bytes)
    if (receipt.get('schema') != 1 or receipt.get('target') != 'psp' or
            receipt.get('method') not in ('hardware', 'ppsspp') or receipt.get('fidelity_reviewed') is not True):
        raise ValueError('A reviewed PSP fidelity receipt is required')
    sources = canonical_sources(root, required_sources(root))
    if receipt.get('generator_sha256') != fingerprint(sources):
        raise ValueError('PSP receipt does not identify the current native generator')
    files = [output_identity(source_data.contained(output, name), name) for name in sorted(native_outputs())]
    recorded = receipt.get('files')
    if (not isinstance(recorded, list) or any(not isinstance(row, dict) for row in recorded) or
            sorted(recorded, key=lambda r:r.get('path', '')) != files):
        raise ValueError('Native output hashes differ from the reviewed PSP receipt')
    layout = checked_cue_layout(source_data.contained(output, 'source_data_ptr_challenge_demo_data.jsd'))
    if receipt.get('cue_layout') != layout:
        raise ValueError('PSP receipt cue layout differs from its generated output')
    identity = dict(schema=1, profile=PROFILE,
        baseline_manifest_crc32='%08x' % (zlib.crc32(asset_manifest(catalog['files'])) & 0xffffffff),
        generator_sources=sources, generator_sha256=fingerprint(sources), files=files,
        validation=dict(target='psp', method=receipt['method'], fidelity_reviewed=True,
                        receipt_sha256=hashlib.sha256(receipt_bytes).hexdigest(), cue_layout=layout))
    apply_native_catalog(catalog, identity, root)
    return identity

def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, default=Path(__file__).with_name('asset_catalog.json'))
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--fingerprint', action='store_true', help='Print reviewed public-source fingerprint before a PSP run')
    parser.add_argument('--outputs', type=Path)
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if args.fingerprint:
        sources = canonical_sources(args.source_root, required_sources(args.source_root))
        print(json.dumps(dict(generator_sha256=fingerprint(sources), generator_sources=sources), indent=2, sort_keys=True))
        return
    if not all((args.outputs, args.receipt, args.out)):
        parser.error('--outputs, --receipt and --out are required to collect identities')
    identity = collect_native_identity(json.loads(args.catalog.read_text()), args.outputs, args.receipt, args.source_root)
    args.out.write_text(json.dumps(identity, indent=2, sort_keys=True) + '\n')
    print(json.dumps(dict(profile=PROFILE, files=len(identity['files']), generator_sha256=identity['generator_sha256'])))

if __name__ == '__main__':main()
