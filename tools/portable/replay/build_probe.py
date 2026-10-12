#!/usr/bin/env python3
"""Build an isolated payload-free PSP original-controller execution proof."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

def build(out, sdk, callback=False, bootstrap=False, api=False, staff_pose=False):
    api=api or staff_pose
    root=Path(__file__).resolve().parents[3]
    out.mkdir(parents=True,exist_ok=True)
    for name in ('portable_replay_vm.c','portable_replay_vm.h','portable_replay_scanline.h','portable_crc32.h','portable_replay_controller.c','portable_replay_controller.h',
                 'portable_replay_cues.c','portable_replay_cues.h','source_fmac.h','source_float_fast.h'):
        shutil.copyfile(root/'psp/src'/name,out/name)
    if bootstrap or api:
        shutil.copyfile(root/'psp/src/portable_assets.h',out/'portable_assets.h')
        for stem in ('portable_replay_arm','portable_replay_maple','portable_replay_gdrom','portable_disc'):
            for suffix in ('.c','.h'):shutil.copyfile(root/'psp/src'/(stem+suffix),out/(stem+suffix))
    if api:
        for suffix in ('.c','.h'):shutil.copyfile(root/'psp/src'/('portable_replay_bootstrap'+suffix),out/('portable_replay_bootstrap'+suffix))
    if api:
        for suffix in ('.h','.inc'):shutil.copyfile(root/'psp/src'/('portable_replay_staff_reuse'+suffix),out/('portable_replay_staff_reuse'+suffix))
    shutil.copyfile(Path(__file__).with_name('staff_pose_psp_probe.c' if staff_pose else 'api_probe.c' if api else 'bootstrap_probe.c' if bootstrap else 'callback_probe.c' if callback else 'probe.c'),out/'probe.c')
    (out/'Makefile').write_text('''TARGET = replay-proof
OBJS = probe.o portable_replay_vm.o portable_replay_controller.o portable_replay_cues.o
CFLAGS = -O2 -G0 -std=gnu99 -Wall -Wextra -ffp-contract=off
LIBS = -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = JGR Replay Installer Proof
include $(PSPSDK)/lib/build.mak
''')
    if bootstrap or api:
        makefile=out/'Makefile'
        makefile.write_text(makefile.read_text().replace('OBJS = probe.o','OBJS = portable_disc.o portable_replay_arm.o portable_replay_maple.o portable_replay_gdrom.o probe.o'))
    if api:
        makefile.write_text(makefile.read_text().replace('OBJS =','OBJS = portable_replay_bootstrap.o'))
    if staff_pose:
        for suffix in ('.c','.h'):shutil.copyfile(root/'psp/src'/('portable_replay_pack'+suffix),out/('portable_replay_pack'+suffix))
        makefile.write_text(makefile.read_text().replace('OBJS =','OBJS = portable_replay_pack.o'))
    env=dict(os.environ);env['PATH']=str(sdk/'bin')+os.pathsep+env['PATH']
    env['PSPDEV']=str(sdk);env['PSPSDK']=str(sdk/'psp/sdk')
    subprocess.run(['make','-j2'],cwd=out,env=env,check=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--pspdev',type=Path,default=Path.home()/'pspdev')
    p.add_argument('--callback',action='store_true',help='Private RAM-fixture callback parity executable, not an installer')
    p.add_argument('--bootstrap',action='store_true',help='Disc-only original startup and complete Gum replay proof')
    p.add_argument('--api',action='store_true',help='Current install-only streaming API and complete Gum replay proof')
    p.add_argument('--staff-pose',action='store_true',help='Private generated-common-input fixture and original-disc STAFF pose API proof')
    a=p.parse_args()
    if sum((a.callback,a.bootstrap,a.api,a.staff_pose))>1:p.error('Select one proof kind')
    build(a.out.resolve(),a.pspdev.resolve(),a.callback,a.bootstrap,a.api,a.staff_pose)
