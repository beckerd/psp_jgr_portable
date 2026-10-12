#!/usr/bin/env python3
"""Original 0ddbac explosion pairs selected through 06fa40(31)/1b5780."""
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
DAT='afs/ENEMY/045_effectdat.bin'
TXR='afs/ENEMY/044_effecttxr.bin'
BASE=0x8cff0000
def manifest():
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    data=(DEC/DAT).read_bytes();txr=(DEC/TXR).read_bytes()
    def u(a):return struct.unpack_from('<I',exe,a-0x8c010000)[0]
    table=u(0x8c1b5a60+16);offsets={};p=0
    while (p:=txr.find(b'GBIX',p))>=0:offsets[p]=len(offsets);p+=8
    bindings={};p=0x8c1b9590
    while u(p):bindings[u(p)]=u(p+4);p+=8
    rows=[]
    for i in range(4):
        root,texlist,motion=[u(u(table+i*12+j*4)) for j in range(3)]
        count=u(texlist+4);addresses=bindings[texlist]
        texmap=[offsets[u(addresses+j*4)-0x8c800000] for j in range(count)]
        kind,md=struct.unpack_from('<2I',data,motion-BASE);assert kind&65535==0x15
        rows.append(dict(root=root,texlist=texlist,motion=motion,frames=kind>>16,texture_map=texmap))
    assert [r['frames'] for r in rows]==[79,15,29,14]
    return dict(source=DAT,textures=TXR,source_sha256=hashlib.sha256(data).hexdigest(),textures_sha256=hashlib.sha256(txr).hexdigest(),models=rows)
def export(path):
    info=manifest();args=[sys.executable,'tools/jgc_convert.py',str(path),'--motions=1','--source-material=1','--source-environment=1']
    for i,r in enumerate(info['models']):
        args.append(f"SceneExplosion{i}:{DAT}:{TXR}:0,0,0,0:rawat:{BASE:x}:{r['root']:x}:{r['motion']:x}:texmap="+','.join(map(str,r['texture_map'])))
    subprocess.run(args,cwd=DEC,check=True);return info
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args();path=ROOT/'psp/scenefx.jgc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-scene-fx-') as tmp:
            p=Path(tmp)/path.name;info=export(p);assert p.read_bytes()==path.read_bytes();assert json.loads(path.with_suffix('.json').read_text())==info
    else:
        info=export(path);path.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n')
if __name__=='__main__':main()
