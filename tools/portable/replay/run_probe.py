#!/usr/bin/env python3
"""Run the PSP/MIPS installer proof in an isolated emulator directory.

This proves target execution/output only, not physical PSP speed or complete
route generation. Original inputs are locally linked; no game data is bundled.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

def emulate(emulator,out):
    (out/'replay_result.json').unlink(missing_ok=True)
    config=out/'config.ini'
    config.write_text('[Graphics]\nGraphicsBackend = 0\n[Sound]\nEnable = False\n[SystemParam]\nPSPModel = 0\n')
    command=[str(emulator),'-j',str(out/'EBOOT.PBP'),'--appendconfig='+str(config),
             '--windowed','--loglevel=2','--graphics=software']
    with (out/'emulator.log').open('wb') as log:
        process=subprocess.Popen(command,cwd=out,env={**os.environ,'SDL_AUDIODRIVER':'dummy'},stdout=log,stderr=subprocess.STDOUT)
        try:
            start=time.monotonic()
            while process.poll() is None and time.monotonic()-start<30:
                if (out/'replay_result.json').exists():break
                time.sleep(.1)
        finally:
            if process.poll() is None:
                process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    return json.loads((out/'replay_result.json').read_text())

def run(eboot, exe, bank, expected, emulator, out):
    out.mkdir(parents=True,exist_ok=True)
    original=out/'original';original.mkdir(exist_ok=True)
    for source,name in ((exe,'1ST_READ.BIN'),(bank,'mission_set.bin')):
        dest=original/name
        if dest.exists() or dest.is_symlink():dest.unlink()
        dest.symlink_to(source.resolve())
    shutil.copyfile(eboot,out/'EBOOT.PBP')
    (out/'controller.trace').unlink(missing_ok=True)
    report=emulate(emulator,out)
    report['controller_byte_identical']=expected.read_bytes()==(out/'controller.trace').read_bytes()
    report['eboot_sha256']=hashlib.sha256(eboot.read_bytes()).hexdigest()
    report['trace_sha256']=hashlib.sha256((out/'controller.trace').read_bytes()).hexdigest()
    report['environment']='PPSSPP PSP-1000 configuration; not physical PSP timing'
    (out/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    assert report['complete_controller'] and report['controller_byte_identical']
    print(json.dumps(report))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('eboot','executable','bank','expected','emulator','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();run(a.eboot.resolve(),a.executable.resolve(),a.bank.resolve(),a.expected.resolve(),a.emulator.resolve(),a.out.resolve())
