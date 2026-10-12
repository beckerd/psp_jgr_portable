#!/usr/bin/env python3
"""Export the two 0a6710 wreck trees and eight attached effect trees per bank.

Bindings come from 0a5ab0's executable tables, NOT flight-root heuristics.
Texture offsets in these ENEMY banks use 8ca00000 (also the DAT base).
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DEC = ROOT / 'decompile'
BASE = 0x8ca00000

def manifest():
    exe = (DEC / 'extracted/1ST_READ.BIN').read_bytes()
    def u(a): return struct.unpack_from('<I', exe, a - 0x8c010000)[0]
    rows = []
    sources = []
    for variant, (dat, txr, table, bindings) in enumerate([
        ('025_heridat', '024_heritxr', 0x8c1b632c, 0x8c1c98f0),
        ('029_bheridat', '028_bheritxr', 0x8c1b647c, 0x8c1c9798),
    ]):
        dp, tp = f'afs/ENEMY/{dat}.bin', f'afs/ENEMY/{txr}.bin'
        data, textures = (DEC / dp).read_bytes(), (DEC / tp).read_bytes()
        offsets = {}; at = 0
        while (at := textures.find(b'GBIX', at)) >= 0:
            offsets[at] = len(offsets); at += 8
        maps = {}; at = bindings
        while u(at):
            tl, ptr = u(at), u(at + 4)
            maps[tl] = [offsets[u(ptr + k * 4) - BASE] for k in range(u(tl + 4))]
            at += 8
            assert at < bindings + 256
        sources.append(dict(data=dp, textures=tp, data_sha256=hashlib.sha256(data).hexdigest(),
                            textures_sha256=hashlib.sha256(textures).hexdigest()))
        for part in range(10):
            if part < 2:
                root = u(table + 8 + part * 4)
                motions = [u(table + 0x3c), u(table + 0x40)]
                tl = u(table + 0x14)
            else:
                root = u(table + 0x78 + (part - 2) * 4)
                motions = [u(table + 0x9c + (part - 2) * 4)]
                tl = u(table + 0x74)
            assert BASE <= root < BASE + len(data) - 52
            headers = [struct.unpack_from('<I', data, a - BASE)[0] for a in motions]
            assert all(h & 65535 in (0x11, 0x15) for h in headers)
            rows.append(dict(name=f'heli{variant}_{part}', variant=variant, part=part,
                             root=root, motions=motions, frames=[h >> 16 for h in headers],
                             texlist=tl, texture_map=maps[tl], draw_mode=part % 2))
    return dict(sources=sources, models=rows)

def export(path):
    info = manifest()
    args = [sys.executable, 'tools/jgc_convert.py', str(path), '--motions=2',
            '--source-material=1', '--source-environment=1']
    for r in info['models']:
        s = info['sources'][r['variant']]
        args.append(f"{r['name']}:{s['data']}:{s['textures']}:0,0,0,0:rawat:{BASE:x}:"
                    f"{r['root']:x}:" + ','.join(f'{a:x}' for a in r['motions']) +
                    ':texmap=' + ','.join(map(str, r['texture_map'])))
    subprocess.run(args, cwd=DEC, check=True)
    return info

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true'); args = ap.parse_args()
    path = ROOT / 'psp/heliwreck.jgc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-heli-wreck-') as tmp:
            out = Path(tmp) / path.name; info = export(out)
            assert out.read_bytes() == path.read_bytes()
            assert json.loads(path.with_suffix('.json').read_text()) == info
    else:
        info = export(path)
        path.with_suffix('.json').write_text(json.dumps(info, indent=2) + '\n')

if __name__ == '__main__': main()
