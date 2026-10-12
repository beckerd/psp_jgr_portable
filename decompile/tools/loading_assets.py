#!/usr/bin/env python3
"""Task 0b / 01733c: unchanged LOAD models and dense Smilebit motions.

The screen uses vertex-coloured geometry, not a replacement loading bitmap.
The resident pointers at 1c9188..1c9194 select these two original trees.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def bake(out):
    ex=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    data=(ROOT/'decompile/afs/LOAD/000_load.bin').read_bytes()
    pointers=struct.unpack_from('<4I',ex,0x1b9188)
    specs=[]
    for i in range(2):
        obj,motion=pointers[i*2:i*2+2]
        assert struct.unpack_from('<HH',data,motion-0x8cf70000)==(0x15,101)
        specs.append(f'Loading{i}:afs/LOAD/000_load.bin:afs/U_GAME/001_gamedisptxr.bin:0,0,0,0:rawat:8cf70000:{obj:x}:{motion:x}:lighting=1')
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out),'--source-material=1','--source-environment=1',*specs],cwd=ROOT/'decompile',check=True)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    target=ROOT/'psp/loading.jgc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-loading-') as tmp:
            out=Path(tmp)/target.name;bake(out);assert out.read_bytes()==target.read_bytes()
    else:bake(target)
if __name__=='__main__':main()
