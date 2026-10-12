#!/usr/bin/env python3
"""Exercise real guest-address readers in Wasm, including the original crash.

The regression variant changes only the formerly ungrouped model pointer
expression. It must trap, while the current readers must return exact bytes
and reject addresses beyond their declared image.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
TESTS = {
    'portable_models_jobs.c': r'''
int probe_models(void){
 unsigned char bytes[16]={0x78,0x56,0x34,0x12,0xef,0xcd,0xab,0x90};
 Textures t={0};t.code=bytes;t.exe.size=sizeof bytes;
 volatile uint32_t address=0x8c010000u;
 assert(stage_word(&t,address)==0x12345678u);
 assert(stage_word(&t,address+4)==0x90abcdefu);
 assert(!stage_word(&t,address-1)&&!stage_word(&t,address+13));return 0;
}
''',
    'portable_stage.c': r'''
int probe_stage(void){
 unsigned char bytes[16]={0x78,0x56,0x34,0x12,0xef,0xcd,0xab,0x90};
 Stage s={0};s.code=s.data=bytes;s.exe.size=s.source.size=sizeof bytes;
 volatile uint32_t code=EB,data=SB;
 assert(ew(&s,code)==0x12345678u&&ew(&s,code+4)==0x90abcdefu);
 assert(dw(&s,data)==0x12345678u&&dw(&s,data+4)==0x90abcdefu);
 assert(!ew(&s,code-1)&&!ew(&s,code+13));
 assert(!dw(&s,data-1)&&!dw(&s,data+13));return 0;
}
''',
    'portable_people_convert.c': r'''
int probe_people(void){
 unsigned char bytes[16]={0x78,0x56,0x34,0x12};Image i={0};i.data=bytes;i.size=sizeof bytes;
 volatile unsigned address=BASE;
 assert(source(&i,address,4)==bytes&&source(&i,address+4,4)==bytes+4);
 assert(!source(&i,address-1,4)&&!source(&i,address+13,4));return 0;
}
''',
    'portable_native_title.c': r'''
int probe_title(void){
 unsigned char bytes[16]={0x78,0x56,0x34,0x12};TitleSource s={bytes,sizeof bytes};
 volatile unsigned address=TITLE_BASE;
 assert(at(&s,address,4)==bytes&&at(&s,address+4,4)==bytes+4);
 assert(!at(&s,address-1,4)&&!at(&s,address+13,4));return 0;
}
''',
    'portable_replay_vm.c': r'''
int probe_vm(void){
 PortableReplayVM v;unsigned char bytes[16]={0x78,0x56,0x34,0x12};
 portable_replay_vm_init(&v);assert(portable_replay_vm_map(&v,0x8c010000u,bytes,sizeof bytes,PRVM_READ));
 assert(portable_replay_vm_direct_memory(&v,0x8c010000u));volatile unsigned address=0x8c010000u;
 assert(direct_access(&v,address,4,PRVM_READ)==bytes);
 assert(direct_span(&v,address+4,4,PRVM_READ)==bytes+4);
 assert(!direct_access(&v,address-4,4,PRVM_READ)&&!direct_span(&v,address+13,4,PRVM_READ));return 0;
}
'''
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emcc', type=Path, default=ROOT/'build/browser/emsdk/upstream/emscripten/emcc.py')
    parser.add_argument('--out', type=Path, default=ROOT/'build/browser/pointer-probe')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    folder = args.out.resolve(); folder.mkdir(parents=True, exist_ok=True)
    src = ROOT/'psp/src'
    compiler = [sys.executable, str(args.emcc.resolve())] if args.emcc.suffix == '.py' else [str(args.emcc.resolve())]
    flags = ['-O2', '-std=gnu99', '-ffp-contract=off', '-DJGR_PORTABLE_ASSETS=1',
             '-DJGR_PORTABLE_ASSETS_IMPLEMENTATION=1', '-DJGR_DIAGNOSTICS=0', '-D_GNU_SOURCE',
             '-I'+str(src), '-idirafter', str(ROOT/'psp/tests/render_stubs'),
             '-idirafter', str(ROOT/'psp/tests/stubs'), '-sENVIRONMENT=node', '-sASSERTIONS=1']
    calls = ['probe_models','probe_stage','probe_people','probe_title','probe_vm']
    (folder/'main.c').write_text('\n'.join('int '+name+'(void);' for name in calls)+
                                '\nint main(void){return '+'|'.join(name+'()' for name in calls)+';}\n')
    units = []
    for name, body in TESTS.items():
        unit = folder/('probe_'+name)
        unit.write_text('#include <assert.h>\n#include '+json.dumps(str(src/name))+'\n'+body)
        units.append(unit)
    current = folder/'current.cjs'
    subprocess.run(compiler+flags+[str(p) for p in units]+[str(folder/'main.c'), '-lm', '-o', str(current)], check=True)
    subprocess.run([args.node, str(current)], check=True)
    print('Five real guest-address readers pass exact-byte and bounds checks in Wasm.')
    old = folder/'old_models.c'
    code = (src/'portable_models_jobs.c').read_text()
    assert 't->code+(a-0x8c010000)' in code
    old.write_text('#include <assert.h>\n'+code.replace('t->code+(a-0x8c010000)','t->code+a-0x8c010000')+TESTS['portable_models_jobs.c'])
    regression = folder/'original.cjs'
    subprocess.run(compiler+flags+[str(old)]+[str(p) for p in units[1:]]+[str(folder/'main.c'), '-lm', '-o', str(regression)], check=True)
    result = subprocess.run([args.node,str(regression)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    assert result.returncode and 'memory access out of bounds' in result.stdout, result.stdout
    (folder/'original-trap.log').write_text(result.stdout)
    print('Original model pointer expression reproduces the memory-access trap.')


if __name__ == '__main__':
    main()
