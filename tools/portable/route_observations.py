#!/usr/bin/env python3
"""Bake freshly observed performances using disc control/character banks.

The caller supplies capture receipts, never preconverted routes. Every stream
is checked by the existing converter against the original controller records;
setup_assets then checks the resulting asset hashes before publication.
"""
import argparse
import json
from pathlib import Path
import shutil
import sys

from observations import jobs, validate


def build(tree, captures, family, key=None):
    tree=tree.resolve()
    fingerprint=json.loads((tree.parent/'disc.json').read_text())
    definitions=[j for j in jobs() if j['kind']==family and (key is None or j['key']==key)]
    if not definitions:raise ValueError('No requested route recipe')
    verified={}
    for job in definitions:
        name=job['key']
        actual,source=validate(Path(captures[name]),fingerprint)
        if actual!=name:raise ValueError('Wrong performance recipe '+name)
        # Keep converter provenance relative to this clean tree. The staged
        # inputs are raw newly-generated observations, not legacy captures.
        dest=tree/'observations'/name
        dest.mkdir(parents=True,exist_ok=True)
        names=(['title_actor_camera.bin','title.csv','audio.csv'] if family=='demo' else
               ['rival%d.bin'%n for n in range(3 if family=='gang' else 1)])
        for filename in names+['flycast.log','capture.json']:
            shutil.copyfile(source/filename,dest/filename)
        verified[name]=dest
    sys.path.insert(0,str(tree/'decompile/tools'))
    if family=='demo':
        import challenge_demo_assets as module
        records=module.specs()
        selected=[]
        for job in definitions:
            spec=next(r for r in records if r['mission']==job['mission'] and r['set']==job['subset'])
            spec['source']=verified[job['key']]
            selected.append(spec)
        module.specs=lambda:selected
        module.build()
    elif family=='rival':
        import challenge_rival_assets as module
        module.SPECS=[r for r in module.SPECS if any(j['mission']==r['mission'] for j in definitions)]
        module.CAPTURE=tree/'observations'
        # The converter's historical directory names are retained only
        # inside the clean tree; no developer captures are consulted.
        for job in definitions:
            dest=module.CAPTURE/('dc_rival_'+job['mission'].lower())
            dest.mkdir(exist_ok=True)
            for filename in ('rival0.bin','flycast.log'):
                shutil.copyfile(verified[job['key']]/filename,dest/filename)
        module.build()
    elif family=='gang':
        import poison_route_assets as module
        for job in definitions:
            module.build(verified[job['key']],prefix=job['prefix'],character=job['character'],
                         bank=job['bank'],subset=job['subset'])
    else:raise ValueError('Unsupported route family')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree',type=Path,required=True)
    p.add_argument('--captures',type=Path,required=True)
    p.add_argument('--family',choices=['demo','rival','gang'],required=True)
    p.add_argument('--key',help='Development only: a single performance; cannot form a complete release pack')
    a=p.parse_args()
    build(a.tree,json.loads(a.captures.read_text()),a.family,a.key)
