#!/usr/bin/env python3
"""Compare disc-only STAFF execution against private migration outputs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'decompile/tools'))
import staff_route_assets

def compare_metadata(actual,expected):
    if actual==expected:return {'byte_identical':True,'initial_zero_timing_only':False,'differences':[]}
    a=struct.unpack_from('<4s7I',actual);b=struct.unpack_from('<4s7I',expected)
    if a!=b:return {'byte_identical':False,'initial_zero_timing_only':False,'header_mismatch':True}
    offset=32+a[4]*24+a[5]*32
    if actual[:offset]!=expected[:offset]:return {'byte_identical':False,'initial_zero_timing_only':False,'pose_or_spray_mismatch':True}
    changes=[]
    for i in range(a[6]):
        x=struct.unpack_from('<8I',actual,offset+i*32);y=struct.unpack_from('<8I',expected,offset+i*32)
        if x!=y:changes.append({'index':i,'actual':x,'expected':y})
    return {'byte_identical':False,'initial_zero_timing_only':bool(changes) and all(
        d['actual'][0]==0 and d['expected'][0]==1 and d['actual'][1:]==d['expected'][1:] and
        not any(d['actual'][3:]) for d in changes),'differences':changes}

def run(a):
    a.out.mkdir(parents=True,exist_ok=True);results=[]
    for character in a.characters:
        target=a.out/f'staff{character:02d}';target.mkdir(exist_ok=True)
        with (target/'execution.log').open('w') as log:
            result=subprocess.run([str(a.probe.resolve()),str(a.gdi.resolve()),str(a.hardware_table.resolve()),str(target.resolve()),str(character)],stdout=log,stderr=subprocess.STDOUT)
        record={'character':character,'returncode':result.returncode}
        if result.returncode==0:
            route,meta,report=staff_route_assets.bake(target)
            expected_route=(a.expected/f'staffroute{character:02d}.bin').read_bytes()
            expected_meta=(a.expected/f'staffroute{character:02d}_meta.bin').read_bytes()
            record.update(route_byte_identical=route==expected_route,route_sha256=hashlib.sha256(route).hexdigest(),
                samples=report['frames'],metadata=compare_metadata(meta,expected_meta))
            if (target/'staff_native_meta.bin').exists():record['native_metadata_byte_identical']=(target/'staff_native_meta.bin').read_bytes()==expected_meta
            (target/'route.bin').write_bytes(route);(target/'meta.bin').write_bytes(meta)
        results.append(record);(a.out/'verified.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(record),flush=True)
    return int(any(r['returncode'] or not r['route_byte_identical'] or not(
        r['metadata']['byte_identical'] or r['metadata'].get('initial_zero_timing_only')) for r in results))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('probe','gdi','hardware-table','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--expected',type=Path,default=ROOT/'psp');p.add_argument('--characters',nargs='+',type=int,default=list(range(16)))
    sys.exit(run(p.parse_args()))
