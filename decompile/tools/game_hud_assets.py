#!/usr/bin/env python3
"""Original GAME HUD entries via 177fbc/182a38; no hand-cut atlas rectangles."""
import argparse
from pathlib import Path
import struct
from pvr_decode import decode

ROOT=Path(__file__).resolve().parents[2]
def generate():
    exe=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    data=(ROOT/'decompile/afs/U_GAME/000_gamedispdat.bin').read_bytes()
    txr=(ROOT/'decompile/afs/U_GAME/001_gamedisptxr.bin').read_bytes()
    u=lambda a:struct.unpack_from('<I',exe,a-0x8c010000)[0]
    v=lambda a:struct.unpack_from('<I',data,a-0x8c980000)[0]
    def entries(pointer):
        header=v(pointer);first,n=v(header),v(header+4)
        assert struct.unpack_from('<3fi2f',data,header+8-0x8c980000)==(0,0,0,0,1,1)
        return [struct.unpack_from('<HH4fhh2f4I4f',data,first+i*64-0x8c980000) for i in range(n)]
    rows=entries(0x8c980214)+entries(0x8c980228)+entries(0x8c98022c)
    assert len(rows)==6
    for font,n in enumerate((10,10,11)):
        for digit in range(n):
            row=entries(u(u(0x8c182a38+4*font)+4*digit));assert len(row)==1;rows+=row
    lines=['/* Generated from GAME raw 0x40-byte entries. Indices: frame 0..3,',
           '   bundle 4, single 5, font 0 at 6, font 1 at 16, timer font 2 at 26. */',
           'static const HudEntry HUD_ENTRIES[37]={']
    for r in rows:
        slot,pad,x,y,w,h,rot,z,pu,pv,*tail=r
        assert slot in (1,2) and pad==9600 and rot==0 and pu==pv==.5
        assert len(set(tail[:4]))==1 and tail[0]==0xffffffff
        f=lambda x:float(x).hex()+'f'
        lines.append('    {'+str(slot-1)+','+','.join(map(f,(x,y,w,h,*tail[4:])))+'},')
    lines+=['};','']
    texture=bytearray(struct.pack('<4sI',b'TEX1',2))
    for slot in (1,2):
        at=u(0x8c177fc4+4*slot)-0x8c800000;at=txr.index(b'PVRT',at,at+64)
        n=struct.unpack_from('<I',txr,at+4)[0];im=decode(txr[at:at+n+8]).convert('RGBA')
        w,h=im.size;assert w<=512 and h<=512
        texture+=struct.pack('<HH',w,h)
        for r,g,b,a in im.getdata():texture+=struct.pack('<H',(a>>4)<<12|(b>>4)<<8|(g>>4)<<4|(r>>4))
    return '\n'.join(lines).encode(),bytes(texture)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    for name,data in zip(('psp/src/game_hud_entries.inc','psp/gamehud.bin'),generate()):
        path=ROOT/name
        if args.check:assert path.read_bytes()==data,name
        else:path.write_bytes(data)
if __name__=='__main__':main()
