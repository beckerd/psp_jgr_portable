#!/usr/bin/env python3
"""Original PLAYER camera records used by 01eb80, with source pointers intact."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
ROOT=Path(__file__).resolve().parents[2]
BASE=0x8c8e1000
FIRST=0x8c8f6704
TABLE=0x8c8fa0c0
def build():
    source=ROOT/'decompile/afs/PLAYER/016_player_effect_dat.bin'
    data=source.read_bytes();u=lambda a:struct.unpack_from('<I',data,a-BASE)[0]
    end=TABLE+17*4;rows=[]
    for style in range(17):
        rec=u(TABLE+style*4);initial,track=u(rec),u(rec+4);md=u(track)
        assert u(track+8)==0x401c1
        assert FIRST<=initial and initial+64<=end
        assert FIRST<=rec and rec+8<=end and FIRST<=md and md+32<=end
        for ch in range(4):
            p,n=u(md+ch*4),u(md+16+ch*4)
            assert n>0 and FIRST<=p and p+n*(16 if ch<2 else 8)<=end
        rows.append(dict(style=style,record=rec,initial=initial,track=track,frames=u(track+4)))
    image=b'SPC1'+struct.pack('<3I',FIRST,TABLE,17)+data[FIRST-BASE:end-BASE]
    info=dict(source=str(source.relative_to(ROOT/'decompile')),source_sha256=hashlib.sha256(data).hexdigest(),
              base=BASE,first=FIRST,table=TABLE,styles=rows,bytes=len(image))
    return image,info
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    image,info=build();p=ROOT/'psp/spraycam.bin';j=p.with_suffix('.json')
    if args.check:assert p.read_bytes()==image and json.loads(j.read_text())==info
    else:p.write_bytes(image);j.write_text(json.dumps(info,indent=2)+'\n')
if __name__=='__main__':main()
