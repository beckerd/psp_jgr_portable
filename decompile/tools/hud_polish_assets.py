#!/usr/bin/env python3
"""Original Finish!! (0e8f5c) and vehicle impact (0438b4) models/motions."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
DEC=ROOT/'decompile'

def export(directory):
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    game=(DEC/'afs/U_GAME/000_gamedispdat.bin').read_bytes()
    u=lambda a:struct.unpack_from('<I',exe,a-0x8c010000)[0]
    v=lambda a:struct.unpack_from('<I',game,a-0x8c980000)[0]
    specs=[('finishhud','afs/U_GAME/000_gamedispdat.bin','afs/U_GAME/001_gamedisptxr.bin',
            0x8c980000,v(0x8c9801b4),v(0x8c9801b8),30),
           ('impactfx','afs/PLAYER/016_player_effect_dat.bin','afs/PLAYER/017_player_effect_txr.bin',
            0x8c8e1000,u(0x8c182fec),u(0x8c183090),15)]
    metadata={}
    for name,dat,txr,base,obj,motion,frames in specs:
        source=(DEC/dat).read_bytes()
        assert struct.unpack_from('<H',source,motion-base+2)[0]==frames
        row=f'{name}:{dat}:{txr}:0,0,0,0:rawat:{base:x}:{obj:x}:{motion:x}:lighting=1'
        path=directory/(name+'.jgc')
        subprocess.run([sys.executable,'tools/jgc_convert.py',str(path),'--motions=1',
                        '--source-material=1','--source-environment=1',row],cwd=DEC,check=True)
        metadata[name]=dict(source=dat,textures=txr,root=f'{obj:08x}',motion=f'{motion:08x}',frames=frames,
            source_sha256=hashlib.sha256(source).hexdigest(),
            texture_sha256=hashlib.sha256((DEC/txr).read_bytes()).hexdigest(),
            sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    return metadata

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-hud-polish-assets-') as tmp:
            directory=Path(tmp);metadata=export(directory)
            for name in metadata:assert (directory/(name+'.jgc')).read_bytes()==(ROOT/'psp'/(name+'.jgc')).read_bytes()
            assert metadata==json.loads((ROOT/'psp/hud_polish_assets.json').read_text())
    else:
        metadata=export(ROOT/'psp')
        (ROOT/'psp/hud_polish_assets.json').write_text(json.dumps(metadata,indent=2)+'\n')
if __name__=='__main__':main()
