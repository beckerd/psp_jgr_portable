#!/usr/bin/env python3
"""Build and verify native multi-selection installation from a user's disc."""
import argparse,hashlib,json,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--gdi',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('names',nargs='+');a=p.parse_args()
 a.out.mkdir(parents=True,exist_ok=True);exe=a.out/'session-proof'
 sources=sorted((ROOT/'psp/src').glob('portable_replay*.c'))+[ROOT/'psp/src'/f'{n}.c' for n in ('portable_disc','portable_hardware','portable_tables','portable_convert')]
 inputs=set(sources+[Path(__file__),Path(__file__).with_name('session_probe.c')])
 inputs.update((ROOT/'psp/src').glob('portable_*.h'))
 inputs.update((ROOT/'psp/src').glob('portable_*.inc'))
 inputs.update(ROOT/'psp/src'/name for name in ('source_fmac.h','source_float_fast.h'))
 def source_hashes():return {str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(inputs)}
 source_binding=source_hashes()
 subprocess.run(['cc','-O3','-std=gnu99','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'psp/src'),str(Path(__file__).with_name('session_probe.c')),*map(str,sources),'-lm','-o',str(exe)],check=True)
 if source_binding!=source_hashes():raise SystemExit('Sources changed during compilation; repeat this proof from stable inputs')
 (a.out/'compiled-sources.json').write_text(json.dumps(source_binding,indent=2)+'\n')
 start=time.monotonic()
 with (a.out/'execution.log').open('w') as log:
  run=subprocess.run([str(exe.resolve()),str(a.gdi.resolve()),str(a.assets.resolve()),str(a.out.resolve()),*a.names],stdout=log,stderr=subprocess.STDOUT)
 catalog={r['path']:r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
 report=[]
 for name in a.names:
  f=a.out/name
  if not f.is_file():continue
  data=f.read_bytes();digest=hashlib.sha256(data).hexdigest();wanted=catalog[name]
  report.append({'path':name,'bytes':len(data),'sha256':digest,'exact':len(data)==wanted['bytes'] and digest==wanted['sha256']})
 proof={'native_execution':True,'physical_psp':False,'source_inputs':'original disc + already verified generated bind banks','executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'compiled_sources_sha256':hashlib.sha256((a.out/'compiled-sources.json').read_bytes()).hexdigest(),'seconds':time.monotonic()-start,'exit':run.returncode,'files':report,'complete':run.returncode==0 and len(report)==len(a.names) and all(r['exact'] for r in report)}
 (a.out/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
 if not proof['complete']:raise SystemExit(1)
if __name__=='__main__':main()
