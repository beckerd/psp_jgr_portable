#!/usr/bin/env python3
"""Restore the flight-body TEXLISTs selected by 0a5ab0/0a5bf4.

Both original lists bind PVR 1. Preserve meshes, motion keys, baked vertex
colors, and every other model's textures; replace only the private atlas.
"""
import argparse, hashlib, json, subprocess, sys, tempfile
from pathlib import Path
from cutscene_sources import executable, afs_file, texture_lists
from denki_body_assets import repair
from jgc_compact import compact
from character_roster import archive
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'psp'

def build():
    e=executable();shared=(P/'chars.jgc').read_bytes()
    rows=(ROOT/'decompile/chars_spec.txt').read_text().splitlines()
    for model,dat,txr,table,binding in (
        (35,25,24,0x8c1b632c,0x8c1c98f0),
        (37,29,28,0x8c1b647c,0x8c1c9798)):
        maps,_=texture_lists(e,binding,afs_file('ENEMY',txr).read_bytes(),0x8ca00000)
        texture_map=maps[e.u(table+0x10)]
        assert texture_map==[1]
        row=rows[model]
        assert row.endswith(':tex='+str(texture_map[0]))
        with tempfile.TemporaryDirectory(prefix='jgr-heli-body-') as tmp:
            q=Path(tmp)/'body.jgc'
            # The explicit root must match the existing four-bone flight
            # model. No motion/key data is replaced by this texture repair.
            fields=row.split(':')
            spec=':'.join(fields[:4])+f':rawat:8ca00000:{e.u(table):x}:0:tex={texture_map[0]}'
            subprocess.run([sys.executable,'tools/jgc_convert.py',str(q),spec],cwd=ROOT/'decompile',check=True,capture_output=True)
            shared=repair(shared,q.read_bytes(),model,4,9)
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
