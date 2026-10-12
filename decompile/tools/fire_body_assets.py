#!/usr/bin/env python3
"""Restore Fire's 07dc1c -> 1b5fd8 -> 1b8644 body TEXLIST.

Both kind 9 and kind 25 bind PVR 8, not flame animation PVR 0.
Keep the existing mesh, motion, vertex colors and unrelated textures.
"""
import argparse, hashlib, json, subprocess, sys, tempfile
from pathlib import Path
from cutscene_sources import executable, afs_file, pvr_offsets
from denki_body_assets import repair
from jgc_compact import compact
from character_roster import archive
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'psp'

def build():
    e=executable();shared=(P/'chars.jgc').read_bytes()
    assert e.u(0x8c07dd74)==0x8c1b5fd8
    tl=e.u(0x8c1b5fd8);assert tl==0x8c1b8644 and e.u(tl+4)==1
    offsets=sorted(pvr_offsets(afs_file('ENEMY',16).read_bytes()))
    texture=offsets.index(e.u(tl+8)-0x8ca00000);assert texture==8
    rows=(ROOT/'decompile/chars_spec.txt').read_text().splitlines()
    for model,table in ((33,0x8c1b5fd0),(50,0x8c1b5fc8)):
        assert rows[model].endswith(':tex='+str(texture))
        spec=':'.join(rows[model].split(':')[:4])+f':rawat:8ca00000:{e.u(table):x}:0:texmap={texture}'
        with tempfile.TemporaryDirectory(prefix='jgr-fire-body-') as tmp:
            q=Path(tmp)/'body.jgc'
            subprocess.run([sys.executable,'tools/jgc_convert.py',str(q),spec],
                           cwd=ROOT/'decompile',check=True,capture_output=True)
            shared=repair(shared,q.read_bytes(),model,12,22,companions=(33,50))
    packed=compact(shared)
    return {'chars.jgc':shared,'chars_compact.jgc':packed,
            'chars_roster.bin':archive(packed,(P/'playerlight.bin').read_bytes())}

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    for name,data in build().items():
        if a.check:assert (P/name).read_bytes()==data,name
        else:(P/name).write_bytes(data)
        print(name,len(data),hashlib.sha256(data).hexdigest())
    if not a.check:
        p=P/'playerlight.json';info=json.loads(p.read_text())
        info['shared_bank_sha256']=hashlib.sha256((P/'chars.jgc').read_bytes()).hexdigest()
        p.write_text(json.dumps(info,indent=2)+'\n')
