#!/usr/bin/env python3
"""03355c / 034760: the six original stage menu model/animation lists."""
from pathlib import Path
import argparse,struct,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
def wipe_path():
    ex=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    # 04c660 interpolates frame 30 against the following descriptor bytes,
    # not a clamped/looped key. Preserve that source tail as the 32nd sample.
    rows=[struct.unpack_from('<I3f',ex,0x16fda8+16*i)[1:3] for i in range(32)]
    return ('/* Original 17fda8 path, including the descriptor tail read at frame 30. */\n'
        'static const float PAUSE_WIPE_PATH[32][2]={\n'+
        ''.join('    {'+','.join(v.hex()+'f' for v in row)+'},\n' for row in rows)+'};\n').encode()
def bake(out):
    ex=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes();data=(ROOT/'decompile/afs/U_GAME/000_gamedispdat.bin').read_bytes()
    u=lambda a:struct.unpack_from('<I',ex,a-0x8c010000)[0]
    word=lambda a:struct.unpack_from('<I',data,a-0x8c980000)[0]
    models={};spec=[];lists=[]
    def model(cell,selected,intro):
        key=(cell,selected,intro)
        if key not in models:
            clips=[word(m) for m in (selected,intro) if m];idx=len(spec);models[key]=idx
            spec.append(f'Pause{idx}:afs/U_GAME/000_gamedispdat.bin:afs/U_GAME/001_gamedisptxr.bin:0,0,0,0:rawat:8c980000:{word(cell):x}:'+(','.join(f'{m:x}' for m in clips)or'0')+':lighting=1')
        return models[key]
    for stage in range(6):
        a=u(0x8c17874c+stage*4);rows=[]
        while u(a+20):
            x,y,z,s,kind,cell,selected,intro=struct.unpack_from('<4f4I',ex,a-0x8c010000)
            rows.append('{'+','.join([str(model(cell,selected,intro)),str(kind),str(0 if selected else -1),str(bool(selected)*1 if intro else -1),*[v.hex()+'f' for v in (x,y,z,s)]])+'}')
            a+=32
        lists.append(rows)
    lines=['/* Generated original 17874c lists: stage class 0..5. */','static const PauseEntry PAUSE_ENTRIES[6][7]={']
    for rows in lists:lines.append('    {'+','.join(rows+['{-1}'])+'},')
    lines+=['};',''];inc='\n'.join(lines).encode()
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out),'--source-material=1','--source-environment=1',*spec],cwd=ROOT/'decompile',check=True)
    return inc
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    target=ROOT/'psp/pausehud.jgc';inc=ROOT/'psp/src/pause_hud_entries.inc';path=ROOT/'psp/src/pause_wipe_path.inc'
    if a.check:
        with tempfile.TemporaryDirectory(prefix='jgr-pause-') as tmp:
            out=Path(tmp)/target.name;expected=bake(out);assert out.read_bytes()==target.read_bytes();assert expected==inc.read_bytes();assert wipe_path()==path.read_bytes()
    else:inc.write_bytes(bake(target));path.write_bytes(wipe_path())
if __name__=='__main__':main()
