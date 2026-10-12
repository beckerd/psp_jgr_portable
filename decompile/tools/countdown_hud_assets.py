#!/usr/bin/env python3
"""050260: original GAME 3/2/1/GO models selected by 182aa4, unanimated."""
from pathlib import Path
import argparse
import struct
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def bake(out):
    exe=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    data=(ROOT/'decompile/afs/U_GAME/000_gamedispdat.bin').read_bytes()
    rows=[]
    for i in range(4):
        cell=struct.unpack_from('<I',exe,0x8c182aa4-0x8c010000+i*4)[0]
        obj=struct.unpack_from('<I',data,cell-0x8c980000)[0]
        rows.append(f'Countdown{i}:afs/U_GAME/000_gamedispdat.bin:afs/U_GAME/001_gamedisptxr.bin:0,0,0,0:rawat:8c980000:{obj:x}:0:lighting=1')
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out),'--source-material=1','--source-environment=1',*rows],cwd=ROOT/'decompile',check=True)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    target=ROOT/'psp/countdown.jgc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-countdown-') as tmp:
            out=Path(tmp)/'countdown.jgc';bake(out);assert target.read_bytes()==out.read_bytes()
    else:bake(target)
    for ref,name in ((179,'go'),(180,'one'),(181,'three'),(182,'two')):
        source=ROOT/f'decompile/afs/SND_DJTALK/{ref:03d}_e29_{name}.adx'
        data=source.read_bytes();assert data[:12].hex()=='80000020031204010000ac44'
        path=ROOT/f'psp/countvoice{ref}.adx'
        if args.check:assert path.read_bytes()==data,path
        else:path.write_bytes(data)
if __name__=='__main__':main()
