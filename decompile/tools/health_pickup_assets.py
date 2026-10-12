#!/usr/bin/env python3
"""Original 0db430 health meshes/motions, also used by 0df000 and 0ed638/668."""
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

def bake(out):
    exe = (ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    txr = (ROOT/'decompile/afs/PLAYER/017_player_effect_txr.bin').read_bytes()
    u = lambda offset: struct.unpack_from('<I',txr,offset)[0]
    assert txr[:4] == b'EFTX'
    lists={};offset=u(4)
    while u(offset):
        lists[u(offset)]=u(offset+4);offset+=8
    rows = []
    for name, slot, root, motion in (
        ('HealthSmall', 40, 0x8c8e251c, 0x8c8ed6e8),
        ('HealthLarge', 18, 0x8c8e200c, 0x8c8ed620),
    ):
        assert struct.unpack_from('<I', exe, 0x182fe8-0x10000+slot*4)[0] == root
        assert struct.unpack_from('<I', exe, 0x18308c-0x10000+slot*4)[0] == motion
        # 0c7ea8 chooses EFTX slot 40/18, NOT PVR 40/18. Both are
        # one-entry lists pointing to the shared first PVR atlas.
        texlist=u(u(12)+slot*4)
        assert u(texlist+4)==1 and u(lists[texlist])==txr.find(b'GBIX')
        rows.append(f'{name}:afs/PLAYER/016_player_effect_dat.bin:afs/PLAYER/017_player_effect_txr.bin:0,0,0,0:rawat:8c8e1000:{root:x}:{motion:x}:tex=0:lighting=1')
    subprocess.run([sys.executable, 'tools/jgc_convert.py', str(out),
        '--nolight=1', '--source-material=1', '--source-environment=1', *rows],
        cwd=ROOT/'decompile', check=True)

if __name__ == '__main__':
    bake(ROOT/'psp/healthpickups.jgc')
