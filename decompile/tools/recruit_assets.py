#!/usr/bin/env python3
"""Original PLAY +54 mouth hierarchy, +68 CNK table and PTEX +54.

Per-skater banks retain every source key; no procedural lip animation.
These assets do not establish complete scene/voice synchronization.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from baked_asset_colors import match_face_colors,bake_expression_colors
from baked_light import character_shadow_lift
ROOT=Path(__file__).resolve().parents[2]
DEC=ROOT/'decompile'

def export(directory):
    manifest=[]
    for index,row in enumerate((DEC/'chars_spec.txt').read_text().splitlines()[:16]):
        name,dat,txr=row.split(':')[:3]
        data,texture=(DEC/dat).read_bytes(),(DEC/txr).read_bytes()
        def u(blob,at):return struct.unpack_from('<I',blob,at)[0]
        table=u(data,u(data,8)+0x68)
        motions=[u(data,table+4*i) for i in range(3)]
        frames=[u(data,p)>>16 for p in motions]
        assert frames==([2,2,2] if name=='Goji' else [160,160,50])
        path=directory/f'recruitmouth{index:02d}.jgc'
        # Retain the original normals only while exporting; the PSP bank
        # stays JGC2 and carries no extra runtime lighting data.
        # Actual Cube is PLAY character 6 / model 10 (recodat). The legacy
        # spec labels that row "Slate"; its "Cube" row is actually Garam.
        corrected=name in ('Mew','YoYo','Piranha') or index==10;face_bake=None
        strength=character_shadow_lift(dat)
        with tempfile.TemporaryDirectory(prefix='jgr-expression-light-') as tmp:
            side=Path(tmp)/'lighting.bin'
            subprocess.run([sys.executable,'tools/jgc_convert.py',str(path),
                '--toon=1','--play-variant=companion',
                *(['--lighting-sidecar='+str(side)] if corrected else []),row],
                cwd=DEC,check=True,stdout=subprocess.PIPE)
            if corrected:
                bank,face_bake=bake_expression_colors((ROOT/'psp/chars.jgc').read_bytes(),index,path.read_bytes(),side.read_bytes(),shadow_lift=strength)
                path.write_bytes(bank)
        bank=path.read_bytes();assert u(bank,4)==1 and u(bank,28)==3
        if strength and not corrected:
            bank,matched=match_face_colors((ROOT/'psp/chars.jgc').read_bytes(),index,bank)
            path.write_bytes(bank)
        manifest.append(dict(model=index,name=name,source=dat,textures=txr,
            source_sha256=hashlib.sha256(data).hexdigest(),texture_sha256=hashlib.sha256(texture).hexdigest(),
            root=u(data,u(data,8)+0x54),texlist=u(texture,u(texture,8)+0x54),
            motions=motions,frames=frames,bones=u(bank,8),bytes=len(bank),sha256=hashlib.sha256(bank).hexdigest()))
        if strength and not corrected:manifest[-1]['matched_head_vertices']=matched
        if face_bake:manifest[-1]['face_bake']=dict(face_bake,palette='shared PLAY head; shibuya half-strength interiors')
        if strength and corrected:manifest[-1]['face_bake']['shadow_lift']=strength
    return manifest

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    target=ROOT/'psp'
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-recruit-assets-') as tmp:
            d=Path(tmp);info=export(d)
            for p in d.iterdir():assert p.read_bytes()==(target/p.name).read_bytes(),p.name
            assert info==json.loads((target/'recruitmouths.json').read_text())
    else:
        info=export(target);(target/'recruitmouths.json').write_text(json.dumps(info,indent=2)+'\n')
    print(f'{len(info)} original PLAY mouths; {sum(r["bytes"] for r in info):,} bytes, loaded individually.')
if __name__=='__main__':main()
