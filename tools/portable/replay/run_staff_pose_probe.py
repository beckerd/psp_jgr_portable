#!/usr/bin/env python3
"""Private MIPS STAFF pose proof with explicitly generated common-world fixture.

This is not a full first-install proof. The fixture is never packaged with a
release; a real installation creates it from its first live original-disc run.
"""
import argparse,hashlib,json,os,shutil,subprocess,time
from pathlib import Path

def run(a):
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 for source in a.gdi.parent.iterdir():
  if source.suffix.lower() in ('.gdi','.bin','.raw'):
   if source.suffix.lower()=='.gdi':shutil.copyfile(source,out/source.name)
   else:(out/source.name).symlink_to(source.resolve())
 inputs=[(a.hardware_table,'fsca-hardware.bin'),(a.common,'common.swi'),(a.metadata,'common-meta.bin'),(a.chars,'chars.jgc'),(a.spray,'spraymesh.jgc')]
 for source,name in inputs:(out/name).symlink_to(source.resolve())
 (out/'disc-path.txt').write_text(a.gdi.name+'\n');(out/'character.txt').write_text(str(a.character)+'\n');shutil.copyfile(a.eboot,out/'EBOOT.PBP')
 config=out/'config.ini';config.write_text('[General]\nMemStickDirectory='+str(out/'config/ppsspp')+'\nFirstRun=False\nPauseOnLostFocus=False\nPauseWhenMinimized=False\n'
  '[Graphics]\nGraphicsBackend=0\nSoftwareRenderer=True\nFullScreen=False\n[CPU]\nCPUCore=1\nCPUSpeed=333\n[Sound]\nEnable=False\n[SystemParam]\nPSPModel=0\n')
 result=out/'replay-result.json';started=time.monotonic()
 with (out/'emulator.log').open('wb') as log:
  process=subprocess.Popen([str(a.emulator.resolve()),'-j',str(out/'EBOOT.PBP'),'--appendconfig='+str(config),'--windowed','--loglevel=2','--graphics=software'],cwd=out,env={**os.environ,'SDL_AUDIODRIVER':'dummy','XDG_CONFIG_HOME':str(out/'config')},stdout=log,stderr=subprocess.STDOUT)
  try:
   while process.poll() is None and time.monotonic()-started<a.timeout:
    if result.exists():break
    time.sleep(.25)
  finally:
   if process.poll() is None:
    process.terminate()
    try:process.wait(timeout=5)
    except subprocess.TimeoutExpired:process.kill();process.wait()
 report=json.loads(result.read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 report.update(environment='PPSSPP PSP-1000; concurrent host load; not physical hardware timing',scope='MIPS pose path using a private host-generated common-world input fixture; not a full first-install proof',elapsed_host_seconds=time.monotonic()-started,eboot_sha256=sha(a.eboot),fixture_sha256=sha(a.common),route_byte_identical=(out/'staffroute.bin').read_bytes()==a.expected.read_bytes(),metadata_byte_identical=(out/'staffroute_meta.bin').read_bytes()==a.expected_metadata.read_bytes())
 (out/'verified.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report));assert report['result']==1 and report['route_byte_identical'] and report['metadata_byte_identical']
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ['eboot','gdi','hardware-table','common','metadata','chars','spray','expected','expected-metadata','emulator','out']:p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--character',type=int,required=True);p.add_argument('--timeout',type=float,default=3600)
 run(p.parse_args())
