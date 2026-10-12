#!/usr/bin/env python3
"""Original 0e990c/0e9d54 launcher models, material bindings and CNK clips."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2];DEC=ROOT/'decompile'
def manifest():
    x=(DEC/'extracted/1ST_READ.BIN').read_bytes();d=(DEC/'afs/STAGELAST/001_lastdat.bin').read_bytes()
    txr=(DEC/'afs/STAGELAST/000_lasttxr.bin').read_bytes()
    u=lambda a:struct.unpack_from('<I',x,a-0x8c010000)[0]
    offsets={};at=0
    # This bank's first VQ PVRT length extends into the following textures.
    # Match the existing JGC decoder's GBIX enumeration, verifying headers.
    while (at:=txr.find(b'GBIX',at))>=0:
        p=at+8+struct.unpack_from('<I',txr,at+4)[0];assert txr[p:p+4]==b'PVRT'
        offsets[at]=len(offsets);at+=8
    lists={};a=0x8c1d0cb8
    while u(a):
        tl,ptr=u(a),u(a+4);lists[tl]=[offsets[u(ptr+k*4)-0x8c800000] for k in range(u(tl+4))];a+=8
    motions=[u(0x8c1d0e6c),u(0x8c1d0e70)]
    headers=[struct.unpack_from('<I',d,a-0x8cb00000)[0] for a in motions]
    assert headers==[0x00a00015,0x02580015]
    models=[dict(root=u(ra),texlist=u(ta),texture_map=lists[u(ta)],draw_mode=mode)
        for ra,ta,mode in [(0x8c1d0e68,0x8c1d0e64,0),(0x8c1d0e78,0x8c1d0e74,2)]]
    return dict(models=models,motions=motions,frames=[h>>16 for h in headers],
        lighting='JGC3: unbaked material/local normals; mode 0 cel 2956e0, mode 2 normal 2337b4',
        source_sha256=hashlib.sha256(d).hexdigest(),textures_sha256=hashlib.sha256(txr).hexdigest(),
        idle_positions=[struct.unpack_from('<3f',x,0x8c1dc118+i*12-0x8c010000) for i in range(2)],
        attack_positions=[struct.unpack_from('<3f',x,0x8c1dc130+i*12-0x8c010000) for i in range(4)],
        idle_angles=[u(0x8c1dc160+i*4) for i in range(2)],attack_angles=[u(0x8c1dc168+i*4) for i in range(4)],
        attachment=dict(bone=u(0x8c1dc190)&65535,count=u(0x8c1dc190)>>16,
            points=struct.unpack_from('<6f',x,u(0x8c1dc194)-0x8c010000)))
def export(path):
    info=manifest();args=[sys.executable,str(DEC/'tools/jgc_convert.py'),str(path),'--motions=2',
        '--source-material=1','--source-environment=1']
    for i,r in enumerate(info['models']):
        args.append(f"final_hazard_{i}:afs/STAGELAST/001_lastdat.bin:afs/STAGELAST/000_lasttxr.bin:0,0,0,0:rawat:8cb00000:{r['root']:x}:"
            +','.join(f'{m:x}' for m in info['motions'])+':texmap='+','.join(map(str,r['texture_map']))
            +':lighting='+('2' if r['draw_mode']==0 else '1'))
    subprocess.run(args,cwd=DEC,check=True);return info
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    output=ROOT/'psp/finalhazard.jgc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-final-hazard-') as tmp:
            p=Path(tmp)/output.name;info=export(p)
            assert p.read_bytes()==output.read_bytes()
            assert json.loads(output.with_suffix('.json').read_text())==json.loads(json.dumps(info))
    else:
        info=export(output);output.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n')
if __name__=='__main__':main()
