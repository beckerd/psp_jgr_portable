#!/usr/bin/env python3
"""Recover Benten Burning's omitted Fire sprites and Kairiki attack trees."""
import json, struct, subprocess, sys, tempfile
from pathlib import Path
from cutscene_sources import executable, afs_file, fingerprint
ROOT=Path(__file__).resolve().parents[2]
BASE=0x8ca00000

def export(out):
    e=executable();rows=[]
    args=[sys.executable,'tools/jgc_convert.py',str(out),'--source-material=1','--source-environment=1']
    # A one-entry body list and the eight-image flame list are distinct.
    # Resolve the actual GBIX offsets, rather than treating local IDs as global.
    def texmap(bank,tl):
        from cutscene_sources import pvr_offsets
        raw=afs_file('ENEMY',bank).read_bytes();offsets=sorted(pvr_offsets(raw))
        ptr=tl+8
        return [offsets.index(e.u(ptr+4*i)-BASE) for i in range(e.u(tl+4))]
    def add(name,bank,root,motion,tl,phase=None):
        dp,tp=afs_file('ENEMY',bank),afs_file('ENEMY',bank-1)
        mapping=texmap(bank-1,tl)
        if phase is not None: mapping=[mapping[phase]]*len(mapping)
        args.append(f'{name}:{dp}:{tp}:0,0,0,0:rawat:{BASE:x}:{root:x}:{motion:x}:texmap='+','.join(map(str,mapping)))
        rows.append(dict(name=name,root=hex(root),motion=hex(motion),texture_map=mapping))
    # First two models replace the body; next three are the attached swooshes.
    for i in range(2):add('kairiki_body'+str(i),11,e.u(0x8c1b5de0+i*8),e.u(0x8c1b5e5c+i*4),e.u(0x8c1b5df0))
    for i in range(3):add('kairiki_attack'+str(i),11,e.u(0x8c1b5e6c),e.u(0x8c1b5e70+i*4),e.u(0x8c1b5e68))
    # Four left/right stream strips, then four stopped/free terminal images.
    # One geometry tree per shape. Each starts with a different image so all
    # eight images survive conversion; runtime remaps them without cloning.
    for mesh in range(8):
        add(f'fire{mesh}',17,e.u(0x8c1b6070+mesh*4),0,e.u(0x8c1b606c),mesh)
    subprocess.run(args,cwd=ROOT/'decompile',check=True)
    return dict(sources=[fingerprint(afs_file('ENEMY',i)) for i in (10,11,16,17)],models=rows)

if __name__=='__main__':
    p=ROOT/'psp/burning.jgc'
    if '--check' in sys.argv:
        with tempfile.TemporaryDirectory(prefix='jgr-burning-') as t:
            q=Path(t)/p.name;info=export(q);assert q.read_bytes()==p.read_bytes()
            assert info==json.loads(p.with_suffix('.json').read_text())
    else:p.with_suffix('.json').write_text(json.dumps(export(p),indent=2)+'\n')
