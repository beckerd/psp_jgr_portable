#!/usr/bin/env python3
"""Translate the supplied reverb1.FPD DSP instructions, not a fitted echo.

The original live AICA COEF/MADRS/MPRO blocks match this SFPB at +54/+254/+454.
Only this program's instruction subset is emitted; unknown fields fail.
"""
from pathlib import Path
import argparse
import struct
ROOT=Path(__file__).resolve().parents[2]
def generate():
    b=(ROOT/'decompile/afs/E_SND_SE/079_test_fx.fpb').read_bytes()
    assert b[:4]==b'SFPB' and b[20:32]==b'reverb1.FPD\0'
    coef=struct.unpack_from('<128i',b,0x54)
    coef=[(x&65535)-65536 if x&32768 else x&65535 for x in coef]
    delay=struct.unpack_from('<64I',b,0x254)
    ops=[struct.unpack_from('<4I',b,0x454+16*i) for i in range(128)]
    assert all(not any(w) for w in ops[84:])
    lines=['/* Generated from original E_SND_SE/079_test_fx.fpb; reverb1.FPD. */',
           'static void source_reverb_program(SourceReverb *s,int input,int out[2])',
           '{','    int acc=0,shift=0,memval[4]={0};unsigned cursor=s->cursor;',
           '    (void)input;']
    written_temp=set()
    for i,(a,b,c,d) in enumerate(ops[:84]):
        tra=a>>9&127;twa=a>>1&127;ira=b>>7&63;ysel=b>>13&3;shift=c>>4&3
        assert not a&~0xfffe and not b&~0xfffe and not c&~0x7f13 and not d&~0x7e80
        assert ysel in (0,1) and shift in (0,1) and not c&0x84
        assert ira<=32
        # Relative TEMP indices are distinct modulo 128. Keep a scalar after
        # each write so the C compiler need not reload through potentially
        # aliased runtime indices. Retain every original state write.
        x=f't{tra}' if tra in written_temp else f's->temp[(cursor+{tra})&127]'
        inp='input*16' if ira==32 else f's->mem[{ira}]'
        base='0' if c&2 else 'acc' if c&1 else x
        multiplier=(coef[i]>>3) if ysel else 0 # no FRCL in this program
        operand=inp if b&0x8000 else x
        expression=base if not multiplier else f'source_reverb_multiply(({operand}),{multiplier})+({base})'
        lines.append(f'    /* DSP step {i} */')
        # INPUTS is sampled BEFORE IWT; preserve an aliased source slot.
        if b&0x40 and multiplier and b&0x8000 and ira==(b>>1&31):
            lines.append(f'    int in{i}={inp};');expression=expression.replace(f'({inp})',f'(in{i})')
        if b&0x40:lines.append(f'    s->mem[{b>>1&31}]=memval[{i&3}];')
        lines.append(f'    shift=source_reverb_clip(acc{ "*2" if shift else ""});acc={expression};')
        if a&0x100:
            lines.append(f'    {"" if twa in written_temp else "int "}t{twa}=shift;')
            lines.append(f'    s->temp[(cursor+{twa})&127]=t{twa};')
            written_temp.add(twa)
        if c&0x6000:
            assert i&1
            address=delay[d>>9&63]+bool(d&0x80)
            assert address<=18294 # proven history bound for the compact PSP ring
            lines.append(f'    unsigned at{i}=(cursor+{address})&SOURCE_REVERB_MASK;')
            if c&0x2000:lines.append(f'    memval[{(i+2)&3}]=source_reverb_read(s,at{i});')
            if c&0x4000:lines.append(f'    source_reverb_write(s,at{i},shift);')
        if c&0x1000:
            assert (c>>8&15)<2
            lines.append(f'    out[{c>>8&15}]=(int16_t)(shift>>8);')
    lines+=['    s->cursor=cursor==1 ? SOURCE_REVERB_MASK+1:cursor-1;','}']
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args()
    value=generate()
    if a.check:assert (ROOT/'psp/src/reverb_program.inc').read_text()==value
    else:print(value,end='')
