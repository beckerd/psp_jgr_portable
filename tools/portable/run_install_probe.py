#!/usr/bin/env python3
"""Check a private native-conversion subset, then boot it with the disc removed.

Uses an isolated emulator Memory Stick. No user settings/saves are changed.
Passing a subset proves those converters only, never a complete release pack.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
from PIL import Image
from install_plan import make_plan,conversion_recipes

ROOT=Path(__file__).resolve().parents[2]
def boot(emulator,root,game,label,timeout):
    result=game/'install-result.json';result.unlink(missing_ok=True)
    cfg=root/'config.ini'
    cfg.write_text('[General]\nMemStickDirectory='+str(root/'config/ppsspp')+'\nFirstRun=False\nAutoRun=True\nPauseOnLostFocus=False\nPauseWhenMinimized=False\n'
        '[Graphics]\nGraphicsBackend=0\nSoftwareRenderer=True\nInternalResolution=1\nFullScreen=False\n'
        '[Sound]\nEnable=False\n[CPU]\nCPUCore=1\nCPUSpeed=333\n[SystemParam]\nPSPModel=0\n')
    env={**os.environ,'SDL_AUDIODRIVER':'dummy','XDG_CONFIG_HOME':str(root/'config')}
    with (root/(label+'.log')).open('wb') as log:
        p=subprocess.Popen([str(emulator),'-j',str(game/'EBOOT.PBP'),'--appendconfig='+str(cfg),
            '--windowed','--loglevel=2','--graphics=software'],cwd=game,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+timeout
            while p.poll() is None and time.monotonic()<deadline:
                if result.exists():break
                time.sleep(.1)
        finally:
            if p.poll() is None:
                p.terminate()
                try:p.wait(timeout=5)
                except subprocess.TimeoutExpired:p.kill();p.wait()
    report=json.loads(result.read_text());(root/(label+'.json')).write_text(json.dumps(report,indent=2)+'\n')
    return report

def run(args):
    root=args.out.resolve();root.mkdir(parents=True,exist_ok=False)
    game=root/'JGR';game.mkdir(parents=True)
    settings=root/'config/ppsspp/PSP/SYSTEM';settings.mkdir(parents=True)
    (settings/'ppsspp.ini').write_text('[General]\nMemStickDirectory='+str(root/'config/ppsspp')+'\nFirstRun=False\nAutoRun=True\n')
    shutil.copyfile(args.eboot,game/'EBOOT.PBP')
    conversions=conversion_recipes();catalog=json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())
    catalog['files']=[r for r in catalog['files'] if r['path'] in conversions]
    plan,coverage=make_plan(catalog,{},conversions);(game/'install.recipe').write_bytes(plan)
    (root/'coverage.json').write_text(json.dumps(coverage,indent=2)+'\n')
    links=[]
    for source in args.gdi.parent.iterdir():
        if source.suffix.lower() in ('.gdi','.bin','.raw'):
            link=game/source.name
            if source.suffix.lower()=='.gdi':shutil.copyfile(source,link)
            else:link.symlink_to(source.resolve())
            links.append(link)
    pixels=b''.join(bytes((i*13%256,i*31%256,i*7%256,i*19%256)) for i in range(128*128))
    (game/'texture-source.rgba').write_bytes(struct.pack('<4I',128,128,64,64)+pixels)
    im=Image.frombytes('RGBA',(128,128),pixels).resize((64,64),Image.Resampling.LANCZOS)
    q=im.quantize(256,method=Image.Quantize.FASTOCTREE);p=q.tobytes()
    expected=bytes(q.getpalette('RGBA'))+b''.join(p[(y+dy)*64+x:(y+dy)*64+x+16]
        for y in range(0,64,8) for x in range(0,64,16) for dy in range(8))
    first=boot(args.emulator.resolve(),root,game,'first',args.timeout)
    assert first['status']==0,first
    for row in catalog['files']:
        generated=game/'data/assets'/row['path'];assert generated.stat().st_size==row['bytes'],row['path']
        assert hashlib.sha256(generated.read_bytes()).hexdigest()==row['sha256'],row['path']
    assert first['indexed']==0 and (game/'texture-output.bin').read_bytes()==expected
    for link in links:link.unlink()  # Local symlinks only; source media untouched.
    (game/'data/assets/assets.manifest').unlink()
    second=boot(args.emulator.resolve(),root,game,'without-disc',args.timeout)
    assert second['status']==0 and second['written']==0 and second['reused']==len(catalog['files']),second
    summary={'environment':'PPSSPP PSP-1000, not physical PSP timing','subset_only':True,
        'emulator_space_query_redirect':'Writable umd0 homebrew folder; only free-space devctl redirected to ms0 by proof harness',
        'converted_files':len(catalog['files']),'converted_bytes':sum(r['bytes'] for r in catalog['files']),
        'all_hashes_match':True,'indexed_mips_match':True,'disc_removed_restart':True,
        'manifest_rebuilt_without_disc':True,'first':first,'second':second}
    (root/'verified.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('eboot','gdi','emulator','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--timeout',type=float,default=240)
    run(p.parse_args())
