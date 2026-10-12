#!/usr/bin/env python3
"""Losslessly import Love Attack's original three chase performances."""
import argparse
from pathlib import Path
from poison_route_assets import ROOT, build as build_routes

SOURCE = ROOT / 'decompile/campaign_captures/love_attack_20260920/dreamcast_poses'

def build(folder=SOURCE, check=False):
    assert 'GANG_ROUTES complete stage=1' in (folder/'flycast.log').read_text()
    return build_routes(folder, check, prefix='love', character=11, bank=3)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=SOURCE);p.add_argument('--check',action='store_true')
    a=p.parse_args();build(a.source,a.check)
