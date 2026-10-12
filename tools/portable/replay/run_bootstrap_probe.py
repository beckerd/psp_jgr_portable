#!/usr/bin/env python3
"""Execute the disc-only PSP replay proof in an isolated emulator directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

def run(a):
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    for source in a.gdi.parent.iterdir():
        if source.suffix.lower() in ('.gdi','.bin','.raw'):
            if source.suffix.lower()=='.gdi':shutil.copyfile(source,out/source.name)
            else:(out/source.name).symlink_to(source.resolve())
    (out/'fsca-hardware.bin').symlink_to(a.hardware_table.resolve())
    (out/'disc-path.txt').write_text(a.gdi.name+'\n')
    shutil.copyfile(a.eboot,out/'EBOOT.PBP')
    config=out/'config.ini'
    config.write_text('[General]\nMemStickDirectory='+str(out/'config/ppsspp')+'\nFirstRun=False\nPauseOnLostFocus=False\nPauseWhenMinimized=False\n'
        '[Graphics]\nGraphicsBackend=0\nSoftwareRenderer=True\nFullScreen=False\n'
        '[CPU]\nCPUCore=1\nCPUSpeed=333\n[Sound]\nEnable=False\n[SystemParam]\nPSPModel=0\n')
    result=out/'replay-result.json'
    with (out/'emulator.log').open('wb') as log:
        process=subprocess.Popen([str(a.emulator.resolve()),'-j',str(out/'EBOOT.PBP'),'--appendconfig='+str(config),
            '--windowed','--loglevel=2','--graphics=software'],cwd=out,
            env={**os.environ,'SDL_AUDIODRIVER':'dummy','XDG_CONFIG_HOME':str(out/'config')},stdout=log,stderr=subprocess.STDOUT)
        started=time.monotonic()
        try:
            while process.poll() is None and time.monotonic()-started<a.timeout:
                if result.exists():break
                time.sleep(.25)
        finally:
            if process.poll() is None:
                process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    report=json.loads(result.read_text())
    report.update(environment='PPSSPP PSP-1000; not physical hardware timing',
        elapsed_host_seconds=time.monotonic()-started,
        eboot_sha256=hashlib.sha256(a.eboot.read_bytes()).hexdigest(),
        hardware_reference_sha256=hashlib.sha256(a.hardware_table.read_bytes()).hexdigest(),
        expected_route_byte_identical=(out/'disc-route.bin').read_bytes()==a.expected.read_bytes(),
        expected_cue_bytes_identical=(out/'disc-route.bin.cues').read_bytes()==Path(str(a.expected)+'.cues').read_bytes())
    (out/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report));assert report['complete_route'] and report['expected_route_byte_identical'] and report['expected_cue_bytes_identical']

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('eboot','gdi','hardware-table','expected','emulator','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--timeout',type=float,default=3600)
    run(p.parse_args())
