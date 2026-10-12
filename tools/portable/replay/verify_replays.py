#!/usr/bin/env python3
"""Run disc-only replay jobs, then independently compare expected assets."""
import argparse
import json
import subprocess
from pathlib import Path
from verify_bootstrap import verify

def run(a):
    jobs=json.loads(a.jobs.read_text())['jobs'];a.out.mkdir(parents=True,exist_ok=True)
    reports=[]
    for job in jobs:
        if job['kind']!='demo' or (a.only and job['key'] not in a.only):continue
        out=a.out/job['key'];out.mkdir(exist_ok=True)
        raw=out/'disc-route.bin'
        selection=','.join(str(job[k]) for k in ('stage','chapter','record','subset','player'))
        result=subprocess.run([str(a.probe.resolve()),str(a.gdi.resolve()),str(a.hardware_table.resolve()),str(raw.resolve()),selection],capture_output=True,text=True)
        (out/'execution.log').write_text(result.stdout+result.stderr)
        if result.returncode:
            report={'job':job['key'],'execution_error':result.stdout+result.stderr}
        else:
            try:report={'job':job['key'],**verify(raw,a.observations/job['key'],out/'comparison')}
            except Exception as e:report={'job':job['key'],'comparison_error':repr(e)}
        reports.append(report)
        (a.out/'verified.json').write_text(json.dumps(reports,indent=2)+'\n')
        print(json.dumps(report),flush=True)
    return reports

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('probe','gdi','hardware-table','jobs','observations','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--only',action='append')
    run(p.parse_args())
