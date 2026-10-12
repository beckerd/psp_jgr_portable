#!/usr/bin/env python3
"""Build the setup-only EBOOT; the native gameplay PRX lives in its PSAR bundle.

The setup executable deliberately has no game rendering/audio BSS. Executable
replacement releases the importer before gameplay starts. This build step does
not read an original disc or locally converted game assets.
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


def build(psp, bundle, output, sdk, icon0=None, pic1=None):
    if psp.is_symlink() or bundle.is_symlink() or output.is_symlink():
        raise ValueError('Setup build inputs and output must not be linked')
    psp=psp.resolve();bundle=bundle.resolve();output=output.resolve()
    receipt=validate(bundle.read_bytes())
    if len(receipt['entries'])!=5:
        raise ValueError('Setup requires a gameplay PRX and installer recipe in its bundle')
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
    if (folder/'runtime.psar').is_symlink():raise ValueError('Setup bundle must not be linked')
    shutil.copyfile(bundle,folder/'runtime.psar')
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
LDFLAGS += -Wl,--gc-sections
LIBS = -lm -lpsppower -lpspsystemctrl_user
BUILD_PRX = 1
PSP_FW_VERSION = 660
PSP_LARGE_MEMORY = 0
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = JGR Portable
SFOFLAGS = -s DISC_ID=JGRV00001
PSP_EBOOT_PSAR = runtime.psar
PARAM.SFO: Makefile
EBOOT.PBP: Makefile runtime.psar
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
    target=folder/'Makefile'
    if target.is_symlink():raise ValueError('Setup Makefile must not be linked')
    if not target.exists() or target.read_text()!=makefile:target.write_text(makefile)
    env={**os.environ,'PSPDEV':str(sdk),'PSPSDK':str(sdk/'psp/sdk'),
         'PATH':str(sdk/'bin')+os.pathsep+os.environ['PATH']}
    subprocess.run(['make','-j2'],cwd=folder,env=env,check=True)
    temporary=output.with_name(output.name+'.setup-tmp')
    if temporary.is_symlink() or output.is_symlink():raise ValueError('Executable output must not be linked')
    executable, artwork = with_artwork((folder/'EBOOT.PBP').read_bytes(), icon0, pic1)
    temporary.write_bytes(executable);temporary.replace(output)
    report={'schema':1,'scope':'Project build inputs and split executable layout; not complete conversion or hardware validation',
            'setup_sources':inputs,'bundle':receipt,
            'setup_makefile_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
            'setup_elf_sha256':hashlib.sha256((folder/'jgrsetup.elf').read_bytes()).hexdigest(),
            'eboot_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'memory_model':'PSP-1000'}
    if artwork:
        report['private_artwork'] = artwork
    (folder/'build-receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--psp',type=Path,required=True);p.add_argument('--bundle',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--pspdev',type=Path,default=Path(os.environ.get('PSPDEV',Path.home()/'pspdev')))
    p.add_argument('--icon0',type=Path,help='Explicit private 144x80 PNG for the XMB icon')
    p.add_argument('--pic1',type=Path,help='Explicit private 480x272 PNG for the XMB background')
    a=p.parse_args()
    try:build(a.psp,a.bundle,a.out,a.pspdev.resolve(),a.icon0,a.pic1)
    except (OSError,ValueError,subprocess.CalledProcessError) as error:p.exit(1,str(error)+'\n')
