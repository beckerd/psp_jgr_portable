#!/usr/bin/env python3
"""032f60 / 03319c / 037aa4 original GAME result models and sparse clips."""
from pathlib import Path
import argparse, struct, subprocess, sys, tempfile
from ninjalib import read_object
ROOT=Path(__file__).resolve().parents[2]
def text_assets():
    data=(ROOT/'decompile/afs/U_GAME/000_gamedispdat.bin').read_bytes()
    ex=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    u=lambda a:struct.unpack_from('<I',data,a)[0]
    p=u(0x1e4)-0x8c980000
    strings=[data[u(p+i*4)-0x8c980000:].split(b'\0')[0] for i in range(4)]
    digits=[ex[a-0x8c010000:a-0x8c010000+2] for a in struct.unpack_from('<11I',ex,0x163ba0)]
    literal=lambda s:'"'+''.join(f'\\x{b:02x}' for b in s)+'"'
    delays=[struct.unpack_from('<16I',ex,a-0x8c010000) for a in (0x8c179558,0x8c179594)]
    names=[]
    for a in (0x8c17948c,0x8c1794c8):
        names.append([ex[p-0x8c010000:].split(b'\0')[0] for p in struct.unpack_from('<15I',ex,a-0x8c010000)])
    return ('/* Original GAME +1e4 formats and 173ba0 numeric glyphs. */\nstatic const char *const RESULT_TEXT[4]={'+','.join(map(literal,strings))+'};\n'+
        'static const char RESULT_DIGITS[11][3]={'+','.join(map(literal,digits))+'};\n'+
        '/* 0366e0 / 036930: +94 tally count, indexed by playable resident id. */\n'+
        'static const unsigned RESULT_VOICE_DELAY[2][16]={'+','.join('{'+','.join(map(str,r))+'}' for r in delays)+'};\n'+
        '/* 179504 language table: Japanese, then the shared Western names. */\n'+
        'static const char *const RESULT_NAMES[2][15]={'+','.join('{'+','.join(map(literal,r))+'}' for r in names)+'};\n').encode()
CELLS=((0x78,0xb4,0),(0x84,0xac,0),(0x94,0xd8,0),(0x98,0xd4,0),
       (0x90,0xcc,0x100),(0x7c,0xb8,0xf0),(0x9c,0xe8,0x104),
       (0x70,0xac,0),(0xa4,0xe0,0),
       (0xa8,0xe4,0x10c),(0x88,0xc4,0xf8),(0x74,0xb0,0xec),
       (0xa0,0xdc,0x108),(0x8c,0xc8,0xfc),(0x80,0xbc,0xf4),
       (0x1dc,0x1e0,0)) # 037f04 NEW RECORD
def bake(out):
    data=(ROOT/'decompile/afs/U_GAME/000_gamedispdat.bin').read_bytes()
    word=lambda off:struct.unpack_from('<I',data,off)[0]
    rows=[];ranges=[];records=[]
    for i,(obj,first,second) in enumerate(CELLS):
        root=word(obj);nodes=[]
        def walk(a):
            while a:
                n=read_object(data,a,0x8c980000);nodes.append(n)
                walk(n['child']);a=n['sib']
        walk(root);assert len(nodes)<=64
        clips=[]
        for cell in (first,second):
            if not cell:clips.append(0);continue
            motion=word(cell);clips.append(motion);md=word(motion-0x8c980000)
            assert word(motion-0x8c980000+8)==0x30007
            ranges.extend(((motion,motion+12),(md,md+len(nodes)*24)))
            for j in range(len(nodes)):
                p=struct.unpack_from('<6I',data,md-0x8c980000+j*24)
                for ch in range(3):
                    if p[ch]:ranges.append((p[ch],p[ch]+p[ch+3]*16))
        records.append(struct.pack('<3I',root,*clips))
        rows.append(f'Result{i}:afs/U_GAME/000_gamedispdat.bin:afs/U_GAME/001_gamedisptxr.bin:0,0,0,0:rawat:8c980000:{root:x}:0:lighting=1')
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out),'--source-material=1','--source-environment=1',*rows],cwd=ROOT/'decompile',check=True)
    base=min(a for a,b in ranges);end=max(b for a,b in ranges)
    out.with_suffix('.bin').write_bytes(struct.pack('<4s3I',b'FHM1',base,end-base,len(records))+b''.join(records)+data[base-0x8c980000:end-0x8c980000])
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    target=ROOT/'psp/failurehud.jgc';text=ROOT/'psp/src/result_hud_text.inc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-failure-hud-') as tmp:
            out=Path(tmp)/target.name;bake(out)
            for suffix in ('.jgc','.bin'):assert out.with_suffix(suffix).read_bytes()==target.with_suffix(suffix).read_bytes()
            assert text_assets()==text.read_bytes()
    else:bake(target);text.write_bytes(text_assets())
if __name__=='__main__':main()
