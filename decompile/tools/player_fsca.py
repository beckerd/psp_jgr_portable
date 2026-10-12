#!/usr/bin/env python3
"""Convert the local SH-4 CPU sine ROM data to a PSP read-only table.

Input is Flycast core/hw/sh4/fsca-table.h, containing the numeric CPU table,
not game code. Keep both positive quadrants: the hardware table is not exactly
symmetric. No emulator implementation is copied. Output via stdout.
"""
import re
import struct
import sys
from pathlib import Path

def generate(source):
    words=re.findall(r'0x([0-9a-fA-F]{8})',source)
    assert len(words)==32768
    values=[struct.unpack('<f',struct.pack('<I',int(w,16)))[0] for w in words]
    assert values[0]==0 and values[16384]==1
    lines=['/* SH-4 numeric FSCA ROM; converted by decompile/tools/player_fsca.py',
           '   from the local Flycast CPU table. Both positive quadrants are required. */',
           'static const float SH4_SIN[32768] = {']
    for i in range(0,len(values),8):
        lines.append('    '+','.join(v.hex()+'f' for v in values[i:i+8])+',')
    return '\n'.join(lines+['};',''])

if __name__=='__main__':
    print(generate(Path(sys.argv[1]).read_text()),end='')
