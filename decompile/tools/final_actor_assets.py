#!/usr/bin/env python3
"""Source-bound final boss/satellite art. Export is not boss-logic execution.

0ece24/0ecfd8 bind these original texlists, object roots and CNK motions.
All source textures stay resident: 0ecaac rotates the satellite texlist.
No guessed roots, fabricated geometry or replacement animations.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
DEC=ROOT/'decompile'
def manifest():
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    dat=(DEC/'afs/STAGELAST/001_lastdat.bin').read_bytes()
    txr=(DEC/'afs/STAGELAST/000_lasttxr.bin').read_bytes()
    def u(a):return struct.unpack_from('<I',exe,a-0x8c010000)[0]
    offsets={};at=0
    while (at:=txr.find(b'GBIX',at))>=0:offsets[at]=len(offsets);at+=8
    lists={};address=0x8c1d0cb8
    while u(address):
        tl,ptr=u(address),u(address+4)
        lists[tl]=[offsets[u(ptr+k*4)-0x8c800000] for k in range(u(tl+4))]
        address+=8;assert address<0x8c1d0db0
    definitions=[
        ('goji',0x8c1d0e84,0x8c1d0e88,list(range(0x8c1d0e8c,0x8c1d0ea0,4)),0),
        # Each orbit draws BOTH model parts. The left and right instances
        # select eb4 and eb8 respectively; they are not one clip per model.
        ('orbit_part_a',0x8c1d0ea4,0x8c1d0eac,[0x8c1d0eb4,0x8c1d0eb8],0),
        ('orbit_part_b',0x8c1d0ea8,0x8c1d0eb0,[0x8c1d0eb4,0x8c1d0eb8],2),
        ('satellite_root',0x8c1d0ebc,0x8c1d0ec4,[0x8c1d0ecc],2),
        ('satellite_0',0x8c1d0ec0,0x8c1d0ec8,[],0),
        ('final_root',0x8c1d0ed4,0x8c1d0edc,[],2),
        ('satellite_5',0x8c1d0ed8,0x8c1d0ee0,[],0),
        # 0ed598 draws this extra central shell at the SAME bone-63 matrix
        # used by the final boss. It is not either of the two boss roots.
        ('central_final_attachment',0x8c1d0e7c,0x8c1d0e80,[],0),
    ]
    rows=[]
    for name,ta,ra,ma,mode in definitions:
        root=u(ra);motions=[u(a) for a in ma];tl=u(ta)
        assert 0x8cb00000<=root<0x8cb00000+len(dat)-52
        headers=[struct.unpack_from('<I',dat,m-0x8cb00000)[0] for m in motions]
        assert all((h&65535) in (0x11,0x15) for h in headers)
        rows.append(dict(name=name,root=root,texlist=tl,texture_map=lists[tl],motions=motions,frames=[h>>16 for h in headers],draw_mode=mode))
    return dict(models=rows,texture_count=len(offsets),source_sha256=hashlib.sha256(dat).hexdigest(),textures_sha256=hashlib.sha256(txr).hexdigest(),
        runtime_enabled='development arena and explicit phase-five inspection only',
        lighting='JGC3: unbaked material/local normals; mode 0 cel 2956e0, mode 2 normal 2337b4',
        remaining=['shared campaign RNG and sequenced/spatial object audio','complete spray/HUD graph','matched source render-state and lighting visual audit'])
def export(path):
    info=manifest();args=[sys.executable,str(DEC/'tools/jgc_convert.py'),str(path),'--motions=5','--source-material=1','--source-environment=1','--keep-textures=all']
    for r in info['models']:
        motions=','.join(f'{a:x}' for a in r['motions']) or '0'
        args.append(f"{r['name']}:afs/STAGELAST/001_lastdat.bin:afs/STAGELAST/000_lasttxr.bin:0,0,0,0:rawat:8cb00000:{r['root']:x}:{motions}:texmap="+','.join(map(str,r['texture_map']))+(':lighting=2' if r['draw_mode']==0 else ':lighting=1'))
    subprocess.run(args,cwd=DEC,check=True)
    return info
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    output=ROOT/'psp/finalactors.jgc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-final-actors-') as tmp:
            p=Path(tmp)/output.name;info=export(p)
            assert p.read_bytes()==output.read_bytes()
            assert json.loads(output.with_suffix('.json').read_text())==info
    else:
        info=export(output);output.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n')
if __name__=='__main__':main()
