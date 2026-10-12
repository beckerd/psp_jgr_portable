#!/usr/bin/env python3
"""Recover all 14 Kogane passage records registered by FUN_8c039ef0."""
import argparse
from benten_portals import ROOT, generate

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    text = generate(0x8c17b920, 'kogane', 'kogane_portals.py', count=14)
    path = ROOT/'psp/src/kogane_portals.inc'
    if args.check:
        assert path.read_text() == text, 'Kogane passages differ from original executable'
    else:
        path.write_text(text)
