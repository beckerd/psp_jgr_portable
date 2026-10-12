#!/usr/bin/env python3
"""Generate local CPU/AICA numeric tables from an explicit reference checkout.

This tool reads hardware model inputs from the supplied reference checkout.
Their provenance and upstream license are recorded in reference_sources.json.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def generate(tree, source):
    manifest=json.loads(Path(__file__).with_name('reference_sources.json').read_text())
    for row in manifest['files']:
        path=source/row['path']
        data=path.read_bytes()
        if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
            raise ValueError('Unsupported reference input: '+row['path'])
    (tree/'reference_inputs').mkdir(exist_ok=True)
    output=tree/'psp/src';output.mkdir(parents=True,exist_ok=True)
    command=[sys.executable,str(tree/'decompile/tools/player_fsca.py'),str(source/'core/hw/sh4/fsca-table.h')]
    data=subprocess.check_output(command)
    (output/'player_fsca.inc').write_bytes(data)
    subprocess.run([sys.executable,str(tree/'decompile/tools/smsb_assets.py'),'--reference-source',
                    str(source/'core/hw/aica/sgc_if.cpp')],check=True,cwd=tree/'decompile')
    receipt=dict(manifest,generated=[dict(path='psp/src/'+n,
                   sha256=hashlib.sha256((output/n).read_bytes()).hexdigest())
                   for n in ('player_fsca.inc','smsb_tables.inc')])
    (tree/'reference_inputs/receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Reference CPU/AICA tables generated from verified external hardware model')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree',type=Path,required=True)
    p.add_argument('--reference-source',type=Path,required=True)
    a=p.parse_args()
    try:generate(a.tree.resolve(),a.reference_source.resolve())
    except (ValueError,OSError,subprocess.CalledProcessError) as e:p.exit(1,str(e)+'\n')
