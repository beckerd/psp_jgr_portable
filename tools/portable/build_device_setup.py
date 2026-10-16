#!/usr/bin/env python3
"""Build an unloadable converter PRX and package the game as EBOOT DATA.PSP.

The converter runs before the game initializes libc, then releases its heap,
callbacks and workers before returning to the host. No LoadExec handoff occurs.
"""
import argparse
import hashlib
from io import BytesIO
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import re

from bundle import validate

NATIVE_CORE = ('collide.c', 'ctrl.c', 'pathworld.c', 'pathobject.c', 'pathcoll.c',
               'pathmission.c', 'mission.c', 'mission_quad.c', 'scene.c', 'graffiti.c',
               'graffiti_game.c', 'spray.c', 'canplay.c', 'zones.c', 'staff_replay.c',
               'charpose.c', 'release_report.c', 'source_data_sections.c',
               'source_data_pointers.c', 'source_data_io.c')


def setup_sources(psp):
    """Exact setup translation units and their local include closure.

    Reusing native movement does not import the renderer or its storage. Source
    table definitions are selected from the reviewed replay loader registry.
    Inactive private includes may be absent from the source tree; the
    compiler dependency audit still rejects any active private include.
    """
    root = Path(psp) / 'src'
    units = {p.name for p in root.glob('portable_*.c')}
    units.update(('crash.c', 'release_report.c'))
    files = {p.name for p in root.glob('portable_*') if p.suffix in ('.c', '.h', '.inc')}
    files.update(units)
    files.update(('source_fmac.h', 'source_float_fast.h', 'stage_texture_usage.inc'))
    tables = []
    registry = root / 'portable_native_tables.c'
    if registry.exists():
        tables = ['source_data_' + name + '.c' for name in
                  re.findall(r'\bX\((\w+)\)', registry.read_text())]
        if not tables or len(tables) != len(set(tables)):
            raise ValueError('Invalid native setup table registry')
        units.update(NATIVE_CORE)
        units.update(tables)
        files.update(units)
    pending = list(files)
    while pending:
        name = pending.pop()
        path = root / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Missing or linked setup source: ' + name)
        for included in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', path.read_text(), re.M):
            if '/' in included or '\\' in included:
                # Inactive branches refer to these deliberately absent
                # private inputs. Never copy such
                # a payload if it is present; active missing includes still
                # fail compilation, and compiler dependencies are audited.
                parts = included.split('/')
                if (len(parts) == 3 and parts[:2] == ['generated', 'source_data']
                        and parts[2] not in ('', '.', '..') and '\\' not in included
                        and not (root / included).exists()
                        and not any((root.joinpath(*parts[:i])).is_symlink() for i in range(1, 4))):
                    continue
                raise ValueError('Setup includes must stay in the source directory: ' + included)
            if included not in files and (root / included).exists():
                files.add(included)
                pending.append(included)
    return sorted(units), sorted(files), tables

def artwork_png(path, size):
    """Read explicit private artwork; never discover or load game images."""
    from PIL import Image
    path = Path(path)
    if path.is_symlink():
        raise ValueError('Artwork input must not be linked')
    data = path.read_bytes()
    with Image.open(BytesIO(data)) as image:
        if image.format != 'PNG' or image.size != size or getattr(image, 'is_animated', False):
            raise ValueError('Artwork must be a single %dx%d PNG' % size)
        image.verify()
    return data


def with_artwork(data, icon0=None, pic1=None):
    """Replace only explicit ICON0/PIC1 slots, preserving all executable bytes."""
    if len(data) < 40 or data[:4] != b'\0PBP':
        raise ValueError('Invalid PSP executable container')
    offsets = list(struct.unpack_from('<8I', data, 8))
    if offsets[0] != 40 or offsets != sorted(offsets) or offsets[-1] >= len(data):
        raise ValueError('Invalid PSP executable offsets')
    sections = [data[start:end] for start, end in zip(offsets, offsets[1:] + [len(data)])]
    artwork = []
    for slot, name, path, size in ((1, 'ICON0', icon0, (144, 80)), (4, 'PIC1', pic1, (480, 272))):
        if path is not None:
            sections[slot] = artwork_png(path, size)
            artwork.append({'slot': name, 'source_name': Path(path).name,
                            'bytes': len(sections[slot]),
                            'sha256': hashlib.sha256(sections[slot]).hexdigest(),
                            'public_distribution': False})
    offsets = []; position = 40
    for section in sections:
        offsets.append(position); position += len(section)
    return data[:8] + struct.pack('<8I', *offsets) + b''.join(sections), artwork


def build_module(psp, sdk):
    if psp.is_symlink():raise ValueError('Setup sources must not be linked')
    psp=psp.resolve()
    folder=psp/'.setup';folder.mkdir(exist_ok=True)
    if folder.is_symlink():raise ValueError('Setup build directory must not be linked')
    unit_names, source_names, table_sources = setup_sources(psp)
    sources=[psp/'src'/name for name in unit_names]
    if not sources or not any(p.name=='portable_launcher.c' for p in sources):
        raise ValueError('Missing setup launcher source')
    inputs=[]
    for name in source_names:
        path=psp/'src'/name
        if path.is_symlink():raise ValueError('Setup source must not be linked')
        destination=folder/path.name
        if destination.is_symlink():raise ValueError('Setup build input must not be linked')
        if not destination.exists() or destination.read_bytes()!=path.read_bytes():shutil.copyfile(path,destination)
        inputs.append({'path':'psp/src/'+path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    objects=' '.join(path.stem+'.o' for path in sources)
    makefile='''TARGET = jgrsetup
.DEFAULT_GOAL := all
OBJS = '''+objects+'''
CFLAGS = -O2 -G0 -std=gnu99 -Wall -Wextra -ffp-contract=off -MMD -MP
CFLAGS += -fstack-usage -Werror=frame-larger-than=24576
CFLAGS += -ffunction-sections -fdata-sections -DJGR_DIAGNOSTICS=0
CFLAGS += -DJGR_PORTABLE_ASSETS=1 -DJGR_DEVICE_SETUP=1 -DJGR_SETUP_ME=1 -DJGR_BUNDLED_RUNTIME=1
CFLAGS += -DJGR_SETUP_CRASH=1
CFLAGS += -include portable_assets.h
LDFLAGS += -Wl,--gc-sections,--undefined=__library_exports -nostartfiles
LIBS = -lm -lpsppower
PRX_EXPORTS = exports.exp
BUILD_PRX = 1
PSP_FW_VERSION = 660
PSP_LARGE_MEMORY = 0
$(OBJS): Makefile
include $(PSPSDK)/lib/build.mak
-include $(OBJS:.o=.d)
'''
    if table_sources:
        native_units = [name for name in unit_names if name in NATIVE_CORE or
                        name in table_sources or name.startswith('portable_native_')]
        native_objects = ' '.join(Path(name).stem + '.o' for name in native_units)
        makefile += ('\nCFLAGS += -DJGR_NATIVE_REPLAY=1\n' + native_objects + ': CFLAGS += -DJGR_PORTABLE_ASSETS_IMPLEMENTATION=1'
                     ' -include portable_native_namespace.h\n')
    exports=folder/'exports.exp'
    export_text='PSP_BEGIN_EXPORTS\nPSP_EXPORT_START(syslib, 0, 0x8000)\nPSP_EXPORT_FUNC_HASH(module_start)\nPSP_EXPORT_FUNC_HASH(module_stop)\nPSP_EXPORT_VAR_HASH(module_info)\nPSP_EXPORT_END\nPSP_END_EXPORTS\n'
    if not exports.exists() or exports.read_text()!=export_text:exports.write_text(export_text)
    target=folder/'Makefile'
    if target.is_symlink():raise ValueError('Setup Makefile must not be linked')
    if not target.exists() or target.read_text()!=makefile:target.write_text(makefile)
    env={**os.environ,'PSPDEV':str(sdk),'PSPSDK':str(sdk/'psp/sdk'),
         'PATH':str(sdk/'bin')+os.pathsep+os.environ['PATH']}
    subprocess.run(['make','-j2'],cwd=folder,env=env,check=True)
    report={'schema':2,'scope':'Unloadable converter inputs; hardware validation is separate',
            'setup_sources':inputs,
            'setup_makefile_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
            'setup_elf_sha256':hashlib.sha256((folder/'jgrsetup.elf').read_bytes()).hexdigest(),
            'memory_model':'PSP-1000'}
    (folder/'module-receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def build(psp, bundle, output, sdk, icon0=None, pic1=None):
    if any(p.is_symlink() for p in (psp,bundle,output)):
        raise ValueError('Build inputs must not be linked')
    psp=psp.resolve();bundle=bundle.resolve();output=output.resolve()
    receipt=validate(bundle.read_bytes())
    report=json.loads((psp/'.setup/module-receipt.json').read_text())
    temporary=output.with_name(output.name+'.setup-tmp')
    subprocess.run([str(sdk/'bin/pack-pbp'),str(temporary),str(psp/'PARAM.SFO'),
                    'NULL','NULL','NULL','NULL','NULL',str(psp/'jgrview.prx'),str(bundle)],check=True)
    data,artwork=with_artwork(temporary.read_bytes(),icon0,pic1)
    temporary.write_bytes(data);temporary.replace(output)
    report.update(bundle=receipt,eboot_sha256=hashlib.sha256(data).hexdigest(),
                  game_prx_sha256=hashlib.sha256((psp/'jgrview.prx').read_bytes()).hexdigest(),
                  architecture='game-main-converter-module')
    if artwork:report['private_artwork']=artwork
    (psp/'.setup/build-receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--psp',type=Path,required=True);p.add_argument('--bundle',type=Path)
    p.add_argument('--out',type=Path);p.add_argument('--pspdev',type=Path,default=Path(os.environ.get('PSPDEV',Path.home()/'pspdev')))
    p.add_argument('--icon0',type=Path,help='Explicit private 144x80 PNG for the XMB icon')
    p.add_argument('--pic1',type=Path,help='Explicit private 480x272 PNG for the XMB background')
    p.add_argument('--module-only',action='store_true')
    a=p.parse_args()
    try:
        if a.module_only:build_module(a.psp,a.pspdev.resolve())
        elif a.bundle and a.out:build(a.psp,a.bundle,a.out,a.pspdev.resolve(),a.icon0,a.pic1)
        else:p.error('--bundle and --out are required for packaging')
    except (OSError,ValueError,subprocess.CalledProcessError) as error:p.exit(1,str(error)+'\n')
