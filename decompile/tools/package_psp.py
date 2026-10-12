#!/usr/bin/env python3
"""Stage a save-free, diagnostic-free PSP image and verify it by SHA-256.

Does not deploy, delete files, or modify an existing image. The source adx
directory may be the checkout's symlink into the older dist/JGRVIEW image.
Verification is read-only and can also check the files copied to a PSP.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

ROOT = Path(__file__).resolve().parents[2]
SUFFIXES = {'.bin', '.jpn', '.jgc', '.jgs', '.jvo', '.jrs', '.jgm', '.adx', '.col', '.sob', '.hgt'}
TEXT = {'events.txt', 'grf_names.txt', 'grf_desc.txt', 'radio_names.txt', 'crash_handler_license.txt'}
OBSOLETE = {'stage1_old.jgs', 'stage1_new.jgs'}
RUNTIME_OUTPUTS = {'jgr_log.txt', 'jgr_log_prev.txt', 'jgr_me_log.txt',
                   'jgr_crash.txt', 'jgr_hang.txt', 'jgr_hang_stack.bin'}
FLAGS = set('''autopilot realtimeprofile storyinput lookat level.txt flat nosky nocull nohull hullcw
    nosave missionentry recruitinspect audioaudit spikecapture spikefixture verboseperf logfixture'''.split())


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def diagnostic(name):
    return name in FLAGS or name.endswith('test')


def verify_eboot(source):
    data = (source / 'EBOOT.PBP').read_bytes()
    magic, version, *offsets = struct.unpack_from('<10I', data)
    if magic != 0x50425000 or offsets != sorted(offsets) or offsets[0] != 40:
        raise ValueError('Invalid PBP header')
    if offsets[-1] > len(data):
        raise ValueError('Truncated PBP')
    offsets.append(len(data))
    sections = [data[a:b] for a, b in zip(offsets, offsets[1:])]
    for index, name, dims in ((1, 'JSR-ICON0.png', (144, 80)),
                              (4, 'JSR-PIC1.png', (480, 272))):
        art = (source / 'Icon' / name).read_bytes()
        if art[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack_from('>II', art, 16) != dims:
            raise ValueError(f'Invalid PSP artwork: {name}')
        if sections[index] != art:
            raise ValueError(f'EBOOT artwork is stale: {name}')
    if sections[6] != (source / 'jgrview.prx').read_bytes():
        raise ValueError('EBOOT executable is stale')
    sfo = sections[0]
    smagic, _, keys, values, count = struct.unpack_from('<5I', sfo)
    if smagic != 0x46535000:
        raise ValueError('Invalid SFO')
    fields = {}
    for i in range(count):
        key, fmt, length, capacity, offset = struct.unpack_from('<HHIII', sfo, 20 + 16 * i)
        name = sfo[keys + key:].split(b'\0', 1)[0].decode()
        value = sfo[values + offset:values + offset + length]
        fields[name] = struct.unpack('<I', value)[0] if fmt == 0x404 else value.rstrip(b'\0').decode()
    for key, value in {'TITLE': 'JGR Portable', 'DISC_ID': 'JGRV00001', 'MEMSIZE': 0}.items():
        if fields.get(key) != value:
            raise ValueError(f'Unexpected SFO {key}: {fields.get(key)!r}')
    return fields


def asset_list(source):
    names = {'EBOOT.PBP', 'jgr_me.prx', 'jgr_crash.prx'}
    for p in source.iterdir():
        if p.is_file() and not p.name.startswith('.') and p.name not in OBSOLETE | RUNTIME_OUTPUTS:
            if p.suffix.lower() in SUFFIXES or p.name in TEXT:
                names.add(p.name)
    for folder, suffix in (('adx', '.adx'), ('grf', '.bin')):
        for p in (source / folder).iterdir():
            if p.is_file() and not p.name.startswith('.') and p.suffix.lower() == suffix:
                names.add(f'{folder}/{p.name}')
    required = {'jgr_me.prx', 'jgr_crash.prx', 'chars_compact.jgc', 'missions.bin', 'gamehud.bin', 'title.jgc',
                'title_tex.bin', 'title_route.bin', 'title_ui.bin', 'title_lists.bin', 'garage.jgc', 'loading.jgc', 'finalscene.jgc', 'paintspray.jgc'}
    for n in range(35):
        required.update((f'movie{n:02}.bin', f'movie{n:02}.jgc'))
    required.update(f'resultcam{n:02}.bin' for n in range(16))
    required.update(f'staff{n:02}.bin' for n in range(20))
    required.update(f'staffroute{n:02}{suffix}.bin' for n in range(16) for suffix in ('','_meta'))
    required.update(f'garam_demo{n}.bin' for n in range(3))
    required.update(f'mew_demo{n}.bin' for n in range(3))
    required.update(f'people{n}_compact.jgc' for n in (1, 2, 3, 5))
    required.update(f'people{n}_native.jpn' for n in (1, 2, 3, 5))
    for mission,count in (('s1m11',6),('s1m06',3),('s1m07',1),('s3m11',3)):
        required.update(f'challenge_{mission}_{n}.bin' for n in range(count))
    required.update('challenge_rival_'+name+'.bin' for name in ('s1m07','s2m07','s3m07'))
    required.update(('benten_crane.jgc','grind_lifts.jgc','grind_lights.jgs'))
    required.update(f'{city}_train{suffix}' for city in ('benten','bantam') for suffix in ('.bin','.jgc'))
    required.update(f'rivaltag{n}.bin' for n in range(5))
    required.update(f'{gang}_rival{n}.bin' for gang in ('poison','noise','love','mask') for n in range(3))
    required.update(f'skatergraffiti{n:02}.bin' for n in range(16))
    required.update(('enemyprojectiles.jgc','enemygas.jgc','enemyaudio.bin','tabanfx.bin','burning.jgc'))
    required.update(('scenes.bin','cutscene_sources.bin','eventflare.jgc','eventblast.jgc'))
    required.update(f'eventplay{n:02}{suffix}.jgc' for n in (18,19,20) for suffix in ('','_mouth'))
    # EVENT bindings live in generated C tables, outside the literal .c/.h
    # scan below. Every referenced mesh must travel with the executable.
    required.update(re.findall(r'"([^"\n]+\.jgc)"',(source/'src/scene_models.inc').read_text()))
    required.update(f'movievoice{n:03}.adx' for n in range(184))
    for n in (1, 2, 3, 5, 6):
        required.update(f'stage{n}{suffix}' for suffix in ('.jgs', '.jvo', '.jrs', '.jgm', '.col', '.sob', '_obj.jgs'))
        required.update(f'traffic{n}{suffix}' for suffix in ('.bin','.jgc','_grips.bin'))
    if required - names:
        raise ValueError(f'Missing assets: {sorted(required - names)}')
    if len([n for n in names if n.startswith('adx/')]) != 211:
        raise ValueError('Expected all 211 original ADX music files')
    if len([n for n in names if n.startswith('grf/')]) != 102:
        raise ValueError('Expected all 102 graffiti images')
    # Also check literal file references in runtime code. Format strings are
    # covered by the families above and the source data manifests.
    required_literals = set()
    for p in (source / 'src').glob('*'):
        if p.suffix not in ('.c', '.h'):
            continue
        for name in re.findall(r'"([^"\n]+\.(?:bin|jgc|jgs|jvo|jrs|adx|col|sob|hgt|txt))"', p.read_text()):
            name = name.removeprefix('ms0:/PSP/GAME/JGRVIEW/')
            if '%' not in name and name.lstrip('/') not in RUNTIME_OUTPUTS and name not in ('level.txt', 'music.adx'):
                required_literals.add(name)
    # stream.c tries music.adx if the legacy startup alias music1.adx is absent.
    if 'music.adx' in names:
        required_literals.discard('music1.adx')
    if required_literals - names:
        raise ValueError(f'Missing referenced files: {sorted(required_literals - names)}')
    return sorted(names)


def verify(image, manifest):
    for row in manifest['files']:
        name = Path(row['path'])
        if name.is_absolute() or '..' in name.parts:
            raise ValueError('Unsafe manifest path')
        p = image / name
        if not p.is_file() or p.stat().st_size != row['bytes'] or digest(p) != row['sha256']:
            raise ValueError(f'Verification failed: {name}')
    flags = [p.name for p in image.iterdir() if diagnostic(p.name)]
    if flags:
        raise ValueError(f'Diagnostic controls remain in player image: {flags}')
    print(f'Verified {len(manifest["files"])} files, {manifest["bytes"]:,} bytes: {image}', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, default=ROOT / 'psp')
    ap.add_argument('--out', type=Path, help='New package directory; must not exist')
    ap.add_argument('--verify', type=Path, help='Read-only verification of an image')
    ap.add_argument('--manifest', type=Path, help='Manifest for --verify')
    args = ap.parse_args()
    if args.verify:
        if not args.manifest or args.out:
            ap.error('--verify requires --manifest and cannot be combined with --out')
        verify(args.verify, json.loads(args.manifest.read_text()))
        return
    if not args.out:
        ap.error('--out is required when packaging')
    sfo = verify_eboot(args.source)
    names = asset_list(args.source)
    args.out.mkdir(parents=True, exist_ok=False)
    image = args.out / 'JGRVIEW'
    image.mkdir()
    rows = []
    for name in names:
        src, dst = args.source / name, image / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
        rows.append({'path': name, 'bytes': src.stat().st_size, 'sha256': digest(src)})
    manifest = {'sfo': sfo, 'bytes': sum(r['bytes'] for r in rows), 'files': rows,
                'hardware_validated': False, 'campaign_complete': False}
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (args.out / 'files.txt').write_text(''.join(n + '\n' for n in names))
    verify(image, manifest)


if __name__ == '__main__':
    main()
