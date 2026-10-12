#!/usr/bin/env python3
"""Create private before/after callback fixtures from a fresh original disc boot.

These are interpreter-development oracles, not reusable installation assets.
No capture receipt accepted by the release asset pipeline is produced.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

def run(a):
    root=Path(__file__).resolve().parents[3]
    sys.path.insert(0,str(root/'tools/portable'))
    from capture import stage_scripts
    from disc import Disc
    if a.out.exists():raise ValueError('Use a new private oracle directory')
    a.out.mkdir(parents=True)
    with Disc(a.gdi) as disc:fingerprint=disc.fingerprint()
    entry=stage_scripts(root,a.out,'demo')
    # The fresh runner's protected callback already catches errors and prevents
    # any original savestate read/write. Add our callback around that wrapper.
    with entry.open('a') as f:f.write('\ndofile('+json.dumps(str(Path(__file__).with_name('callback_oracle.lua')))+')\n')
    for name in ('vmu','saves'):(a.out/name).mkdir()
    env=dict(os.environ,JGR_REFERENCE_OUT=str(a.out)+'/',JGR_REFERENCE_STAGE='1',
             JGR_REFERENCE_CHAPTER='10',JGR_REFERENCE_CHARACTER='0',JGR_REFERENCE_FALL='1',
             JGR_DEMO_CHARACTER='1',JGR_DEMO_RECORD='0',JGR_DEMO_SET='0')
    env['JGR_REPLAY_TICK']=str(a.tick)
    options={'config:LuaFileName':os.path.relpath(entry,a.config.parent),'config:Dynarec.Enabled':'yes',
        'config:rend.ThreadedRendering':'no','config:rend.vsync':'no','config:Debug.GDBEnabled':'no',
        'config:Dreamcast.AutoLoadState':'no','config:Dreamcast.AutoSaveState':'no',
        'config:VMUPath':str(a.out/'vmu'),'config:SavePath':str(a.out/'saves'),'window:fullscreen':'no'}
    opts=','.join(k+'='+json.dumps(v) for k,v in options.items())
    original_config=a.config.read_bytes();process=None
    try:
        with (a.out/'flycast.log').open('wb') as log:
            process=subprocess.Popen([str(a.emulator),'-config',opts,str(a.gdi)],env=env,stdout=log,stderr=subprocess.STDOUT)
            start=time.monotonic()
            while process.poll() is None and not (a.out/'capture.exit').exists() and time.monotonic()-start<a.timeout:time.sleep(.2)
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        if a.config.read_bytes()!=original_config:a.config.write_bytes(original_config)
    metadata=json.loads((a.out/'callback.json').read_text())
    assert (a.out/'before.ram').stat().st_size==(a.out/'after.ram').stat().st_size==16*1024*1024
    metadata['disc']=fingerprint
    metadata['emulator_sha256']=hashlib.sha256(a.emulator.read_bytes()).hexdigest()
    metadata['fixture_sha256']={name:hashlib.sha256((a.out/name).read_bytes()).hexdigest()
                               for name in ('before.ram','after.ram','timer_reads.bin')}
    (a.out/'oracle.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps({k:v for k,v in metadata.items() if k not in ('disc','fixture_sha256')}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('gdi','emulator','config','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--timeout',type=int,default=240)
    p.add_argument('--tick',type=int,default=64)
    a=p.parse_args()
    for name in ('gdi','emulator','config','out'):setattr(a,name,getattr(a,name).resolve())
    run(a)
