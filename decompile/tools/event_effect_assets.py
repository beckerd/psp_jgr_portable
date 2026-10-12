#!/usr/bin/env python3
"""Original EVENT flames (0e2464) and riot-squad burst pairs (0dd780)."""
import subprocess,sys,json
from cutscene_sources import ROOT,DEC,executable,texture_lists
from event_assets import typed_binding
e=executable();out=ROOT/'psp';rows=[]
dat='afs/ENEMY/045_effectdat.bin';tex='afs/ENEMY/044_effecttxr.bin'
lists,_=texture_lists(e,0x8c1b9590,(DEC/tex).read_bytes(),0x8c800000)
args=[sys.executable,'tools/jgc_convert.py',str(out/'eventflare.jgc'),'--motions=0','--source-material=1']
for root in (e.u(0x8c1b6710),e.u(0x8c1b6714)):
    for frame in range(8):
        args.append(f'Flame{len(rows)}:{dat}:{tex}:0,0,0,0:rawat:8cff0000:{root:x}:0:texmap='+','.join(str(lists[0x8c1b94a8][frame]) for _ in range(8)))
        rows.append(dict(root=root,frame=frame))
subprocess.run(args,cwd=DEC,check=True)
b=typed_binding(4);table=0x8c1b5744
args=[sys.executable,'tools/jgc_convert.py',str(out/'eventblast.jgc'),'--motions=1','--source-material=1']
for offset in (12,24,36,48):
    root=e.u(e.u(table+offset));motion=e.u(e.u(table+offset+8))
    args.append(f'Burst{offset}:{b["source"]}:{b["textures"]}:0,0,0,0:rawat:{b["source_base"]:x}:{root:x}:{motion:x}')
    rows.append(dict(root=root,motion=motion))
subprocess.run(args,cwd=DEC,check=True)
(out/'eventeffects.json').write_text(json.dumps(rows,indent=2)+'\n')
