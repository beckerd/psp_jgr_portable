#!/usr/bin/env python3
"""Rebuild Shibuya placements and rebake the shared city PLAY palette.

Stage placement transforms follow Ninja Z/Y/X rotation. Character exports
change only RGB bytes; texels, CLUTs, UVs, indices, alpha, outlines, motions
and bank sizes are retained. Animated lighting remains a static
bind-pose approximation. Other cities' architecture is not rebaked here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from baked_asset_colors import recolor,recolor_character
from baked_light import character_shadow_lift
from jgc_compact import compact
from character_roster import archive

ROOT=Path(__file__).resolve().parents[2]
DEC=ROOT/'decompile'


def build(characters_only=False):
    assets={}
    with tempfile.TemporaryDirectory(prefix='jgr-baked-light-') as tmp:
        stage=Path(tmp)/'stage.jgs';chars=Path(tmp)/'chars.jgc';legacy=Path(tmp)/'legacy.jgc';unlit=Path(tmp)/'unlit.jgc'
        jobs=[['tools/jgs_convert.py','afs/STAGE1/000_st1dat.bin','0x8cb00000',str(stage),
               '--layout=stage1_layout.json','--texmap=stage1_texmap_static.json','--maxedge=120','--baked-light=shibuya'],
              ['tools/jgc_convert.py',str(chars),'--spec=chars_spec.txt','--toon=1','--baked-light=shibuya'],
              ['tools/jgc_convert.py',str(legacy),'--spec=chars_spec.txt'],
              ['tools/jgc_convert.py',str(unlit),'--spec=chars_spec.txt','--nolight=1']]
        for args in (jobs[1:] if characters_only else jobs):
            subprocess.run([sys.executable,*args],cwd=DEC,check=True,stdout=subprocess.PIPE)
        models=[]
        for index,row in enumerate((DEC/'chars_spec.txt').read_text().splitlines()):
            if row and not row.startswith('#') and (DEC/row.split(':')[1]).read_bytes()[:4]==b'PLAY':models.append(index)
        # Half-strength character correction, always from the original
        # export: repeated builds must not accumulate color adjustments.
        softened,_=recolor(legacy.read_bytes(),chars.read_bytes(),models,strength=0.5)
        for index,row in enumerate((DEC/'chars_spec.txt').read_text().splitlines()):
            strength=character_shadow_lift(row.split(':')[1])
            if strength:softened,_=recolor(softened,unlit.read_bytes(),[index],strength=strength)
        chars.write_bytes(softened)
        # Architecture is a complete reproducible export. RGB-only copying
        # would retain the old, incorrect multi-axis placement transforms.
        if not characters_only:assets['stage1.jgs']=stage.read_bytes()
        shared=(ROOT/'psp/chars.jgc').read_bytes()
        for model in models:shared=recolor_character(shared,model,chars.read_bytes(),model)
        assets['chars.jgc']=shared
        assets['chars_compact.jgc']=compact(assets['chars.jgc'])
        # Ordinary gameplay/intro scenes load this mission-cast archive,
        # not the complete shared bank. Keep all three palettes together.
        assets['chars_roster.bin']=archive(assets['chars_compact.jgc'],(ROOT/'psp/playerlight.bin').read_bytes())
    return assets


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true')
    ap.add_argument('--characters-only',action='store_true',help='leave stage placement exports untouched')
    args=ap.parse_args()
    assets=build(args.characters_only);rows=[]
    for name,data in assets.items():
        path=ROOT/'psp'/name
        if args.check:assert path.read_bytes()==data,name
        else:path.write_bytes(data)
        rows.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    if not args.check:
        path=ROOT/'psp/playerlight.json';info=json.loads(path.read_text())
        info['shared_bank_sha256']=hashlib.sha256(assets['chars.jgc']).hexdigest()
        path.write_text(json.dumps(info,indent=2)+'\n')
    print(json.dumps(rows,indent=2))


if __name__=='__main__':main()
