#!/usr/bin/env python3
"""04e8d4 pickup HUD: original PLAYER effect object table slot 10."""
from pathlib import Path
import argparse
import struct
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def bake(out):
    exe=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    obj=struct.unpack_from('<I',exe,0x182fe8-0x10000+10*4)[0]
    assert obj==0x8c8e3288
    row=f'PickupCan:afs/PLAYER/016_player_effect_dat.bin:afs/PLAYER/017_player_effect_txr.bin:0,0,0,0:rawat:8c8e1000:{obj:x}:0:lighting=2'
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out),'--nolight=1','--source-material=1','--source-environment=1',row],cwd=ROOT/'decompile',check=True)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    target=ROOT/'psp/canhud.jgc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-canhud-') as tmp:
            generated=Path(tmp)/'canhud.jgc';bake(generated);assert generated.read_bytes()==target.read_bytes()
    else:bake(target)
if __name__=='__main__':main()
