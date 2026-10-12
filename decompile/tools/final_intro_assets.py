#!/usr/bin/env python3
"""EVENT_0092 (descriptor 4), source-bound actor and both CNK motions.

The three camera tracks, dialogue and timing stay in the unmodified EVENT
image. This exporter does not synthesize choreography or select lookalikes.
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
BASE=0x8c900000

def manifest():
    source=DEC/'afs/EVENT/006_event_0092.bin'
    texture=DEC/'afs/EVENT/007_event_0092txr.bin'
    data=source.read_bytes();txr=texture.read_bytes()
    def u(a):
        assert BASE<=a<=BASE+len(data)-4 and not a&3,hex(a)
        return struct.unpack_from('<I',data,a-BASE)[0]
    def commands(p):
        for _ in range(1024):
            w=tuple(u(p+j*4) for j in range(5));yield w
            if w[0]==100:return
            p+=20
        raise ValueError('unterminated EVENT stream')
    offsets={};at=0
    while at<len(txr):
        assert txr[at:at+4]==b'GBIX';offsets[at]=len(offsets)
        p=at+8+struct.unpack_from('<I',txr,at+4)[0]
        assert txr[p:p+4]==b'PVRT';at=p+8+struct.unpack_from('<I',txr,p+4)[0]
    setup=list(commands(u(BASE)));main=list(commands(u(BASE+4)))
    table=next(w[1] for w in setup if w[0]==0x56);lists={}
    while u(table):
        tl,ptr=u(table),u(table+4);count=u(tl+4);assert 0<count<=256
        lists[tl]=[offsets[u(ptr+k*4)-0x8cda0000] for k in range(count)];table+=8
    actors=[w for w in main if w[0]==0x2d];assert len(actors)==1
    _,slot,root,tl,flags=actors[0];assert slot==0 and flags==2
    motions=[w[2] for w in main if w[0]==0x34 and w[1]==slot]
    assert len(motions)==2 and all(u(m)&65535==0x11 for m in motions)
    track=next(w[2] for w in main if w[0]==0x2f and w[1]==slot)
    return dict(descriptor=4,source=str(source.relative_to(DEC)),textures=str(texture.relative_to(DEC)),
        source_sha256=hashlib.sha256(data).hexdigest(),textures_sha256=hashlib.sha256(txr).hexdigest(),
        root=root,texlist=tl,flags=flags,texture_map=lists[tl],motions=motions,
        frames=[u(m)>>16 for m in motions],world_track=track,
        lighting='JGC3: unbaked material/local normals; cel path 2956e0',
        cameras=[dict(initial=w[1],track=w[2]) for w in main if w[0]==7])

def export(path):
    info=manifest()
    spec=f"final_intro:{info['source']}:{info['textures']}:0,0,0,0:rawat:8c900000:{info['root']:x}:"
    spec+=','.join(f'{m:x}' for m in info['motions'])+':texmap='+','.join(map(str,info['texture_map']))+':lighting=2'
    subprocess.run([sys.executable,str(DEC/'tools/jgc_convert.py'),str(path),'--motions=2',
        '--source-material=1','--source-environment=1',spec],cwd=DEC,check=True)
    return info

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    output=ROOT/'psp/scene0092.jgc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-final-intro-assets-') as tmp:
            p=Path(tmp)/output.name;info=export(p)
            assert p.read_bytes()==output.read_bytes()
            assert json.loads(output.with_suffix('.json').read_text())==info
    else:
        info=export(output);output.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n')
if __name__=='__main__':main()
