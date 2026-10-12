#!/usr/bin/env python3
"""Private final-output parity for disc-only rival and gang controller jobs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'decompile/tools'))
from poison_route_assets import pack_sections

def run(a):
    a.out.mkdir(parents=True,exist_ok=True);reports=[]
    for job in json.loads(a.jobs.read_text())['jobs']:
        if job['kind'] not in ('rival','gang') or (a.only and job['key'] not in a.only):continue
        target=a.out/job['key'];target.mkdir(exist_ok=True);raw=target/'disc-route.bin'
        selection=','.join(str(job[k]) for k in ('stage','chapter','record','subset','player'))+(','+'4' if job['kind']=='gang' else ',3')
        env=os.environ.copy()
        if job['kind']=='gang':env['JGR_REPLAY_GANG_FIXTURE']=str((a.fixtures/(job['key']+'.txt')).resolve())
        with (target/'execution.log').open('w') as log:
            result=subprocess.run([str(a.probe.resolve()),str(a.gdi.resolve()),str(a.hardware_table.resolve()),str(raw.resolve()),selection],env=env,stdout=log,stderr=subprocess.STDOUT)
        report={'job':job['key'],'returncode':result.returncode,'outputs':[]}
        if not result.returncode:
            data=raw.read_bytes();assert data[:4]==b'TAC2' and (len(data)-16)%4192==0
            sections=[[[] for _ in range(3 if job['kind']=='gang' else 2)] for _ in range(3 if job['kind']=='gang' else 1)]
            for at in range(16,len(data),4192):
                row=data[at:at+4192];word=lambda off:struct.unpack_from('<I',row,off)[0]
                if not word(0xc48):continue
                slot=word(0xc20)-107 if job['kind']=='gang' else 0;part=word(0xc50)
                assert 0<=slot<len(sections) and part<len(sections[slot])
                sections[slot][part].append(row)
            for slot,parts in enumerate(sections):
                actual,_,_,_=pack_sections(parts,job['character'])
                name=f"{job['prefix']}_rival{slot}.bin" if job['kind']=='gang' else 'challenge_'+job['key']+'.bin'
                same=actual==(a.expected/name).read_bytes();(target/name).write_bytes(actual)
                report['outputs'].append({'name':name,'section_samples':list(map(len,parts)),'byte_identical':same,'sha256':hashlib.sha256(actual).hexdigest()})
        reports.append(report);(a.out/'verified.json').write_text(json.dumps(reports,indent=2)+'\n');print(json.dumps(report),flush=True)
    return int(any(r['returncode'] or not all(o['byte_identical'] for o in r['outputs']) for r in reports))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('probe','gdi','hardware-table','fixtures','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--jobs',type=Path,default=ROOT/'tools/portable/capture_jobs.json')
    p.add_argument('--expected',type=Path,default=ROOT/'psp');p.add_argument('--only',action='append')
    sys.exit(run(p.parse_args()))
