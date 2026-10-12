#!/usr/bin/env python3
"""Shibuya's original 0c3568 sky pair, including the untextured colour dome."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2];DEC=ROOT/'decompile'
def export(path):
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    roots=struct.unpack_from('<2I',exe,0x8c105e48-0x8c010000)
    assert roots==(0x8cce33c0,0x8cce2ebc)
    args=[sys.executable,str(DEC/'tools/jgc_convert.py'),str(path),
          '--motions=0','--source-material=1','--nolight=1']
    for i,root in enumerate(roots):
        args.append(f'staff_sky_{i}:afs/STAGE1/000_st1dat.bin:afs/STAGE1TXP_ALL/000_texpack_st1_all_00.txp:'
                    f'0,0,0,0:rawat:8cb00000:{root:x}:0:tex=101')
    subprocess.run(args,cwd=DEC,check=True)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    output=ROOT/'psp/staffsky.jgc'
    if a.check:
        with tempfile.TemporaryDirectory(prefix='jgr-staff-sky-') as tmp:
            p=Path(tmp)/output.name;export(p);assert p.read_bytes()==output.read_bytes()
    else:export(output)
if __name__=='__main__':main()
