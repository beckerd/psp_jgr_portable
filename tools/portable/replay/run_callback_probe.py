#!/usr/bin/env python3
"""Private PSP callback parity test. RAM fixtures are never release inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from run_probe import emulate

def run(a):
    a.out.mkdir(parents=True,exist_ok=True)
    for name in ('before.ram','after.ram','timer_reads.bin'):
        path=a.out/name
        if path.exists() or path.is_symlink():path.unlink()
        path.symlink_to(a.oracle/name)
    path=a.out/'fsca-hardware.bin'
    if path.exists() or path.is_symlink():path.unlink()
    path.symlink_to(a.hardware_table)
    d=json.loads((a.oracle/'callback.json').read_text())
    (a.out/'context.txt').write_text(' '.join(str(d[k]) for k in ('actor','stack','fpscr','sr','fpul','fpu_image'))+'\n')
    shutil.copyfile(a.eboot,a.out/'EBOOT.PBP')
    (a.out/'actor.trace').unlink(missing_ok=True)
    result=emulate(a.emulator,a.out)
    result['host_actor_byte_identical']=(a.out/'actor.trace').read_bytes()==a.expected.read_bytes()
    result['host_cues_byte_identical']=(a.out/'actor.trace.cues').read_bytes()==Path(str(a.expected)+'.cues').read_bytes()
    result['eboot_sha256']=hashlib.sha256(a.eboot.read_bytes()).hexdigest()
    result['environment']='PPSSPP PSP-1000 configuration; not physical PSP timing'
    result['complete_route']=False
    (a.out/'verified.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
    assert result['complete_callback'] and result['actor_differing_bytes']==result['cnk_differing_bytes']==0 and result['host_actor_byte_identical'] and result['host_cues_byte_identical']

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('eboot','oracle','hardware-table','expected','emulator','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('eboot','oracle','hardware_table','expected','emulator','out'):setattr(a,name,getattr(a,name).resolve())
    run(a)
