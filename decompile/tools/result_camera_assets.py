#!/usr/bin/env python3
"""01f04c mode 1: original PLAY +88 clear-camera records for every skater."""
import argparse,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
MODELS=(4,6,3,2,1,7,10,0,15,8,13,11,12,5,9,14)

def build():
    rows=[];assets={}
    spec=(ROOT/'decompile/chars_spec.txt').read_text().splitlines()
    for character,model in enumerate(MODELS):
        source=spec[model].split(':')[1];data=(ROOT/'decompile'/source).read_bytes()
        u=lambda p:struct.unpack_from('<I',data,p)[0]
        assert data[:4]==b'PLAY' and u(u(8))==character
        table=u(u(8)+0x88);record=u(table);initial,track=u(record),u(record+4);md=u(track)
        assert u(track+8)==0x401c1
        spans=[(initial,64),(track,12),(md,32),(record,8)]
        for ch in range(4):spans.append((u(md+ch*4),u(md+16+ch*4)*(16 if ch<2 else 8)))
        low=min(p for p,n in spans);high=max(p+n for p,n in spans)
        assert low>0 and high<=len(data) and high%4==0
        # Preserve original relative addresses; append a one-entry pointer
        # table after the original image slice, without rewriting any keys.
        image=b'SPC1'+struct.pack('<3I',low,high,1)+data[low:high]+struct.pack('<I',record)
        name=f'resultcam{character:02d}.bin';assets[name]=image
        rows.append(dict(character=character,source=source,source_sha256=hashlib.sha256(data).hexdigest(),
                         record=record,initial=initial,track=track,frames=u(track+4),bytes=len(image),asset=name))
    return assets,rows

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');a=p.parse_args()
    assets,rows=build();assets['resultcam.json']=(json.dumps(rows,indent=2)+'\n').encode()
    for name,data in assets.items():
        path=ROOT/'psp'/name
        if a.check:assert path.read_bytes()==data,path
        else:path.write_bytes(data)
    print('16 original victory cameras verified' if a.check else '16 original victory cameras exported')
if __name__=='__main__':main()
