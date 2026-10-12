#!/usr/bin/env python3
"""019000/03f926 PLAY spray variants: original grip/can meshes and PTEX lists.

Keep each selected skater in a separate bank. Loading all variants and their
motion tables alongside chars.jgc would waste the PSP-1000 memory budget.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from jgc_compact import mesh_only
from baked_asset_colors import recolor,match_face_colors
from baked_light import character_shadow_lift
ROOT = Path(__file__).resolve().parents[2]
DEC = ROOT/'decompile'

def export(directory):
    rows = (DEC/'chars_spec.txt').read_text().splitlines()[:16]
    manifest = []
    for index, row in enumerate(rows):
        name, dat, txr = row.split(':')[:3]
        data, texture = (DEC/dat).read_bytes(), (DEC/txr).read_bytes()
        def ptr(blob, field):
            table = struct.unpack_from('<I', blob, 8)[0]
            return struct.unpack_from('<I', blob, table+field)[0]
        path = directory/f'spraychar{index:02d}.jgc'
        side = path.with_suffix('.bin')
        subprocess.run([sys.executable, 'tools/jgc_convert.py', str(path),
            '--toon=1', '--play-variant=spray', '--lighting-sidecar='+str(side), row],
            cwd=DEC, check=True, stdout=subprocess.PIPE)
        bank = path.read_bytes()
        # Keep the can/grip variant in the same offline city palette as the
        # ordinary skater. Preserve textures, animation, outlines and PLT1;
        # the final stage still supplies its own dynamic source lighting.
        with tempfile.TemporaryDirectory(prefix='jgr-spray-bake-') as tmp:
            baked=Path(tmp)/'baked.jgc'
            subprocess.run([sys.executable,'tools/jgc_convert.py',str(baked),
                '--toon=1','--play-variant=spray','--baked-light=shibuya',row],
                cwd=DEC,check=True,stdout=subprocess.PIPE)
            bank,_=recolor(bank,baked.read_bytes(),[0],strength=0.5)
            strength=character_shadow_lift(dat)
            if strength:
                unlit=Path(tmp)/'unlit.jgc'
                subprocess.run([sys.executable,'tools/jgc_convert.py',str(unlit),
                    '--play-variant=spray','--nolight=1',row],
                    cwd=DEC,check=True,stdout=subprocess.PIPE)
                bank,_=recolor(bank,unlit.read_bytes(),[0],strength=strength)
            # Piranha and Cube's spray faces share the normal head's
            # surface. Cube is recodat/model 10, mislabeled "Slate".
            if strength or index in (7,10):
                bank,_=match_face_colors((ROOT/'psp/chars.jgc').read_bytes(),index,bank,target_bone=11)
        path.write_bytes(bank)
        (directory/f'spraymesh{index:02d}.jgc').write_bytes(mesh_only(bank))
        nc, nb, _, nv, _, _, nm = struct.unpack_from('<7I', bank, 4)
        assert nc == 1 and nb == 20 and nm == 176
        manifest.append(dict(model=index, name=name, source=dat, textures=txr,
            source_sha256=hashlib.sha256(data).hexdigest(),
            texture_sha256=hashlib.sha256(texture).hexdigest(),
            normal_root=ptr(data,4), spray_root=ptr(data,0x24),
            normal_texlist=ptr(texture,4), spray_texlist=ptr(texture,0x24),
            bytes=len(bank), vertices=nv, sha256=hashlib.sha256(bank).hexdigest(),
            lighting_sha256=hashlib.sha256(side.read_bytes()).hexdigest(),
            baked_palette='shibuya',baked_strength=0.5,dynamic_lighting_sidecar_unchanged=True))
        if strength:manifest[-1]['shadow_lift']=strength
        if index in (7,10):manifest[-1]['face_palette']='shared PLAY head'
    return manifest

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check',action='store_true'); args=ap.parse_args()
    target=ROOT/'psp'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-spray-characters-') as tmp:
            directory=Path(tmp); info=export(directory)
            for path in directory.iterdir(): assert path.read_bytes()==(target/path.name).read_bytes(),path.name
            assert info==json.loads((target/'spraychars.json').read_text())
    else:
        info=export(target)
        (target/'spraychars.json').write_text(json.dumps(info,indent=2)+'\n')
    print(json.dumps(info,indent=2))
if __name__=='__main__': main()
