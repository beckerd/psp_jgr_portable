#!/usr/bin/env python3
"""Build an isolated native PSP converter test, without source game inputs."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

def build(out,sdk):
    root=Path(__file__).resolve().parents[2];out.mkdir(parents=True,exist_ok=True)
    stems=('portable_startup_cache','portable_startup_warning','portable_copy_worker','portable_assets','portable_disc','portable_convert','portable_resources','portable_misc','portable_hardware',
           'portable_mission_convert','portable_tables','portable_tables_cached','portable_mission_cast','portable_scene_convert','portable_graffiti_convert','portable_grind_convert','portable_roster_convert','portable_bubble','portable_people_convert','portable_install','portable_indexed','portable_models','portable_models_jobs','portable_models_colors','portable_models_pack',
           'portable_world_convert','portable_stage','portable_stage_pack','portable_stage_derived','portable_stage_props','portable_scratch')
    stems+=tuple(p.stem for p in sorted((root/'psp/src').glob('portable_replay*.c')))
    for stem in stems:
        for source in (root/'psp/src').glob(stem+'*'):
            if source.suffix in ('.c','.h','.inc'):shutil.copyfile(source,out/source.name)
    for name in ('portable_quant_octree.inc','portable_indexed_sort.inc','stage_texture_usage.inc',
                 'source_fmac.h','source_float_fast.h','portable_crc32.h','portable_asset_identity.h','source_data_startup_registry.inc'):
        shutil.copyfile(root/'psp/src'/name,out/name)
    for source in (root/'psp/src').glob('portable_world*.inc'):
        shutil.copyfile(source,out/source.name)
    shutil.copyfile(Path(__file__).with_name('install_probe.c'),out/'probe.c')
    objects=' '.join(s+'.o' for s in stems if s!='portable_assets')
    (out/'Makefile').write_text('TARGET = install-proof\n.DEFAULT_GOAL := all\nOBJS = probe.o '+objects+'''
CFLAGS = -O2 -G0 -std=gnu99 -Wall -Wextra -ffp-contract=off
LDFLAGS = -Wl,--wrap=sceIoDevctl
LIBS = -lm -lpsppower
PSP_LARGE_MEMORY = 0
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = JGR Native Setup Proof
PARAM.SFO EBOOT.PBP: Makefile
include $(PSPSDK)/lib/build.mak
''')
    env=dict(os.environ);env['PATH']=str(sdk/'bin')+os.pathsep+env['PATH']
    env['PSPDEV']=str(sdk);env['PSPSDK']=str(sdk/'psp/sdk')
    subprocess.run(['make','-j2'],cwd=out,env=env,check=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--pspdev',type=Path,default=Path.home()/'pspdev')
    a=p.parse_args();build(a.out.resolve(),a.pspdev.resolve())
