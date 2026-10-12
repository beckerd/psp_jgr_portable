#!/usr/bin/env python3
"""Package the three completed Dreamcast Garam demonstrations without re-simulation."""
import argparse
import json
from title_route_assets import ROOT, bake

def build(check=False):
    reports=[]
    for index in range(3):
        source=ROOT/f'decompile/campaign_captures/garam_manhole_20260917/dc_demo{index}'
        data,report=bake(source)
        assert report['character']==9 and report['model']==8
        target=ROOT/f'psp/garam_demo{index}.bin'
        if check:assert target.read_bytes()==data
        else:target.write_bytes(data)
        reports.append(dict(report,set=index,source=str(source.relative_to(ROOT))))
    text=json.dumps(reports,indent=2)+'\n'
    target=ROOT/'psp/garam_demos.json'
    if check:assert target.read_text()==text
    else:target.write_text(text)
    print('Three original Garam demonstrations:',sum(r['frames'] for r in reports),'frames')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true')
    build(parser.parse_args().check)
