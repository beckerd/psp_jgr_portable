#!/usr/bin/env python3
"""Recover all five city sky banks from their original 0c34e8 bindings.

Keep untextured vertex-colour domes: the architecture converter's no-UV
filter is not valid for these independently owned background tasks.
No geometry, texture, diffuse colour or material is synthesized here.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from staff_sky_assets import export as export_staff

ROOT=Path(__file__).resolve().parents[2]
DEC=ROOT/'decompile'
# Slot order and nulls are from the unmodified original loader in Flycast;
# source constructor sites: 02f5a4,039af0,031aa8,0f346c,0f3aac.
SKIES={
    1:((0x8cce33c0,0),(0x8cce2ebc,0x8c19de5c),(0,0)),
    2:((0x8cc93a10,0),(0x8cc93634,0x8c1ad95c),(0x8cc93454,0x8c1ad944)),
    3:((0x8ccc5af4,0x8c1c2c54),(0,0),(0x8ccc7b98,0x8c1c2c54)),
    5:((0,0),(0,0),(0x8cc079a4,0)),
    6:((0x8cc352a4,0),(0,0),(0x8cc34ec8,0x8c21968c)),
}
TABLES={1:(0x8c105e48,[0x8cce33c0,0x8cce2ebc]),
        2:(0x8c107cd4,[0x8cc93a10,0x8cc93634,0x8cc93454]),
        3:(0x8c1bc6cc,[0x8ccc7b98,0x8ccc5af4]),
        5:(0x8c20414c,[0x8cc079a4]),
        6:(0x8c20b65c,[0x8cc352a4,0x8cc34ec8])}
def mapping(stage):
    return json.loads((DEC/('stage%d_texmap%s.json'%(stage,'_static' if stage==1 else ''))).read_text())
def export(stage,path):
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    table,roots=TABLES[stage]
    assert list(struct.unpack_from('<%dI'%len(roots),exe,table-0x8c010000))==roots
    if stage==1:
        export_staff(path)
        return
    tm=mapping(stage);pack=tm['packs'][0]
    # Every city sky texture is in the common pack, not an area overlay.
    count=(DEC/pack).read_bytes().count(b'GBIX')
    data=next((DEC/f'afs/STAGE{stage}').glob('000_*')).relative_to(DEC)
    args=[sys.executable,str(DEC/'tools/jgc_convert.py'),str(path),
          '--motions=0','--source-material=1','--nolight=1']
    for slot,(root,texlist) in enumerate(SKIES[stage]):
        if not root:continue
        textures=tm['texlists'][str(texlist)] if texlist else [0]
        assert all(0<=i<count for i in textures)
        args.append(f'sky{stage}_{slot}:{data}:{pack}:0,0,0,0:rawat:8cb00000:{root:x}:0:'
                    'texmap='+','.join(map(str,textures)))
    subprocess.run(args,cwd=DEC,check=True)
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check',action='store_true')
    ap.add_argument('--stages',type=int,nargs='+',default=list(SKIES),choices=list(SKIES))
    a=ap.parse_args()
    for stage in a.stages:
        output=ROOT/'psp'/('staffsky.jgc' if stage==1 else f'sky{stage}.jgc')
        if a.check:
            with tempfile.TemporaryDirectory(prefix='jgr-city-sky-') as tmp:
                p=Path(tmp)/output.name;export(stage,p)
                assert p.read_bytes()==output.read_bytes(),output
        else:export(stage,output)
if __name__=='__main__':main()
