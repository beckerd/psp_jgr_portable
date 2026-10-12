#!/usr/bin/env python3
"""Losslessly import Behind the Mask's three original pursuit performances."""
import argparse
from pathlib import Path
from poison_route_assets import ROOT, build as build_routes

SOURCE = ROOT / 'decompile/campaign_captures/behind_the_mask_20260928/dreamcast_poses'

def build(folder=SOURCE, check=False):
    assert 'GANG_ROUTES complete stage=3' in (folder/'flycast.log').read_text()
    # S3M05 starts with a kind-6 demonstration in subset zero. Its live
    # pursuit is record/subset one and uses the unmasked character 15.
    return build_routes(folder, check, prefix='mask', character=15, bank=24, subset=1)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=SOURCE);p.add_argument('--check',action='store_true')
    a=p.parse_args();build(a.source,a.check)
