#!/usr/bin/env python3
"""Extract the US 0b23c4 mixer table and 02eabc BGM class jump table."""
from pathlib import Path
import argparse, struct
ROOT = Path(__file__).resolve().parents[2]
def generate():
    data = (ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    u = lambda a: struct.unpack_from('<I', data, a-0x8c010000)[0]
    h = lambda a: struct.unpack_from('<H', data, a-0x8c010000)[0]
    lines = ['/* Generated from US 1ST_READ: 0b23c4 / 1ccd04, 02eabc. */',
             'static const int AUDIO_MIX_TABLE[17][5]={']
    for i in range(17):
        row = struct.unpack_from('<5i', data, 0x1ccd04-0x10000+i*20)
        lines.append('    {'+','.join(map(str,row))+'},')
    lines += ['};','static const signed char AUDIO_BGM_CLASS[190]={']
    modes=[]
    for entry in range(190):
        pc=u(0x8c02eae8+(entry+1)*4)
        assert h(pc)&0xff00==0xd000  # mov.l literal,r0 = +80
        pc+=2
        if h(pc)&0xf000==0xa000:  # the class-3 branch over the pool
            d=h(pc)&4095;pc=pc+4+(d if d<2048 else d-4096)*2
        assert h(pc)&0xff00==0xe200, hex(pc)
        modes.append(h(pc)&255)
    for i in range(0,190,20):lines.append('    '+','.join(map(str,modes[i:i+20]))+',')
    return '\n'.join(lines+['};',''])
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    path=ROOT/'psp/src/audio_mix_tables.inc';text=generate()
    if a.check:assert path.read_text()==text
    else:path.write_text(text)
