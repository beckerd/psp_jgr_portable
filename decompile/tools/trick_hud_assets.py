#!/usr/bin/env python3
"""Extract 05b580 names, PLAY section-4 trick IDs and 04fb64 multiplier art."""
from pathlib import Path
import argparse
import struct

ROOT = Path(__file__).resolve().parents[2]

def generate():
    exe = (ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    game = (ROOT/'decompile/afs/U_GAME/000_gamedispdat.bin').read_bytes()
    u = lambda a: struct.unpack_from('<I', exe, a-0x8c010000)[0]
    v = lambda a: struct.unpack_from('<I', game, a-0x8c980000)[0]
    def string(a):
        b = exe[a-0x8c010000:].split(b'\0')[0]
        return '"'+''.join('\\%03o' % x for x in b)+'"'
    lines = ['/* Generated from original US executable and PLAY; language 1. */']
    for name,a,n in [('POINTS',0x8c183170,20),('BONUS',0x8c1831c0,16)]:
        lines.append('static const int TRICK_'+name+'['+str(n)+']={'+','.join(str(u(a+i*4)) for i in range(n))+'};')
    lines.append('static const char *const TRICK_NAMES[7][30]={')
    for a,n in [(0x8c1833fc,15),(0x8c183414,15),(0x8c18342c,30),(0x8c18342c,30),
                (0x8c18345c,1),(0x8c183474,1),(0x8c183444,1)]:
        lines.append('    {'+','.join(string(u(u(a+4)+i*4)) for i in range(n))+'},')
    lines += ['};','static const char TRICK_SUFFIX[]='+string(u(0x8c183200))+';']
    models = (4,6,3,2,1,7,10,0,15,8,13,11,12,5,9,14)
    specs = (ROOT/'decompile/chars_spec.txt').read_text().splitlines()
    for name,offset,n,limit in [('GRIND_IDS',12,6,15),('AIR_IDS',60,4,30)]:
        lines.append('static const unsigned char TRICK_'+name+'[16][2]['+str(n)+']={')
        for model in models:
            label,path,*_ = specs[model].split(':')
            data = (ROOT/'decompile'/path).read_bytes()
            read = lambda at: struct.unpack_from('<I',data,at)[0]
            at = read(4);sections = {}
            while read(at): sections.setdefault(read(at),read(at+4));at+=8
            values = struct.unpack_from('<'+str(n*2)+'I',data,sections[4]+offset)
            assert all(x<limit for x in values), (label,values)
            lines.append('    {'+','.join('{'+','.join(map(str,values[j:j+n]))+'}' for j in (0,n))+'}, /* '+label+' */')
        lines.append('};')
    # 182a6c[font=1][sign=0], same GAME slots as the existing HUD digits.
    header = v(u(u(0x8c182a6c+4)))
    first,n = v(header),v(header+4)
    assert struct.unpack_from('<3fi2f',game,header+8-0x8c980000)==(0,0,0,0,1,1)
    art = ['/* Original 04fb64 font-1 sign-0 list. */','static const HudEntry TRICK_MULTIPLY[]={']
    for i in range(n):
        slot,pad,x,y,w,h,rot,z,pu,pv,*tail=struct.unpack_from('<HH4fhh2f4I4f',game,first+i*64-0x8c980000)
        assert slot in (1,2) and pad==9600 and rot==0 and pu==pv==.5
        assert tail[:4]==[0xffffffff]*4
        art.append('    {'+str(slot-1)+','+','.join(float(x).hex()+'f' for x in (x,y,w,h,*tail[4:]))+'},')
    return '\n'.join(lines+['']), '\n'.join(art+['};',''])

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    for name,data in zip(('trick_hud_tables.inc','trick_hud_entries.inc'),generate()):
        path=ROOT/'psp/src'/name
        if args.check: assert path.read_text()==data,name
        else: path.write_text(data)
