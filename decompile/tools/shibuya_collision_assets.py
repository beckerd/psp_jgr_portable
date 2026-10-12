#!/usr/bin/env python3
"""Reproduce Shibuya world, placed-item and dynamic-prop collision geometry.

031018 and 057280 use Ninja 27a198; 0b6b1c uses 27a06c(r5=0).
Both rotate Z/Y/X. World-grid vertices are already in world coordinates.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
from shibuya_prop_types import SHIBUYA_KNOCKABLE_TYPES
ROOT=Path(__file__).resolve().parents[2]
def build(output, *, legacy_street_boxes=False):
    args=[sys.executable,'tools/jgs_collision.py','afs/STAGE1/000_st1dat.bin',str(output),
        '--grid=0x8cd76ad4','--items=0x8c106090','--sectors=63','--protos=0x8c10748c,54',
        '--dyn=0x8c10618c','--dynprotos=0x8c107644,28']
    # 057280 registers these type-4 nodes as nearby interaction bodies.
    # They are not permanent solid floor/wall strips. street_prop_assets.py
    # preserves their original boxes for the runtime owner instead.
    if not legacy_street_boxes:
        args.append('--dynexclude='+','.join(map(str,SHIBUYA_KNOCKABLE_TYPES)))
    subprocess.run(args,cwd=ROOT/'decompile',check=True,stdout=subprocess.DEVNULL)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='jgr-shibuya-collision-') as tmp:
        output=Path(tmp)/'stage1.col';build(output);data=output.read_bytes()
        target=ROOT/'psp/stage1.col'
        if args.check:assert target.read_bytes()==data
        else:target.write_bytes(data)
    print('Shibuya: 51,896 world + 6,836 placed-item triangles; 155 street interaction boxes owned separately')
if __name__=='__main__':main()
