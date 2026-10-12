#!/usr/bin/env python3
"""Compile and execute the real browser stdio/scratch Asyncify boundary in Node."""
import argparse
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE.parent))
from build import WRAPS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emcc', type=Path, default=ROOT/'build/browser/emsdk/upstream/emscripten/emcc.py')
    parser.add_argument('--out', type=Path, default=ROOT/'build/browser/io-probe')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    output = args.out.resolve(); output.mkdir(parents=True, exist_ok=True)
    compiler = [sys.executable, str(args.emcc.resolve())] if args.emcc.suffix == '.py' else [str(args.emcc.resolve())]
    command = compiler + ['-std=gnu99', '-D_GNU_SOURCE', '-O1', '-Wall', '-Wextra', '-Werror',
        '-I'+str(ROOT/'psp/src'), str(HERE/'io_probe.c'), str(HERE.parent/'io.c'),
        str(ROOT/'psp/src/portable_scratch.c'), '-sASYNCIFY=1', '-sMODULARIZE=1',
        '-sENVIRONMENT=node', '-sSTACK_SIZE=1048576', '-sALLOW_MEMORY_GROWTH=1',
        '-sEXPORTED_RUNTIME_METHODS=ccall', '-sEXPORTED_FUNCTIONS=_main']
    command += ['-Wl,--wrap='+name for name in WRAPS]
    command += ['-o', str(output/'probe.cjs')]
    subprocess.run(command, check=True)
    subprocess.run([args.node, str(HERE/'io_probe.mjs'), str(output/'probe.cjs')], check=True)


if __name__ == '__main__':
    main()
