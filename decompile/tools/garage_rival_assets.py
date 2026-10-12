#!/usr/bin/env python3
"""Import 0240bc's original untextured HERE COMES A RIVAL!! chunk model."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
DEC=ROOT/'decompile'
def source():
    path=DEC/'afs/U_GARAGE/001_garagedat.bin'
    data=path.read_bytes()
    root=struct.unpack_from('<I',data,0x1c0)[0]
    assert root==0x8c81099c
    return root,dict(source=str(path.relative_to(DEC)),sha256=hashlib.sha256(data).hexdigest(),
        pointer_cell='8c8001c0',root=f'{root:08x}',draw='8c0240bc',
        translation=[0,0,-2],scale=0.05249999836087227,textures=0)
def bake():
    root,meta=source()
    subprocess.run([sys.executable,'tools/jgc_convert.py','../psp/garage_rival.jgc',
        '--motions=0','--nolight=1','--source-material=1',
        'RivalBanner:afs/U_GARAGE/001_garagedat.bin:afs/U_GARAGE/002_garagetxr.bin:'
        f'0,0,0,0:rawat:8c800000:{root:x}:0'],cwd=DEC,check=True)
    bank=ROOT/'psp/garage_rival.jgc'
    meta['output_sha256']=hashlib.sha256(bank.read_bytes()).hexdigest()
    (ROOT/'psp/garage_rival_assets.json').write_text(json.dumps(meta,indent=2)+'\n')
if __name__=='__main__':bake()
