#!/usr/bin/env python3
"""Export original stage graffiti templates in mission (area,index) order.

SPT6: SPT5 row and anchors, then the original descriptor +4..58 (84 bytes),
camera-volume count and count * 176 original camera-volume bytes. The legacy
row's display origin remains +4; the GF interaction centre is descriptor +28.
The count is descriptor +f0, NOT inferred from tag size. Character stats
choose the gesture patterns; descriptor +e8 chooses spray animation styles.
Source: 03db94, 0c4ea4/0c4ab4, 03aedc/03b0fc.
"""
import argparse
import collections
from pathlib import Path
import struct

TABLES={1:0x8cddf04c,2:0x8cd9b4c8,3:0x8cb21d58,5:0x8cb0cdb0,6:0x8cb09708}

def descriptors(data, table):
    base=0x8cb00000
    def word(a):return struct.unpack_from('<I',data,a-base)[0]
    for area in range(6):
        p=word(table+4*area);index=0
        while p and word(p):
            a=word(p);assert base<=a<=base+len(data)-0x104
            yield area,index,a
            p+=4;index+=1

def templates(data, table):
    base=0x8cb00000
    def word(a):return struct.unpack_from('<I',data,a-base)[0]
    def floats(a,n):return struct.unpack_from('<'+'f'*n,data,a-base)
    for area,index,a in descriptors(data,table):
        kind=word(a);count=word(a+0xf0);points=word(a+0xec)
        assert 1<=kind<=3 and count<=7,(area,index,hex(a),kind,count)
        assert count==0 or base<=points<=base+len(data)-32*count
        yield (area,index,[float(kind),*floats(a+4,3),*floats(a+0x58,18),*floats(a+0xa0,18)],
               word(a+0xe8),data[points-base:points-base+32*count] if count else b'')

def convert(data,table):
    rows=list(templates(data,table));out=bytearray(struct.pack('<4sI',b'SPT6',len(rows)))
    for (area,index,values,mask,positions),(_,_,a) in zip(rows,descriptors(data,table)):
        out.extend(struct.pack('<II40fII',area,index,*values,mask,len(positions)//32));out.extend(positions)
        p,n=struct.unpack_from('<II',data,a-0x8cb00000+0xf4)
        assert n<=5 and (not n or 0x8cb00000<=p<=0x8cb00000+len(data)-176*n)
        out.extend(data[a-0x8cb00000+4:a-0x8cb00000+0x58]);out.extend(struct.pack('<I',n))
        if n:out.extend(data[p-0x8cb00000:p-0x8cb00000+176*n])
    return bytes(out)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source',type=Path);ap.add_argument('output',type=Path)
    ap.add_argument('--table',type=lambda s:int(s,0),required=True);args=ap.parse_args()
    data=args.source.read_bytes();rows=list(templates(data,args.table))
    args.output.write_bytes(convert(data,args.table))
    print(f'{args.source}: {len(rows)} spots; (type,strokes): '+str(dict(collections.Counter((int(r[2][0]),len(r[4])//32) for r in rows))))

if __name__=='__main__':main()
