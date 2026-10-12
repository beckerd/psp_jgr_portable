#!/usr/bin/env python3
"""Original 178c84 pause maps, 034ca0 markers and 033f84 objective strings."""
from pathlib import Path
import argparse,struct,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
def bake(out):
    ex=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    data=(ROOT/'decompile/afs/U_GAME/000_gamedispdat.bin').read_bytes()
    u=lambda a:struct.unpack_from('<I',ex,a-0x8c010000)[0]
    word=lambda a:struct.unpack_from('<I',data,a-0x8c980000)[0]
    literal=lambda s:'"'+''.join(f'\\x{b:02x}' for b in s)+'"'
    specs=[];models={}
    def model(cell,motion=0):
        key=(cell,motion)
        if key not in models:
            n=len(specs);models[key]=n
            specs.append(f'PauseMap{n}:afs/U_GAME/000_gamedispdat.bin:afs/U_GAME/001_gamedisptxr.bin:0,0,0,0:rawat:8c980000:{word(cell):x}:{word(motion):x}:lighting=1' if motion else
                         f'PauseMap{n}:afs/U_GAME/000_gamedispdat.bin:afs/U_GAME/001_gamedisptxr.bin:0,0,0,0:rawat:8c980000:{word(cell):x}:0:lighting=1')
        return models[key]
    lines=['/* Generated from original 178c84 descriptors and U_GAME. */',
           'static const PauseMapEntry PAUSE_MAPS[6][6]={']
    counts=(5,5,6,1,1,1)
    for stage,count in enumerate(counts):
        rows=[]
        for j in range(count):
            a=u(u(0x8c178c84+stage*4)+j*4)
            x,y,z,s,p=struct.unpack_from('<4fI',ex,a-0x8c010000)
            ids=[]
            while u(p):ids.append(model(u(p)));p+=4
            rows.append('{'+','.join(v.hex()+'f' for v in (x,y,z,s))+',{' + ','.join(map(str,ids+[-1]))+'}}')
        lines.append('    {'+','.join(rows)+'},')
    lines+=['};','static const unsigned PAUSE_MAP_COUNTS[6]={'+','.join(map(str,counts))+'};']
    # 0340d4: player, ordinary rival, GF, Onishima, ordinary police,
    # Poison Jam, Love Shockers, Noise Tanks, helicopter.
    icons=((0,0),(4,0),(0x190,0),(0x180,0x184),(0x188,0x18c),
           (0x194,0x198),(0x19c,0x1a0),(0x1a4,0x1a8),(0x1ac,0x1b0))
    lines+=['static const int PAUSE_MAP_ICONS[9]={'+','.join(str(model(0x8c980000+c,0x8c980000+m if m else 0)) for c,m in icons)+'};']
    p=word(0x8c9801e4)
    strings=[data[word(p+j*4)-0x8c980000:].split(b'\0')[0] for j in (4,9,10,11)]
    digits=[ex[a-0x8c010000:a-0x8c010000+2] for a in struct.unpack_from('<11I',ex,0x163ba0)]
    lines+=['static const char *const PAUSE_MAP_TEXT[4]={'+','.join(map(literal,strings))+'};',
            'static const char PAUSE_MAP_DIGITS[11][3]={'+','.join(map(literal,digits))+'};']
    p=u(u(0x8c0340bc)+4) # Supplied USA build uses language 1 (English).
    xy=struct.unpack_from('<4f',ex,p-0x8c010000)
    lines+=['static const float PAUSE_MAP_TEXT_XY[4]={'+','.join(v.hex()+'f' for v in xy)+'};','']
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out),'--source-material=1','--source-environment=1',*specs],cwd=ROOT/'decompile',check=True)
    return '\n'.join(lines).encode()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    target=ROOT/'psp/pausemap.jgc';inc=ROOT/'psp/src/pause_map_entries.inc'
    if a.check:
        with tempfile.TemporaryDirectory(prefix='jgr-pause-map-') as tmp:
            out=Path(tmp)/target.name;expected=bake(out)
            assert out.read_bytes()==target.read_bytes();assert expected==inc.read_bytes()
    else:inc.write_bytes(bake(target))
if __name__=='__main__':main()
