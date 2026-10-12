#!/usr/bin/env python3
"""Export six source-bound animated STAGELAST scene pieces, NOT a playable stage.

0e9210/0e975c and 0ed318/0ed598 bind the five records at 1d0db0/db4...
to object table 1d0dc4 and CNK motion table 1d0dd8. 0e5470 draws the
background using 1d0e58/5c/60. The original texture loader's kind-0 binding
table is 1d0cb8 (STAGELAST file 0 entry 185ef0). No heuristic root matching,
identity texture mapping or geometry substitution is used here.

This does not export the boss, pickups or satellite effects. Animated collision
is exported separately by final_collision.py.
It must not enable stage 4 in the campaign until those handlers are ported.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

DEC = Path(__file__).resolve().parents[1]
BASE = 0x8cb00000

def source_manifest():
    exe = (DEC/'extracted/1ST_READ.BIN').read_bytes()
    dat = (DEC/'afs/STAGELAST/001_lastdat.bin').read_bytes()
    txr = (DEC/'afs/STAGELAST/000_lasttxr.bin').read_bytes()
    def u(a):
        assert 0x8c010000 <= a <= 0x8c010000+len(exe)-4 and not a&3
        return struct.unpack_from('<I',exe,a-0x8c010000)[0]
    offsets = {};at = 0
    while True:
        at = txr.find(b'GBIX',at)
        if at < 0:break
        offsets[at] = len(offsets);at += 8
    lists = {};p = 0x8c1d0cb8
    while u(p):
        tl,ptr = u(p),u(p+4);n = u(tl+4)
        assert 0 < n <= 256
        lists[tl] = [offsets[u(ptr+k*4)-0x8c800000] for k in range(n)]
        p += 8;assert p < 0x8c1d0db0
    rows = []
    for k in range(6):
        name = f'platform{k}' if k<5 else 'background'
        ta,oa,ma = (0x8c1d0db0+4*k,0x8c1d0dc4+4*k,0x8c1d0dd8+4*k) if k<5 else (0x8c1d0e58,0x8c1d0e5c,0x8c1d0e60)
        tl,root,motion = u(ta),u(oa),u(ma)
        assert BASE <= root < BASE+len(dat)-0x34 and BASE <= motion < BASE+len(dat)-8
        header,md = struct.unpack_from('<II',dat,motion-BASE)
        assert header == 0x01f40015 and BASE <= md < BASE+len(dat)
        row=dict(name=name,texlist=tl,root=root,motion=motion,frames=header>>16,texture_map=lists[tl],draw_mode=2 if k==4 else 1)
        if k<4:
            row['collapse_motion']=u(0x8c1d0e1c+4*k)
            collapse=struct.unpack_from('<I',dat,row['collapse_motion']-BASE)[0]
            assert collapse>>16==640 and collapse&65535 in (0x11,0x15)
            row['collapse_frames']=collapse>>16
        rows.append(row)
    return dict(source_sha256=hashlib.sha256(dat).hexdigest(),textures_sha256=hashlib.sha256(txr).hexdigest(),
                total_source_textures=len(offsets),models=rows,playable_stage=False,
                lighting='JGC3: unbaked material, original local normals, normal path 2337b4',
                remaining=['damaging projectile integration','complete spray/HUD cancellation graph','spatial audio and SMSB sequencer','campaign and visual parity verification'])

def export(output,preview=None):
    manifest = source_manifest()
    command = [sys.executable,str(DEC/'tools/jgc_convert.py'),str(output),'--motions=2','--source-material=1','--source-environment=1']
    if preview:command.append('--preview='+str(preview))
    for r in manifest['models']:
        # Mode 1 uses 234fc6, not the PLAY cel-hull path. Original sky
        # header 808c000a/c8800000/9488a4f6: CullMode 1, no face rejection.
        cull=(':cull=none' if r['draw_mode']==1 else '')+':lighting=1'
        motions=f"{r['motion']:x}"+(f",{r['collapse_motion']:x}" if 'collapse_motion' in r else '')
        command.append(f"{r['name']}:afs/STAGELAST/001_lastdat.bin:afs/STAGELAST/000_lasttxr.bin:0,0,0,0:rawat:8cb00000:{r['root']:x}:{motions}:texmap="+','.join(map(str,r['texture_map']))+cull)
    subprocess.run(command,cwd=DEC,check=True)
    return manifest

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=DEC.parent/'psp/finalscene.jgc')
    ap.add_argument('--preview',type=Path)
    ap.add_argument('--check',action='store_true')
    args = ap.parse_args();output = args.out.resolve()
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-final-assets-') as tmp:
            built = Path(tmp)/'finalscene.jgc';manifest = export(built)
            assert output.read_bytes() == built.read_bytes(),'finalscene bank differs from original-source export'
            assert json.loads(output.with_suffix('.json').read_text()) == manifest
    else:
        manifest = export(output,args.preview.resolve() if args.preview else None)
        output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('Six original 500-frame STAGELAST pieces; final-stage runtime remains unfinished.')

if __name__ == '__main__':main()
