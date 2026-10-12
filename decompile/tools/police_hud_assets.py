#!/usr/bin/env python3
"""Original 049c7c signs plus 049750 direction arrow, sparse NJS motions."""
from pathlib import Path
import argparse
import struct
import subprocess
import sys
import tempfile
from ninjalib import read_object
ROOT=Path(__file__).resolve().parents[2]
def bake(out):
    data=(ROOT/'decompile/afs/U_GAME/000_gamedispdat.bin').read_bytes()
    word=lambda off:struct.unpack_from('<I',data,off)[0]
    rows=[];ranges=[];motions=[]
    for level,(obj,mot,frames) in enumerate(((0x54,0x58,30),(0x60,0x64,60),(0x68,0x6c,60),(0x48,0x4c,30))):
        root,motion=word(obj),word(mot)
        assert word(motion-0x8c980000+4)==frames
        # These are ordinary sparse NJS_MDATA3, NOT Smilebit dense s16
        # channels. Keep the original bytes for scene_sample_njs(cel=0).
        nodes=[]
        def walk(a):
            while a:
                n=read_object(data,a,0x8c980000);nodes.append(n)
                walk(n['child']);a=n['sib']
        walk(root);assert len(nodes)<=64
        md=word(motion-0x8c980000);assert word(motion-0x8c980000+8)==0x30007
        ranges.extend(((motion,motion+12),(md,md+len(nodes)*24)));motions.append(motion)
        for i in range(len(nodes)):
            p=struct.unpack_from('<6I',data,md-0x8c980000+i*24)
            for ch in range(3):
                if p[ch]:ranges.append((p[ch],p[ch]+p[ch+3]*16))
        rows.append(f'PoliceSign{level}:afs/U_GAME/000_gamedispdat.bin:afs/U_GAME/001_gamedisptxr.bin:0,0,0,0:rawat:8c980000:{root:x}:0'+(':lighting=1' if level==3 else ''))
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out),'--nolight=1','--source-material=1','--source-environment=1',*rows],cwd=ROOT/'decompile',check=True)
    base=min(r[0] for r in ranges);end=max(r[1] for r in ranges)
    assert 0x8c980000<=base<end<=0x8c980000+len(data)
    out.with_suffix('.bin').write_bytes(struct.pack('<4s6I',b'PHM2',base,end-base,*motions)+data[base-0x8c980000:end-0x8c980000])
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    target=ROOT/'psp/policehud.jgc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-police-hud-') as tmp:
            out=Path(tmp)/'policehud.jgc';bake(out)
            for suffix in ('.jgc','.bin'):assert out.with_suffix(suffix).read_bytes()==target.with_suffix(suffix).read_bytes()
    else:bake(target)
if __name__=='__main__':main()
