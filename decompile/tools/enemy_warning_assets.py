#!/usr/bin/env python3
"""04471c / 044988: original PLAYER slot 14 danger/report indicator."""
from pathlib import Path
import argparse,struct,subprocess,sys,tempfile

ROOT=Path(__file__).resolve().parents[2]

def bake(out):
    exe=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    data=(ROOT/'decompile/afs/PLAYER/016_player_effect_dat.bin').read_bytes()
    root=struct.unpack_from('<I',exe,0x182fe8-0x10000+14*4)[0]
    motion=struct.unpack_from('<I',exe,0x18308c-0x10000+14*4)[0]
    assert 0x8c8e1000<=root<0x8c8e1000+len(data)
    assert struct.unpack_from('<H',data,motion-0x8c8e1000)[0]==0x15
    # Camera-space X reflection reverses winding. This flat indicator must
    # remain visible from its reflected face when upright.
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out),'--nolight=1',
        '--source-material=1','--source-environment=1',
        f'Danger:afs/PLAYER/016_player_effect_dat.bin:afs/PLAYER/017_player_effect_txr.bin:0,0,0,0:rawat:8c8e1000:{root:x}:{motion:x}:cull=none'],
        cwd=ROOT/'decompile',check=True)

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    out=ROOT/'psp/enemywarning.jgc'
    if a.check:
        with tempfile.TemporaryDirectory(prefix='jgr-warning-') as t:
            p=Path(t)/out.name;bake(p);assert p.read_bytes()==out.read_bytes()
    else:bake(out)
