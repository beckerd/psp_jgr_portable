#!/usr/bin/env python3
"""04165c/183130 original animated spray cues, using GAME's texlist 177fbc."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from pvr_decode import decode as pvr_decode
ROOT=Path(__file__).resolve().parents[2];DEC=ROOT/'decompile'
DAT='afs/PLAYER/016_player_effect_dat.bin'
TXR='afs/E_GAME/001_gamedisptxr.bin'
BASE=0x8c8e1000
def digits():
    """04fa80 type 1: original GAME yellow digits, not the score font."""
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes();data=(DEC/'afs/E_GAME/000_gamedispdat.bin').read_bytes()
    u=lambda a:struct.unpack_from('<I',exe,a-0x8c010000)[0]
    v=lambda a:struct.unpack_from('<I',data,a-0x8c980000)[0]
    lines=['/* 04fa80 / 182a3c, original GAME entries: width,height,u0,v0,u1,v1. */',
           'static const float SPRAY_DIGITS[10][6]={']
    for j in range(10):
        header=v(u(u(0x8c182a3c)+j*4));entry=v(header)-0x8c980000
        assert v(header+4)==1
        assert struct.unpack_from('<H',data,entry)[0]==2
        values=struct.unpack_from('<2f',data,entry+12)+struct.unpack_from('<4f',data,entry+48)
        lines.append('    {'+','.join(float(x).hex()+'f' for x in values)+'},')
    lines.append('};')
    txr=(DEC/TXR).read_bytes();at=u(0x8c177fcc)-0x8c800000
    at=txr.index(b'PVRT',at,at+64);size=struct.unpack_from('<I',txr,at+4)[0]
    im=pvr_decode(txr[at:at+size+8]).convert('RGBA');w,h=im.size;assert (w,h)==(128,128)
    out=bytearray(struct.pack('<4sIHH',b'TEX1',1,w,h))
    for r,g,b,a in im.getdata():out+=struct.pack('<H',(a>>4)<<12|(b>>4)<<8|(g>>4)<<4|(r>>4))
    return '\n'.join(lines)+'\n',bytes(out)
def export(path):
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes();data=(DEC/DAT).read_bytes();txr=(DEC/TXR).read_bytes()
    u=lambda a:struct.unpack_from('<I',exe,a-0x8c010000)[0]
    v=lambda a:struct.unpack_from('<I',data,a-BASE)[0]
    offsets=[];at=0
    while (at:=txr.find(b'GBIX',at))>=0:offsets.append(at);at+=8
    texmap=[offsets.index(u(0x8c177fc4+i*4)-0x8c800000) for i in range(u(0x8c177fc0))]
    rows=[];info=[]
    for mode in range(2):
        roots,motions=u(0x8c183130+mode*8),u(0x8c183134+mode*8)
        for cue in range(1,11):
            root,motion=v(roots+cue*4),v(motions+cue*4)
            info.append(dict(mode=mode,cue=cue,root=root,motion=motion,frames=v(motion)>>16))
            rows.append(f'SprayCue{mode}_{cue}:{DAT}:{TXR}:0,0,0,0:rawat:{BASE:x}:{root:x}:{motion:x}:lighting=1:texmap='+','.join(map(str,texmap)))
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(path),'--motions=1',
        '--source-material=1','--source-environment=1','--texmax=256',*rows],cwd=DEC,check=True)
    return dict(source=DAT,textures=TXR,texture_map=texmap,models=info,
        source_sha256=hashlib.sha256(data).hexdigest(),texture_sha256=hashlib.sha256(txr).hexdigest(),
        sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    path=ROOT/'psp/sprayhud.jgc'
    code,texture=digits()
    for target,data in [(ROOT/'psp/src/spray_digits.inc',code.encode()),(ROOT/'psp/spraydigits.bin',texture)]:
        if args.check:assert target.read_bytes()==data
        else:target.write_bytes(data)
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-spray-hud-assets-') as tmp:
            out=Path(tmp)/path.name;info=export(out);assert out.read_bytes()==path.read_bytes()
            assert info==json.loads(path.with_suffix('.json').read_text())
    else:
        info=export(path);path.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n')
if __name__=='__main__':main()
