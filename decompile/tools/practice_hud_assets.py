#!/usr/bin/env python3
"""035848: original E_TUTORIAL practice-pause objects, clips and help image."""
from pathlib import Path
import argparse,struct,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
def bake(out):
    ex=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    data=(ROOT/'decompile/afs/E_TUTORIAL/000_tutorialdat.bin').read_bytes()
    word=lambda a:struct.unpack_from('<I',data,a-0x8c9f0000)[0]
    rows=[];entries=[]
    for i in range(3):
        x,y,z,s,unused,obj,selected,intro=struct.unpack_from('<4f4I',ex,0x169344+i*32)
        clips=[word(c) for c in (selected,intro) if c]
        rows.append(f'Practice{i}:afs/E_TUTORIAL/000_tutorialdat.bin:afs/E_TUTORIAL/001_tutorialtxr.bin:0,0,0,0:rawat:8c9f0000:{word(obj):x}:'+','.join(f'{c:x}' for c in clips)+':lighting=1')
        entries.append('{'+','.join([str(i),str(0 if selected else -1),str(1 if selected else 0),*[v.hex()+'f' for v in (x,y,z,s)]])+'}')
    assert struct.unpack_from('<I',ex,0x169344+3*32+20)[0]==0
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out/'practicehud.jgc'),'--source-material=1','--source-environment=1',*rows],cwd=ROOT/'decompile',check=True)
    subprocess.run([sys.executable,'tools/jgs_tex2d.py','afs/E_TUTORIAL/001_tutorialtxr.bin',str(out/'practicehud_tex.bin')],cwd=ROOT/'decompile',check=True)
    a=word(0x8c9f00e8);p,n,x,y,z,rot,sx,sy=struct.unpack_from('<II3fi2f',data,a-0x8c9f0000)
    assert n==1 and rot==0 and sx==sy==1
    (out/'practicehud_list.bin').write_bytes(struct.pack('<4sII3fi2f',b'L2D1',1,n,0,0,-1.78,rot,sx,sy)+data[p-0x8c9f0000:p-0x8c9f0000+n*64])
    return ('/* 179344 descriptors, original 9f0000 E_TUTORIAL pointers. */\nstatic const PracticeEntry PRACTICE_ENTRIES[3]={'+','.join(entries)+'};\n').encode()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    inc=ROOT/'psp/src/practice_hud_entries.inc'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-practice-assets-') as tmp:
            out=Path(tmp);assert bake(out)==inc.read_bytes()
            for p in out.iterdir():assert p.read_bytes()==(ROOT/'psp'/p.name).read_bytes(),p.name
    else:inc.write_bytes(bake(ROOT/'psp'))
if __name__=='__main__':main()
