#!/usr/bin/env python3
"""Private parity proof: original-file generated controllers vs observed callbacks.

Captures are expected results only. The generator receives only the original
executable, original mission bank, and selector. This does not certify poses,
camera, collision, or cue timelines; those remain a separate replay workload.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

def verify(root, executable, captures, out, cc='cc'):
    root=root.resolve();out.mkdir(parents=True,exist_ok=True)
    binary=out/'replay-proof'
    subprocess.run([cc,'-O2','-std=c99','-Wall','-Wextra','-Werror','-ffp-contract=off',
                    '-I'+str(root/'psp/src'),str(Path(__file__).with_name('probe.c')),
                    str(root/'psp/src/portable_replay_vm.c'),str(root/'psp/src/portable_replay_controller.c'),
                    '-lm','-o',str(binary)],check=True)
    sys.path.insert(0,str(root/'decompile/tools'))
    from mission_chain import chain_offsets,mission_source
    jobs=json.loads((root/'tools/portable/capture_jobs.json').read_text())['jobs']
    receipts=[]
    for job in jobs:
        if job['kind']!='demo':continue
        mission=mission_source(next((root/'decompile/afs/E_MISSIONDATA').glob('*'+job['mission'].lower()+'.bin')))
        data=mission.read_bytes();offset=chain_offsets(data)[job['record']]
        bank=struct.unpack_from('<I',data,offset+16)[0]
        bankpath=root/f'decompile/afs/MISSIONSET/{bank:03d}_mission_set_{bank:02d}.bin'
        trace=out/(job['key']+'.controller')
        result=subprocess.run([str(binary),str(executable),str(bankpath),str(job['subset']),str(trace)],check=True,capture_output=True,text=True)
        report=json.loads(result.stdout)
        generated=list(struct.iter_unpack('<8I',trace.read_bytes()[8:]))
        reference=(Path(captures[job['key']])/'title_actor_camera.bin').read_bytes()
        assert reference[:4]==b'TAC2' and (len(reference)-16)%4192==0
        count=(len(reference)-16)//4192
        assert len(generated)==count+2,(job['key'],'tick count')
        # Creation consumes the zero header before the first observed player
        # clock 2. The last generated controller sample is the end sentinel.
        for i,at in enumerate(range(16,len(reference),4192)):
            expected=[struct.unpack_from('<I',reference,at+0xb90+o)[0]
                      for o in (0x70,0x6c,0x74,0x78)]
            expected[2]&=255
            assert list(generated[i+1][1:5])==expected,(job['key'],i,'controller')
            magnitude=struct.unpack_from('<I',reference,at+0x1b4)[0]
            assert generated[i+1][7]==magnitude,(job['key'],i,'original player magnitude')
        assert generated[-1][5]==1 and all(not r[5] for r in generated[:-1])
        receipts.append(dict(key=job['key'],verified_samples=count,
            source_executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
            source_bank_sha256=hashlib.sha256(bankpath.read_bytes()).hexdigest(),
            reference_sha256=hashlib.sha256(reference).hexdigest(),
            trace_sha256=hashlib.sha256(trace.read_bytes()).hexdigest(),**report))
    result=dict(complete_route=False,controller_parity=True,jobs=len(receipts),
                samples=sum(r['verified_samples'] for r in receipts),receipts=receipts)
    (out/'controller-parity.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='receipts'}))
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--executable',type=Path,required=True)
    p.add_argument('--captures',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--cc',default='cc')
    a=p.parse_args();verify(a.root,a.executable.resolve(),json.loads(a.captures.read_text()),a.out.resolve(),a.cc)
