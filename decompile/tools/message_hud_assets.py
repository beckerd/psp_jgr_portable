#!/usr/bin/env python3
"""Task 56 message lists, task 22 notification text, and original numeric glyphs."""
import argparse
from pathlib import Path
import struct
ROOT=Path(__file__).resolve().parents[2]
def generate():
    b=(ROOT/'decompile/afs/U_GAME/000_gamedispdat.bin').read_bytes()
    ex=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    u=lambda a:struct.unpack_from('<I',b,a-0x8c980000)[0]
    lit=lambda s:'"'+''.join(f'\\x{v:02x}' for v in s)+'"'
    p=u(0x8c9801e4)
    strings=[b[u(p+i*4)-0x8c980000:].split(b'\0')[0] for i in (12,13)]
    digits=[ex[a-0x8c010000:a-0x8c010000+2] for a in struct.unpack_from('<11I',ex,0x163ba0)]
    text='/* Generated: GAME +1e4[12/13], executable 173ba0. */\n'
    text+='static const char *const HUD_NOTICE_TEXT[2]={'+','.join(map(lit,strings))+'};\n'
    text+='static const char HUD_DIGITS[11][3]={'+','.join(map(lit,digits))+'};\n'
    lines=['/* Generated original GAME +234/+238/+23c; texture slot 1. */',
           'static const MessageEntry MESSAGE_ENTRIES[]={'];counts=[]
    for cell in (0x234,0x238,0x23c):
        h=u(0x8c980000+cell);first,n=u(h),u(h+4);counts.append(n)
        assert struct.unpack_from('<3fi2f',b,h+8-0x8c980000)==(0,0,0,0,1,1)
        for i in range(n):
            slot,flags,x,y,w,ht,rotation,z,pu,pv,*tail=struct.unpack_from('<HH4fhh2f4I4f',b,first+i*64-0x8c980000)
            assert slot==1 and flags&~24==9600 and rotation==0 and pu==pv==.5
            assert len(set(tail[:4]))==1
            # Preserve offscreen authored entries; do not repair their coordinates.
            lines.append('    {'+str(flags&24)+','+hex(tail[0])+'u,'+','.join(v.hex()+'f' for v in (x,y,w,ht,*tail[4:]))+'},')
    lines+=['};','static const unsigned MESSAGE_RANGES[4]={0,'+','.join(str(sum(counts[:i])) for i in range(1,4))+'};','']
    return text.encode(),'\n'.join(lines).encode()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    for name,data in zip(('hud_text.inc','message_hud_entries.inc'),generate()):
        p=ROOT/'psp/src'/name
        if a.check:assert p.read_bytes()==data,name
        else:p.write_bytes(data)
    # 02d4c8 task 298 uses the English MUSEN archive, not DJTALK.
    sources=sorted((ROOT/'decompile/afs/E_SND_MUSEN').glob('*.adx'))
    assert len(sources)==37
    for ref,source in enumerate(sources):
        assert int(source.name.split('_')[0])==ref
        data=source.read_bytes();assert data[:12].hex()=='80000020031204010000ac44'
        p=ROOT/f'psp/messagevoice{ref:03d}.adx'
        if a.check:assert p.read_bytes()==data,p
        else:p.write_bytes(data)
if __name__=='__main__':main()
