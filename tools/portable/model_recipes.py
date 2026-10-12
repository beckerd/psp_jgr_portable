"""Payload-free recipes for the on-device JGC model builder.

Rows contain source names, original object/motion extents, and converter options.
Texture maps are read from PLAY/PTEX on-device or selected by one source index;
copied arbitrary texmap arrays are deliberately not accepted by this format.
"""
from pathlib import Path
import importlib.util
import json
import math
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch

MAGIC = b'JGRJGC1\0'
HEADER = struct.Struct('<8s6I')
ROW = struct.Struct('<192s192s192s16s4f20I')
OPERATION = 30
MAX_ROWS = 4096
ALLOWED_OPTIONS = {'motions', 'lod', 'texmax', 'toon', 'nolight', 'source-material',
                   'source-environment', 'keep-textures', 'texwhite', 'compact', 'mesh-only',
                   'texture-sector-tail', 'texmax-overrides', 'legacy', 'original-culling', 'body-before-decal', 'city-palette', 'output-lighting', 'face-spray', 'face-surface', 'face-expression', 'shared-cast'}


def source_name(name):
    if name.startswith(('afs/', 'extracted/')):
        name = 'decompile/'+name
    pieces = name.split('/')
    if not name.startswith(('decompile/afs/', 'decompile/extracted/')) or any(
            not p or p in ('.', '..') for p in pieces) or '\\' in name or '\0' in name:
        raise ValueError('Invalid model source path')
    encoded = name.encode('ascii')
    if len(encoded) >= 192:
        raise ValueError('Model source path too long')
    return encoded


def power_of_two(value):
    value = int(value)
    if value < 16 or value > 512 or value & (value-1):
        raise ValueError('Texture cap must be a power of two from 16 to 512')
    return value


def recipe(specs, options=None):
    """Return one {operation, source, args, block} conversion descriptor.

    args[0] is zero until the caller appends block to its installation plan;
    args[1] is also zero until the caller fills the exact block byte length. No filesystem input is required.
    """
    options = dict(options or {})
    unknown = set(options)-ALLOWED_OPTIONS
    if unknown:
        raise ValueError('Unsupported model options: '+', '.join(sorted(unknown)))
    specs = [line.strip() for line in specs if line.strip() and not line.lstrip().startswith('#')]
    if not specs or len(specs) > MAX_ROWS:
        raise ValueError('Invalid model row count')
    cap = power_of_two(options.get('texmax', 128))
    if options.get('keep-textures', 'all') not in ('all', 'mapped'):
        raise ValueError('Only keep-textures=all or mapped is supported')
    global_flags = (2 if options.get('keep-textures') == 'all' else 64 if options.get('keep-textures') == 'mapped' else 0) | (4 if 'texwhite' in options else 0)
    if 'compact' in options and 'mesh-only' in options:
        raise ValueError('Choose one JGC derivative')
    global_flags |= 8 if 'compact' in options else 16 if 'mesh-only' in options else 0
    global_flags |= 32 if 'texture-sector-tail' in options else 0
    global_flags |= 128 if 'legacy' in options else 0
    global_flags |= 256 if 'original-culling' in options else 0
    global_flags |= 512 if 'body-before-decal' in options else 0
    global_flags |= 16384 if 'shared-cast' in options else 0
    global_flags |= 1024 if 'output-lighting' in options else 0
    global_flags |= 2048 if 'face-spray' in options else 4096 if 'face-surface' in options else 8192 if 'face-expression' in options else 0
    defaults = (0 if 'nolight' in options else 1) | (2 if 'toon' in options else 0)
    defaults |= 4 if 'source-material' in options else 0
    defaults |= 8 if 'source-environment' in options else 0
    defaults |= 64 if 'city-palette' in options else 0
    overrides = []
    if 'texmax-overrides' in options:
        seen = set()
        for pair in str(options['texmax-overrides']).split(','):
            index, maximum = pair.split(':')
            index, maximum = int(index), power_of_two(maximum)
            if not 0 <= index < 8192 or index in seen:
                raise ValueError('Invalid texture cap override')
            seen.add(index)
            overrides.append((index, maximum))
        overrides.sort()
    rows, tails, first_source = [], bytearray(), None
    tail_start = HEADER.size+len(specs)*ROW.size
    for spec in specs:
        parts = spec.split(':')
        if len(parts) < 4:
            raise ValueError('Incomplete model row')
        name, source, textures, placement = parts[:4]
        encoded_name = name.encode('ascii')[:15]
        if not encoded_name or '\0' in name:
            raise ValueError('Invalid model name')
        source, textures = source_name(source), source_name(textures)
        first_source = first_source or source.decode()
        place = [float(n) for n in placement.split(',')]
        if len(place) != 4 or any(not math.isfinite(v) for v in place):
            raise ValueError('Invalid port placement')
        positional, extra = [], {}
        for part in parts[4:]:
            if '=' in part:
                key, value = part.split('=', 1)
                if key in extra:
                    raise ValueError('Duplicate model row option')
                extra[key] = value
            else:
                positional.append(part)
        if set(extra)-{'mot', 'tex', 'blend', 'cull', 'motionimage', 'lighting', 'alpha', 'material', 'texbinding', 'shadow', 'palette', 'texcap', 'collision-tree'}:
            raise ValueError('Unsupported row option; texmap arrays require source reader instructions')
        flags = defaults | (4 if extra.get('material') == '1' else 0)
        if 'collision-tree' in extra:
            if extra['collision-tree']!='1':raise ValueError('Invalid collision tree selector')
            flags|=512
        if 'palette' in extra:
            if extra['palette']!='1':raise ValueError('Invalid palette selector')
            flags|=64
        if 'shadow' in extra:
            if extra['shadow'] not in ('35','45') or not (flags&64 or global_flags&14336):raise ValueError('Unsupported port shadow lift')
            flags |= 128 if extra['shadow']=='35' else 256
        if extra.get('alpha') == '1':
            flags |= 16
        if 'alpha' in extra and extra['alpha'] != '1':
            raise ValueError('Invalid alpha override')
        if 'cull' in extra:
            if extra['cull'] != 'none':
                raise ValueError('Unsupported cull mode')
            flags |= 32
        lighting = int(extra.get('lighting', 0))
        if lighting not in (0, 1, 2) or (lighting and not flags & 8):
            raise ValueError('Invalid original lighting mode')
        blend = 0
        if 'blend' in extra:
            values = [int(v) for v in extra['blend'].split(',')]
            if len(values) != 2 or any(not 0 <= v < 8 for v in values):
                raise ValueError('Invalid blend factors')
            blend = 16 | values[0] << 5 | values[1] << 8
        motions = 0 if 'output-lighting' in options else int(extra.get('mot', options.get('motions', 12)))
        if not 0 <= motions <= 4096:
            raise ValueError('Invalid motion limit')
        motion_source, motion_base = b'', 0
        if 'motionimage' in extra:
            image, base = extra['motionimage'].rsplit('@', 1)
            motion_source, motion_base = source_name(image), int(base, 16)
        headers = []
        if not positional:
            mode, base, root = 0, 0, int(options.get('lod', 0))
            if not 0 <= root < 64:
                raise ValueError('Invalid PLAY LOD')
        elif len(positional) == 4 and positional[0] == 'rawat':
            mode, base, root = 1, int(positional[1], 16), int(positional[2], 16)
            headers = [int(v, 16) for v in positional[3].split(',') if int(v, 16)]
            if len(headers) > 4096 or any(v < (motion_base if motion_source else base) for v in headers) or root < base:
                raise ValueError('Invalid source extent')
        else:
            raise ValueError('Unsupported root discovery mode')
        selector = int(extra['tex']) if 'tex' in extra else 0xffffffff
        if selector != 0xffffffff and not 0 <= selector < 8192:
            raise ValueError('Invalid texture selector')
        offset = tail_start+len(tails) if headers else 0
        tails.extend(struct.pack('<'+'I'*len(headers), *headers))
        binding = [0]*6
        if 'texbinding' in extra:
            fields = extra['texbinding'].split(',')
            if len(fields) != 6:
                raise ValueError('Texture binding needs image kind, base, loader table, list, texture base, offset mode')
            binding = [int(fields[0]), *[int(v, 16) for v in fields[1:5]], int(fields[5])]
            if binding[0] not in (1, 2, 3) or binding[5] not in (0, 1, 2, 3, 4) or selector != 0xffffffff:
                raise ValueError('Invalid original texture binding reader')
        caps_at = tail_start+len(tails) if overrides and not rows else 0
        caps_count = len(overrides) if caps_at else 0
        if caps_at:
            tails.extend(b''.join(struct.pack('<2I', *entry) for entry in overrides))
        params = [mode, base, root, motions, offset, len(headers), motion_base,
                  selector, flags, lighting, blend, power_of_two(extra.get('texcap',cap))]+binding+[caps_at, caps_count]
        if any(not 0 <= n <= 0xffffffff for n in params):
            raise ValueError('Model parameter outside u32')
        rows.append(ROW.pack(source, textures, motion_source, encoded_name, *place, *params))
    block = HEADER.pack(MAGIC, 1, ROW.size, len(rows), cap, global_flags, 0)+b''.join(rows)+tails
    return dict(operation=OPERATION, source=first_source, args=(0, 0, 0, 0), block=block,
                specs=specs, options=options)


def recipes(tree=None):
    """Basic banks with no postprocessing; requires freshly derived specs.

    This API does not claim that a pre-existing generated spec is proof of a
    fresh conversion. Callers retain the disc-conversion receipt separately.
    """
    if tree is None:
        metadata = json.loads(Path(__file__).with_name('model_jobs.json').read_text())
        if metadata.get('schema') != 1 or metadata.get('kind') != 'model-source-reader-metadata':
            raise ValueError('Unsupported public model metadata')
        return {name: recipe(row['specs'], row['options']) for name, row in metadata['jobs'].items()}
    tree = Path(tree)
    jobs = {}
    for stage in (1, 2, 3, 5):
        spec = tree/f'decompile/people{stage}_spec.txt'
        if spec.is_file():
            jobs[f'people{stage}.jgc'] = recipe(spec.read_text().splitlines())
            jobs[f'people{stage}_compact.jgc'] = recipe(spec.read_text().splitlines(), {'compact': 1})
    spec = tree/'decompile/record_book_spec.txt'
    if spec.is_file():
        jobs['record_book.jgc'] = recipe(spec.read_text().splitlines(),
            {'nolight': 1, 'source-material': 1, 'source-environment': 1, 'texmax': 256})
    for module, output, directory in (
        ('can_hud', 'canhud.jgc', False),
        ('countdown_hud', 'countdown.jgc', False),
        ('enemy_warning', 'enemywarning.jgc', False),
        ('failure_hud', 'failurehud.jgc', False),
        ('loading', 'loading.jgc', False),
        ('pause_hud', 'pausehud.jgc', False),
        ('pause_map', 'pausemap.jgc', False),
        ('police_hud', 'policehud.jgc', False),
        ('practice_hud', 'practicehud.jgc', True),
        ('graffiti_arrow', 'tagarrows.jgc', False),
    ):
        path = tree/f'decompile/tools/{module}_assets.py'
        if path.is_file():
            jobs[output] = capture_first(path, directory)
    jobs.update(event_recipes(tree))
    jobs.update(movie_recipes(tree))
    jobs.update(additional_recipes(tree))
    shared=shared_specs(tree)
    jobs['chars.jgc']=recipe(shared,{'texmax':256,'shared-cast':1})
    jobs['chars_compact.jgc']=recipe(shared,{'texmax':256,'shared-cast':1,'compact':1})
    jobs['playerlight.bin']=recipe(shared,{'texmax':256,'output-lighting':1})
    return jobs


def event_recipes(tree):
    """Derive EVENT texture lookup instructions, not their mapped contents."""
    path = Path(tree).resolve()/'decompile/tools/event_assets.py'
    if not path.is_file():
        return {}
    sys.path.insert(0, str(path.parent))
    try:
        spec = importlib.util.spec_from_file_location('_portable_model_event_catalog', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        ex = module.executable()
        jobs = {}
        for event in module.catalog():
            if not event.get('models'):
                continue
            image = module.Image((module.DEC/event['source']).read_bytes(), module.EVENT_BASE)
            commands = [word for phase in range(3) for _, word in image.commands(image.u(module.EVENT_BASE+4*phase))]
            table = next((word[1] for word in commands if word[0] == 0x56), 0)
            for model in event['models']:
                if model['role'] == 'typed':
                    kind = model['type']
                    owner = {23: 5, 24: 8, 25: 9}.get(kind, kind)
                    record = ex.u(ex.u(0x8c05be50)+owner*4)
                    binding = (1, 0x8c010000, ex.u(record+12), model['texlist'], 0x8ca00000, 1)
                else:
                    binding = (2, module.EVENT_BASE, table, model['texlist'], 0x8cda0000, 1)
                row = (f"Event_{model['root']:x}:{model['source']}:{model['textures']}:0,0,0,0:"
                       f"rawat:{model['source_base']:x}:{model['root']:x}:"+
                       (','.join(f'{v:x}' for v in model['motions']) or '0')+
                       ':lighting='+('2' if model['flags'] & 2 else '1')+
                       f":motionimage={model['motion_source']}@{module.EVENT_BASE:x}"+
                       f':texbinding={binding[0]},'+','.join(f'{v:x}' for v in binding[1:5])+f',{binding[5]}')
                if 'cull' in model:
                    row += ':cull='+model['cull']
                options = {'motions': len(model['motions']), 'source-material': 1, 'source-environment': 1}
                if model.get('texture_sector_tail'):
                    options['texture-sector-tail'] = 1
                jobs[model['asset']] = recipe([row], options)
        return jobs
    finally:
        sys.path.pop(0)


def movie_recipes(tree):
    path = Path(tree).resolve()/'decompile/tools/movie_assets.py'
    if not path.is_file():
        return {}
    sys.path.insert(0, str(path.parent))
    try:
        spec = importlib.util.spec_from_file_location('_portable_model_movie_catalog', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        jobs = {}
        for movie in module.catalog()['movies']:
            data = (module.DEC/movie['source']).read_bytes()
            table = struct.unpack_from('<I', data, 8)[0]
            rows = []
            for segment in movie['segments']:
                for actor in segment['actors']:
                    rows.append(f"Movie_{actor['address']:x}:{movie['source']}:{movie['textures']}:0,0,0,0:"
                                f"rawat:{module.MOVIE_BASE:x}:{actor['root']:x}:0:lighting="+
                                ('2' if actor['flags'] & 2 else '1')+
                                f":texbinding=2,{module.MOVIE_BASE:x},{table:x},{actor['texlist']:x},{module.MOVIE_BASE:x},1")
            options = {'motions': 0, 'source-material': 1, 'source-environment': 1,
                       'texmax': movie['texture_max']}
            if movie.get('texture_max_overrides'):
                options['texmax-overrides'] = ','.join(f'{index}:{cap}' for index, cap in movie['texture_max_overrides'].items())
            jobs[f"movie{movie['movie']:02d}.jgc"] = recipe(rows, options)
        return jobs
    finally:
        sys.path.pop(0)


def capture_first(module_path, directory=False, function="bake", normalize=False, before=(), after=(), no_output=False):
    """Evaluate an existing source-reader up to its first model conversion.

    The subprocess hook never executes the converter and aborts immediately,
    before exporter postprocessing or output writes. Only its parser options
    and source extents become a recipe; unsupported options fail explicitly.
    """
    class Captured(BaseException):
        def __init__(self, command):
            self.command = command

    original_run=subprocess.run
    def intercept(command, *args, **kwargs):
        if len(command)>1 and Path(command[1]).name=='static_texmap.py':
            return original_run(command,*args,**kwargs)
        if len(command) < 3 or Path(command[1]).name != 'jgc_convert.py':
            raise ValueError('Unexpected subprocess while collecting model source metadata')
        if normalize:
            command=list(command)
            keep=next((arg for arg in command[3:] if arg.startswith('--keep-textures=') and arg.split('=',1)[1] not in ('all','mapped')),None)
            if keep:
                selected={int(v) for v in keep.split('=',1)[1].split(',')}
                mapped={int(v) for row in command[3:] if not row.startswith('--') for item in row.split(':') if item.startswith('texmap=') for v in item.split('=',1)[1].split(',')}
                if selected!=mapped:raise ValueError('Texture keep subset is not source-bound')
                command[command.index(keep)]='--keep-textures=mapped'
            command[3:]=[arg if arg.startswith('--') else source_texture_binding(arg,module_path.parent.parent) for arg in command[3:]]
        raise Captured(command)

    module_path = Path(module_path).resolve()
    spec = importlib.util.spec_from_file_location('_portable_model_'+module_path.stem, module_path)
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(module_path.parent))
    try:
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix='model-metadata-') as temp, patch.object(subprocess, 'run', intercept):
            try:
                output = () if no_output else (Path(temp) if directory else Path(temp)/'model.jgc',)
                getattr(module,function)(*before,*output,*after)
            except Captured as event:
                options, rows = {}, []
                for arg in event.command[3:]:
                    if arg.startswith('--'):
                        key, value = arg[2:].split('=', 1)
                        options[key] = value
                    else:
                        rows.append(arg)
                if normalize:
                    rows = [source_texture_binding(row,module_path.parent.parent) for row in rows]
                return recipe(rows, options)
            raise ValueError('Model exporter did not request conversion')
    finally:
        sys.path.pop(0)


def additional_recipes(tree):
    """Source-address recipes for ancillary banks, with no decoded map payload."""
    tree = Path(tree).resolve()
    dec = tree/'decompile'
    ex = (dec/'extracted/1ST_READ.BIN').read_bytes()
    u = lambda data, offset: struct.unpack_from('<I', data, offset)[0]
    eu = lambda address: u(ex, address-0x8c010000)
    jobs = {}
    sys.path.insert(0, str(dec/'tools'))
    try:
        def module(name):
            spec = importlib.util.spec_from_file_location('_portable_extra_'+name, dec/'tools'/f'{name}.py')
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            return mod
        rivals = module('event_runtime_assets')
        chars = (dec/'chars_spec.txt').read_text().splitlines()
        for entry in rivals.rival_assets(False):
            name = chars[rivals.MODELS[entry['character']]].split(':')[0]
            texture = (dec/entry['texture']).read_bytes()
            row = (f"{name}:{entry['source']}:{entry['texture']}:0,0,0,0:rawat:0:{entry['root']:x}:"+
                   (','.join(f'{m:x}' for m in entry['motions']) or '0')+
                   f":lighting=2:texbinding=3,0,{u(texture,4):x},{entry['texlist']:x},0,0")
            jobs[entry['asset']] = recipe([row], {'toon': 1, 'source-material': 1, 'source-environment': 1,
                                                 'motions': len(entry['motions'])})
        ending = module('ending_assets')
        for movie in ending.manifest()['movies']:
            data = (dec/movie['source']).read_bytes()
            rows = []
            for segment in movie['segments']:
                for actor in segment['actors']:
                    rows.append(f"Ending_{actor['address']:x}:{movie['source']}:{movie['textures']}:0,0,0,0:"
                                f"rawat:{ending.B:x}:{actor['root']:x}:0:lighting="+
                                ('2' if actor['flags'] & 2 else '1')+
                                f":texbinding=2,{ending.B:x},{u(data,8):x},{actor['texlist']:x},{ending.B:x},0")
            jobs[f"ending{movie['movie']}.jgc"] = recipe(rows, {'source-material': 1, 'source-environment': 1,
                                                             'texmax': 512 if movie['movie']==25 else 128})
        game = (dec/'afs/U_GAME/000_gamedispdat.bin').read_bytes()
        for name, dat, tex, base, root, motion in (
            ('finishhud', 'afs/U_GAME/000_gamedispdat.bin', 'afs/U_GAME/001_gamedisptxr.bin', 0x8c980000,
             u(game,0x1b4),u(game,0x1b8)),
            ('impactfx', 'afs/PLAYER/016_player_effect_dat.bin', 'afs/PLAYER/017_player_effect_txr.bin',0x8c8e1000,
             eu(0x8c182fec),eu(0x8c183090))):
            row=f'{name}:{dat}:{tex}:0,0,0,0:rawat:{base:x}:{root:x}:{motion:x}:lighting=1'
            jobs[name+'.jgc']=recipe([row], {'motions':1,'source-material':1,'source-environment':1})
        # Original EFTX selectors are read through their loader table, not stored as atlas numbers.
        texname='afs/PLAYER/017_player_effect_txr.bin'
        texture=(dec/texname).read_bytes()
        rows=[]
        for name,slot in (('HealthSmall',40),('HealthLarge',18)):
            root,motion=eu(0x8c182fe8+slot*4),eu(0x8c18308c+slot*4)
            tl=u(texture,u(texture,12)+slot*4)
            rows.append(f'{name}:afs/PLAYER/016_player_effect_dat.bin:{texname}:0,0,0,0:'
                        f'rawat:8c8e1000:{root:x}:{motion:x}:lighting=1:texbinding=3,0,{u(texture,4):x},{tl:x},0,0')
        jobs['healthpickups.jgc']=recipe(rows,{'nolight':1,'source-material':1,'source-environment':1})
        # Original garage texlist initializers store their GBIX pointers immediately after count.
        garage=module('garage_cast')
        rows=garage.extract()[0].splitlines()
        lists=[0x8c10e410]+[eu(0x8c105aec+i*4) for i in range(15)]+[0x8c10e2b4,0x8c10e55c,0x8c10e58c,0x8c10e284]
        def inline_rows(rows, lists):
            result=[]
            assert len(rows)==len(lists)
            for row,tl in zip(rows,lists):
                parts=[part for part in row.split(':') if not part.startswith('texmap=')]
                result.append(':'.join(parts)+f':texbinding=1,8c010000,0,{tl:x},8c800000,2')
            return result
        jobs['garage.jgc']=recipe(inline_rows(rows,lists),{'source-material':1,'source-environment':1,'texmax':256})
        pending=module('garage_pending_assets')
        rows=pending.extract()[0].splitlines()
        lists=[eu(0x8c105aec+i*4) for i in (3,4,5,6,7,8,9,12)]
        jobs['garage_pending.jgc']=recipe(inline_rows(rows,lists),{'source-material':1,'source-environment':1,'texmax':256})
        for index,original in enumerate(chars[:16]):
            name,dat,tex,placement=original.split(':')[:4]
            data,texture=(dec/dat).read_bytes(),(dec/tex).read_bytes()
            head,thead=u(data,8),u(texture,8)
            table=u(data,4);motions_at=0
            while u(data,table):
                if u(data,table)==3:motions_at=u(data,table+4)
                table+=8
            assert motions_at
            motions=[u(data,motions_at+4*i) for i in range(176)]
            row=(f'{name}:{dat}:{tex}:{placement}:rawat:0:{u(data,head+0x24):x}:'+
                 ','.join(f'{m:x}' for m in motions)+f':texbinding=3,0,{u(texture,4):x},{u(texture,thead+0x24):x},0,0')
            jobs[f'spraychar{index:02d}.bin']=recipe([row],{'toon':1,'output-lighting':1})
            options={'toon':1,'motions':176,'city-palette':1}
            if index in (6,7,10):options['face-spray']=1
            if index in (6,10):row+=':shadow='+('35' if index==6 else '45')
            jobs[f'spraychar{index:02d}.jgc']=recipe([row],options)
            jobs[f'spraymesh{index:02d}.jgc']=recipe([row],dict(options,**{'mesh-only':1}))
            motions=[u(data,u(data,head+0x68)+4*i) for i in range(3)]
            row=(f'{name}:{dat}:{tex}:{placement}:rawat:0:{u(data,head+0x54):x}:'+
                 ','.join(f'{m:x}' for m in motions)+f':texbinding=3,0,{u(texture,4):x},{u(texture,thead+0x54):x},0,0')
            options={'toon':1,'motions':3}
            if index in (0,7,10,15):options['face-expression']=1
            elif index==6:options['face-surface']=1
            if index in (6,10):row+=':shadow='+('35' if index==6 else '45')
            jobs[f'recruitmouth{index:02d}.jgc']=recipe([row],options)
        data=(dec/'afs/STAGE6/000_st6dat.bin').read_bytes();tex='afs/STAGE6/001_st6txr.bin'
        rows=[]
        for lod,(rt,tt) in enumerate(((0x8c20b37c,0x8c20b40c),(0x8c20b49c,0x8c20b52c))):
            root,tl=eu(rt+40),eu(tt+40)
            rows.append(f'lift{lod}:afs/STAGE6/000_st6dat.bin:{tex}:0,0,0,0:rawat:8cb00000:{root:x}:8cc7e208:'
                        f'texbinding=1,8c010000,8c219698,{tl:x},8c800000,0:lighting=1:cull=none')
        jobs['grind_lifts.jgc']=recipe(rows,{'motions':1,'texmax':512,'source-material':1,'source-environment':1})
        for output,dat,tex,roots,motions,names,options in (
            ('title.jgc','afs/U_TITLE/000_titledat.bin','afs/U_TITLE/001_titletxr.bin',
             [4+4*i for i in range(4)],[20+4*i for i in range(4)],[f'T{i}' for i in range(4)],
             {'legacy':1,'original-culling':1,'motions':1,'nolight':1,'texwhite':1}),
            ('garage_mark.jgc','afs/U_GARAGE/001_garagedat.bin','afs/U_GARAGE/002_garagetxr.bin',
             [0x140,0x144,0x148,0x14c,0x150,0x158,0x15c,0x160,0x164],
             [s+0x30 for s in (0x140,0x144,0x148,0x14c,0x150,0x158,0x15c,0x160,0x164)],
             [f'M{i}' for i in range(9)], {'legacy':1,'original-culling':1,'motions':1,'nolight':1}),
            ('radio.jgc','afs/U_GARAGE/001_garagedat.bin','afs/U_GARAGE/002_garagetxr.bin',
             [0x340,0x344],[0,0x348],['RadioBase','RadioAnim'],{'motions':1,'source-material':1,'nolight':1,'texmax':512}),
        ):
            data=(dec/dat).read_bytes();base=0x8cf00000 if output=='title.jgc' else 0x8c800000
            rows=[f'{name}:{dat}:{tex}:0,0,0,0:rawat:{base:x}:{u(data,r):x}:{u(data,m) if m else 0:x}' for name,r,m in zip(names,roots,motions)]
            if output=='radio.jgc':
                rows=[row+f':texbinding=1,8c010000,{array:x},1,8c800000,3'
                      for row,array in zip(rows,(0x8c10e5ac,0x8c10e5c4))]
            jobs[output]=recipe(rows,options)
        dat='afs/PLAYER/016_player_effect_dat.bin';tex='afs/PLAYER/017_player_effect_txr.bin'
        rows=[f'{name}:{dat}:{tex}:0,0,0,0:rawat:8c8e1000:{root:x}:{motion:x}' for name,root,motion in
              [('Can1Far',0x8c8e1aec,0x8c8eadd0),('Can1Near',0x8c8e3288,0x8c8eaf60),
               ('Can5Far',0x8c8e283c,0x8c8eae98),('Can5Near',0x8c8e2d68,0x8c8eb028)]]
        jobs['cans.jgc']=recipe(rows,{'motions':1,'texmax':256})
        jobs['cordon.jgc']=recipe([f'Cordon:{dat}:{tex}:0,0,0,0:rawat:8c8e1000:8c8ea7d4:8c8eee70'],{'legacy':1,'motions':1})
        gas_data='afs/ENEMY/003_kidoutaidat.bin';gas_tex='afs/ENEMY/002_kidoutaitxr.bin';rows=[]
        for stage2 in range(2):
            for variant in range(2):
                for part in range(2):
                    root=eu(0x8c1b5c34+stage2*16+variant*4+part*8);motion=eu(0x8c1b5c54+part*4)
                    rows.append(f'gas{stage2}{variant}{part}:{gas_data}:{gas_tex}:0,0,0,0:rawat:8ca00000:{root:x}:{motion:x}:alpha=1')
        for variant in range(2):
            root=eu(0x8c1b5c60+variant*4);motion=eu(0x8c1b5c58)
            rows.append(f'gasbody{variant}:{gas_data}:{gas_tex}:0,0,0,0:rawat:8ca00000:{root:x}:{motion:x}:collision-tree=1')
        jobs['enemygas.jgc']=recipe(rows,{'nolight':1,'source-material':1,'source-environment':1})
        effects=module('event_assets')
        effects_data='afs/ENEMY/045_effectdat.bin';effects_txr='afs/ENEMY/044_effecttxr.bin'
        ptr=0x8c1b9590
        while eu(ptr)!=0x8c1b94a8:
            assert eu(ptr)
            ptr+=8
        pointers=eu(ptr+4)
        rows=[]
        for root in (eu(0x8c1b6710),eu(0x8c1b6714)):
            for frame in range(8):
                rows.append(f'Flame{len(rows)}:{effects_data}:{effects_txr}:0,0,0,0:rawat:8cff0000:{root:x}:0:'
                            f'texbinding=1,8c010000,{pointers+frame*4:x},1,8c800000,3')
        jobs['eventflare.jgc']=recipe(rows,{'motions':0,'source-material':1})
        binding=effects.typed_binding(4)
        rows=[]
        for offset in (12,24,36,48):
            root=eu(eu(0x8c1b5744+offset));motion=eu(eu(0x8c1b5744+offset+8))
            rows.append(f"Burst{offset}:{binding['source']}:{binding['textures']}:0,0,0,0:rawat:{binding['source_base']:x}:{root:x}:{motion:x}")
        jobs['eventblast.jgc']=recipe(rows,{'motions':1,'source-material':1})
        for name, output, function in (
            ('burning_assets','burning.jgc','export'),
            ('enemy_projectile_assets','enemyprojectiles.jgc','export'),
            ('final_actor_assets','finalactors.jgc','export'),
            ('final_hazard_assets','finalhazard.jgc','export'),
            ('scene_effect_assets','scenefx.jgc','export'),
            ('heli_wreck_assets','heliwreck.jgc','export'),
            ('final_stage_assets','finalscene.jgc','export'),
            ('final_intro_assets','scene0092.jgc','export'),
            ('final_scene_assets','scene6198.jgc','export'),
            ('story_intro_assets','scene0100.jgc','export'),
            ('scene_assets','scene5056.jgc','actor_bank'),
            ('police_vehicle_assets','policevehicles.jgc','build'),
            ('spray_hud_assets','sprayhud.jgc','export'),
            ('player_effects','skatefx.jgc','bake'),
            ('staff_sky_assets','staffsky.jgc','export'),
            ('final_emitter_assets','finalemitter.jgc','export'),
        ):
            jobs[output]=capture_first(dec/'tools'/f'{name}.py',function=function,normalize=True)
        for name,output in (('bantam_train_assets','bantam_train.jgc'),
                            ('benten_train_assets','benten_train.jgc'),
                            ('benten_crane_assets','benten_crane.jgc'),
                            ('kogane_house_assets','kogane_house.jgc'),
                            ('kogane_cover_assets','kogane_cover.jgc'),
                            ('shibuya_gate_assets','shibuya_gates.jgc'),
                            ('shibuya_door_assets','shibuya_doors.jgc')):
            jobs[output]=capture_first(dec/'tools'/f'{name}.py',directory=True,function='generate',normalize=True)
            if output=='bantam_train.jgc':jobs[output]=recipe(jobs[output]['specs'],dict(jobs[output]['options'],**{'body-before-decal':1}))
        for stage in (1,2,3):
            jobs[f'traffic{stage}.jgc']=capture_first(dec/'tools/path_assets.py',directory=True,function='export',normalize=True,before=(stage,))
        for stage,name in ((3,'benten'),(5,'bantam')):
            jobs[f'{name}_openings.jgc']=capture_first(dec/'tools/city_opening_assets.py',directory=True,function='generate',normalize=True,after=(stage,))
        jobs['traffic5.jgc']=capture_first(dec/'tools/path_assets.py',directory=True,function='export',normalize=True,before=(5,))
        jobs['traffic6.jgc']=capture_first(dec/'tools/path_assets.py',directory=True,function='export',normalize=True,before=(6,))
        jobs['watersplash.jgc']=capture_first(dec/'tools/kogane_water_assets.py',directory=True,function='products')
        jobs['garage_rival.jgc']=capture_first(dec/'tools/garage_rival_assets.py',no_output=True)
        jobs['scene3025.jgc']=capture_first(dec/'tools/final_scene_assets.py',function='export',normalize=True,after=(90,))
        jobs['paintspray.jgc']=capture_first(dec/'tools/player_effects.py',after=(True,))
        for stage in (2,3,5,6):
            jobs[f'sky{stage}.jgc']=capture_first(dec/'tools/city_sky_assets.py',function='export',normalize=True,before=(stage,))
        return jobs
    finally:
        sys.path.pop(0)


def source_texture_binding(row, dec):
    """Replace a private decoded texmap with original pointer-array instructions.

    The private map is used only to prove a matching original extent. No map
    values are serialized in the public recipe. A constant mapping is the
    already supported single-texture selection operation.
    """
    parts=row.split(':')
    stage_mapping=None
    for index in (1,2):
        path=Path(parts[index])
        if path.is_absolute():
            try:parts[index]=path.relative_to(dec).as_posix()
            except ValueError:
                if index!=2 or '/STAGE' not in parts[1]:raise
                stage=int(parts[1].split('/STAGE',1)[1].split('/',1)[0])
                mapping=json.loads((dec/f'stage{stage}_texmap.json').read_text())
                if path.read_bytes()!=b''.join((dec/p).read_bytes() for p in mapping['packs']):
                    raise ValueError('Temporary texture input differs from original stage packs')
                if len(mapping['packs'])>1:stage_mapping=(stage,mapping)
                parts[index]=mapping['packs'][0]
    mapping=next((p for p in parts[4:] if p.startswith('texmap=')),None)
    if mapping is None:return ':'.join(parts)
    values=[int(n) for n in mapping.split('=',1)[1].split(',')]
    parts.remove(mapping)
    if stage_mapping:
        stage,metadata=stage_mapping
        matches=[int(tl) for tl,actual in metadata['texlists'].items() if actual==values]
        if not matches and set(values)=={0}:matches=[0]  # Empty original far hierarchy fallback.
        if not matches:raise ValueError('No original stage texlist for '+parts[0])
        return ':'.join(parts)+f':texbinding=1,8c010000,{stage:x},{min(matches):x},0,4'
    if len(set(values))==1:
        return ':'.join(parts)+':tex='+str(values[0])
    texture=(dec/parts[2]).read_bytes()
    offsets=[];at=0
    while True:
        at=texture.find(b'GBIX',at)
        if at<0:break
        offsets.append(at);at+=8
    target=[offsets[v] for v in values]
    base=int(parts[5],16)
    images=[(1,0x8c010000,(dec/'extracted/1ST_READ.BIN').read_bytes()),
            (2,base,(dec/parts[1]).read_bytes()),(3,0,texture)]
    for rotation in range(len(target)):
        original=target[-rotation:]+target[:-rotation] if rotation else target
        for texture_base in dict.fromkeys((base,0,0x8c800000,0x8ca00000,0x8cb00000,0x8cda0000,0x8cf00000)):
            pattern=struct.pack('<'+'I'*len(original),*[n+texture_base for n in original])
            for kind,imagebase,data in images:
                at=data.find(pattern)
                while at>=0 and at%4:at=data.find(pattern,at+1)
                if at>=0:
                    count=len(target)|(rotation<<16)
                    return ':'.join(parts)+f':texbinding={kind},{imagebase:x},{imagebase+at:x},{count:x},{texture_base:x},3'
    raise ValueError('No original texture-pointer extent for '+parts[0])


def shared_specs(tree):
    """Resolve generic raw root discovery into bounded original source extents."""
    dec=Path(tree)/'decompile';rows=[]
    for row in (dec/'chars_spec.txt').read_text().splitlines():
        parts=row.split(':');data=(dec/parts[1]).read_bytes()
        if parts[4]=='raw':
            base=int(parts[5],16);index=int(parts[6]);n=len(data)
            u=lambda at:struct.unpack_from('<I',data,at)[0]
            isp=lambda v,align=4:base<=v<base+n and v%align==0
            objects={}
            for at in range(0,n-52,4):
                if u(at)>65535:continue
                model,child,sibling=u(at+4),u(at+44),u(at+48)
                if not all(v==0 or isp(v) for v in (model,child,sibling)) or not (model or child or sibling):continue
                if not all(1e-4<v<1e3 for v in struct.unpack_from('<3f',data,at+32)):continue
                objects[base+at]=(child,sibling)
            children={p for pair in objects.values() for p in pair}
            def size(root):
                stack=[root];count=0;seen=set()
                while stack:
                    node=stack.pop()
                    if not node or node not in objects:continue
                    if node in seen:raise ValueError('Cyclic raw tree')
                    seen.add(node);count+=1;stack.extend(objects[node])
                return count
            roots=sorted(a for a in objects if a not in children);roots.sort(key=lambda a:-size(a))
            root=roots[index];bones=size(root);motions=[]
            for at in range(0,n//4*4-4,4):
                kind,md=u(at),u(at+4)
                if not(0x10000<=kind<0x1000000 and 0<kind&65535<512 and isp(md)):continue
                good=True
                for bone in range(bones):
                    offset=md-base+bone*16
                    if offset+16>n:good=False;break
                    count,pos,ang,scale=struct.unpack_from('<4I',data,offset)
                    if count>=512 or not(pos==0 or isp(pos)) or not(ang==0 or isp(ang,2)) or not(scale==0 or isp(scale)):good=False;break
                if good:motions.append(base+at)
            parts=parts[:4]+['rawat',f'{base:x}',f'{root:x}',','.join(f'{m:x}' for m in motions) or '0']+parts[7:]
        if data[:4]==b'PLAY':
            parts+=['palette=1']
            if parts[1].endswith('012_gumdat.bin'):parts+=['shadow=35']
            if parts[1].endswith('022_recodat.bin'):parts+=['shadow=45']
        if any(parts[1].endswith(n) for n in ('017_firedat.bin','025_heridat.bin','029_bheridat.bin','037_denkidat.bin','021_patcardat.bin')):parts+=['texcap=128']
        rows.append(source_texture_binding(':'.join(parts),dec.resolve()))
    sys.path.insert(0,str(dec/'tools'))
    try:
        spec=importlib.util.spec_from_file_location('_portable_lod_specs',dec/'tools/enemy_lod_assets.py')
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        exe=(dec/'extracted/1ST_READ.BIN').read_bytes()
        original=(dec/'chars_spec.txt').read_text().splitlines()
        for ci,near,far,_ in mod.PAIRS:
            if ci in mod.EXISTING_FAR:continue
            fields=original[ci].split(':');root=struct.unpack_from('<I',exe,far-0x8c010000)[0]
            extra=[p for p in fields[4:] if p.startswith(('tex=','texmap=','texbinding=','material=','cull='))]
            if ci in mod.FAR_TEXLIST:
                tex=(dec/fields[2]).read_bytes();offsets=[];at=0
                while True:
                    at=tex.find(b'GBIX',at)
                    if at<0:break
                    offsets.append(at);at+=8
                ew=lambda a:struct.unpack_from('<I',exe,a-0x8c010000)[0]
                slot=mod.FAR_TEXLIST[ci];slot=slot[0] if isinstance(slot,tuple) else slot
                tl=ew(slot);mapping=[offsets.index(ew(tl+8+i*4)-0x8ca00000) for i in range(ew(tl+4))]
                extra=[p for p in extra if not p.startswith(('tex=','texmap=','texbinding='))]
                extra+=['texmap='+','.join(map(str,mapping))]
            if ci in (33,50):extra+=['texcap=128']
            row=':'.join([fields[0]+'Low',*fields[1:4],'rawat','8ca00000',f'{root:x}','0',*extra])
            rows.append(source_texture_binding(row,dec.resolve()))
    finally:sys.path.pop(0)
    return rows
