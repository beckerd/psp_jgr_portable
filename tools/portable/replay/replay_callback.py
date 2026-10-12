#!/usr/bin/env python3
"""Replay one private original callback fixture; never a release converter."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import struct

def run(oracle,table,out):
    root=Path(__file__).resolve().parents[3];out.mkdir(parents=True,exist_ok=True)
    binary=out/'callback-proof'
    subprocess.run(['cc','-O2','-std=c99','-Wall','-Wextra','-Werror','-ffp-contract=off',
        '-I'+str(root/'psp/src'),str(Path(__file__).with_name('callback_probe.c')),
        str(root/'psp/src/portable_replay_vm.c'),str(root/'psp/src/portable_replay_cues.c'),
        '-lm','-o',str(binary)],check=True)
    d=json.loads((oracle/'callback.json').read_text())
    result=subprocess.run([str(binary),str(oracle/'before.ram'),str(oracle/'after.ram'),
        str(oracle/'timer_reads.bin'),str(d['actor']),str(d['stack']),str(d['fpscr']),
        str(d.get('sr',0)),str(d.get('fpul',0)),str(d.get('fpu_image',0)),
        str(table),str(out/'vm-actor.bin')],capture_output=True,text=True)
    print(result.stdout,end='');report=json.loads(result.stdout)
    before=(oracle/'before.ram').read_bytes()
    tick=struct.unpack_from('<I',before,d['actor']-0x8c000000+0x98)[0]
    with (oracle/'audio.csv').open() as stream:
        expected=[tuple(int(row[k]) for k in ('tick','kind','bank','program','parent','task'))
                  for row in csv.DictReader(stream) if int(row['tick'])==tick and int(row['parent'])==d['actor']]
    actual=list(struct.iter_unpack('<6I',(out/'vm-actor.bin.cues').read_bytes()))
    report['cue_records_byte_identical']=actual==expected
    report['source_tick']=tick
    (out/'vm-result.json').write_text(json.dumps(report,indent=2)+'\n')
    if report['fault'] and 0x8c000000<=report['pc']<0x8d000000:
        import capstone
        cs=capstone.Cs(capstone.CS_ARCH_SH,capstone.CS_MODE_SH4|capstone.CS_MODE_SHFPU|capstone.CS_MODE_LITTLE_ENDIAN)
        pc=report['pc'];data=before[pc-0x8c000000:pc-0x8c000000+12]
        for ins in cs.disasm(data,pc):print(hex(ins.address),ins.mnemonic,ins.op_str)
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('oracle','hardware-table','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();run(a.oracle.resolve(),a.hardware_table.resolve(),a.out.resolve())
