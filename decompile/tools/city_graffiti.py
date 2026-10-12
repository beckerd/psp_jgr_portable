#!/usr/bin/env python3
"""Original static-city wall geometry/reveal graphs, in SPT6 descriptor order.

CWT1 <magic,count>, then <record_bytes>, GfoTemplate, JgcBone tree,
GfoVertex triangles and original per-triangle <texture,flags,diffuse>.
No rescaled quads, guessed UVs or per-spot art selection. 03e3cc/03e540
select the player's small/medium/large texlist by descriptor kind.
"""
import argparse
from pathlib import Path
import struct
from jgs_spots import TABLES, descriptors
from object_graffiti import mesh

ROOT=Path(__file__).resolve().parents[2]
BASE=0x8cb00000

def convert(stage):
    data=(ROOT/f'decompile/afs/STAGE{stage}/000_st{stage}dat.bin').read_bytes()
    rows=[]
    for area,index,a in descriptors(data,TABLES[stage]):
        materials=[];flags,bones,vertices=mesh(data,a,BASE,materials)
        kind=struct.unpack_from('<I',data,a-BASE)[0]
        assert 1<=kind<=3 and len(materials)*3==len(vertices)
        assert all(tex in (0,1) and colour>>24==255 for tex,_,colour in materials)
        row=struct.pack('<8I',stage,area,index,a,kind,len(vertices),flags,len(bones))+data[a-BASE+4:a-BASE+0xe8]
        row+=b''.join(struct.pack('<hH3f3i3f2I',*b) for b in bones)
        row+=b''.join(struct.pack('<2fI3fI',*v) for v in vertices)
        row+=b''.join(struct.pack('<3I',*m) for m in materials)
        rows.append(struct.pack('<I',len(row)+4)+row)
    return struct.pack('<4sI',b'CWT1',len(rows))+b''.join(rows)

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    for stage in TABLES:
        data=convert(stage);path=ROOT/f'psp/walltags{stage}.bin'
        if args.check:assert path.read_bytes()==data
        else:path.write_bytes(data)
        print(f'Stage {stage}: {struct.unpack_from("<I",data,4)[0]} original wall meshes, {len(data)} bytes')
