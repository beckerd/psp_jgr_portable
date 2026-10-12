#!/usr/bin/env python3
"""Original stationary police models, clips and NJS_TEXLIST mappings.

The TXR file order is skid, van, beacon, car, damaged car. Source 0929bc
selects two explicit texture lists; sequential file-order slots are wrong.
"""
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]

def build(out):
    exe=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    def word(address):return struct.unpack_from('<I',exe,address-0x8c010000)[0]
    assert word(0x8c1b6260)==0x8ca042dc and word(0x8c1b626c)==0x8ca1a438
    assert word(0x8c1b6248)==0x8ca1a394  # stationary car; not suspension clip 0
    # Validate list extents; the converter reads the pointer values locally.
    assert word(0x8c1b8d24)==2 and word(0x8c1b8cd4)==2
    # 0aed90: Killer's parked Benz has no animation (+a4=0). +ac is a
    # collision tree, not a motion. Its single texture is TXR offset d80.
    assert [word(a) for a in (0x8c1b63ec,0x8c1b63f0,0x8c1b63fc)]==[0x8ca045d8,0x8ca0bdc8,0x8c1c99bc]
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out),'--source-material=1',
        'ParkedCar:afs/ENEMY/021_patcardat.bin:afs/ENEMY/020_patcartxr.bin:0,0,0,0:rawat:8ca00000:8ca08c78:8ca1a394:texbinding=1,8c010000,8c1b8d28,2,8ca00000,3',
        'PoliceVan:afs/ENEMY/021_patcardat.bin:afs/ENEMY/020_patcartxr.bin:0,0,0,0:rawat:8ca00000:8ca042dc:8ca1a438:texbinding=1,8c010000,8c1b8cd8,2,8ca00000,3',
        'ParkedBenz:afs/ENEMY/027_benzdat.bin:afs/ENEMY/026_benztxr.bin:0,0,0,0:rawat:8ca00000:8ca045d8:0:tex=1',
        'ParkedBenzFar:afs/ENEMY/027_benzdat.bin:afs/ENEMY/026_benztxr.bin:0,0,0,0:rawat:8ca00000:8ca0bdc8:0:tex=1'],
        cwd=ROOT/'decompile',check=True)

if __name__=='__main__':build(ROOT/'psp/policevehicles.jgc')
