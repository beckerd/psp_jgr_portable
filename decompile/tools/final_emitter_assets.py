#!/usr/bin/env python3
"""Export 0ebd00's original eight meshes and animated texture bindings."""
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
    data=(DEC/'afs/STAGELAST/001_lastdat.bin').read_bytes()
    textures=(DEC/'afs/STAGELAST/000_lasttxr.bin').read_bytes()
    word=lambda a:struct.unpack_from('<I',exe,a-0x8c010000)[0]
    offsets={};at=0
    while (at:=textures.find(b'GBIX',at))>=0:
        p=at+8+struct.unpack_from('<I',textures,at+4)[0]
        assert textures[p:p+4]==b'PVRT'
        offsets[at]=len(offsets);at+=8
    texlist=word(0x8c1d0ee8);a=0x8c1d0cb8
    while word(a) and word(a)!=texlist:a+=8
    assert word(a)==texlist and word(texlist+4)==8
    table=word(a+4)
    mapping=[offsets[word(table+4*i)-0x8c800000] for i in range(8)]
    roots=[word(0x8c1d0eec+4*i) for i in range(8)]
    assert all(0x8cb00000<=p<0x8cb00000+len(data) for p in roots)
    return dict(roots=roots,texlist=texlist,texture_map=mapping,
        compact_map=[sorted(mapping).index(i) for i in mapping],
        source_sha256=hashlib.sha256(data).hexdigest(),
        textures_sha256=hashlib.sha256(textures).hexdigest(),
        draw='0ebd00: trail[variant+side], head[side+(contact ? 0:2)]',
        scope='Original meshes/materials and eight source texture frames; no substitute sprites')

def export(path):
    info=manifest()
    args=[sys.executable,str(DEC/'tools/jgc_convert.py'),str(path),
          '--motions=0','--source-material=1','--nolight=1',
          '--keep-textures='+','.join(map(str,info['texture_map']))]
    for i,root in enumerate(info['roots']):
        args.append(f"final_emitter_{i}:afs/STAGELAST/001_lastdat.bin:afs/STAGELAST/000_lasttxr.bin:0,0,0,0:rawat:8cb00000:{root:x}:0:"
                    +'texmap='+','.join(map(str,info['texture_map'])))
    subprocess.run(args,cwd=DEC,check=True)
    return info

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check',action='store_true');args=ap.parse_args()
    output=ROOT/'psp/finalemitter.jgc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-final-emitter-assets-') as tmp:
            p=Path(tmp)/output.name;info=export(p)
            assert p.read_bytes()==output.read_bytes()
            assert json.loads(output.with_suffix('.json').read_text())==info
    else:
        info=export(output)
        output.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n')

if __name__=='__main__':main()
