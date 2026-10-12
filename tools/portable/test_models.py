"""Host validation for setup's bounded native model/mesh conversion primitives."""
from __future__ import annotations
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WRAPPER = r'''
#include "portable_models.h"
#include "portable_convert.h"
#include "portable_indexed.h"
#include <stdlib.h>
#include <string.h>
typedef struct {const unsigned char *data;unsigned length;} Input;
int jgr_disc_read(JgrDisc *d,const JgrDiscFile *f,uint32_t at,void *out,size_t n){
 Input *input=(Input*)d;if(at>f->size || n>f->size-at || f->offset+at>input->length || n>input->length-f->offset-at)return -1;
 memcpy(out,input->data+f->offset+at,n);return 0;}
int jgr_disc_find(JgrDisc *d,const char *name,JgrDiscFile *file){Input *input=(Input*)d;if(strcmp(name,"1ST_READ.BIN"))return -1;file->offset=0;file->size=input->length;return 0;}
typedef struct {JgrDisc *d;JgrDiscFile file;uint32_t base;JgrModelCache *cache;FILE *out;unsigned ordinal,legacy,environment,mode;} State;
static int word(FILE *out,uint32_t value){unsigned char p[4];for(unsigned i=0;i<4;i++)p[i]=value>>(i*8);return fwrite(p,1,4,out)==4?0:-1;}
static int emit(const JgrModelBatch *b,void *opaque){State *s=opaque;
 if(word(s->out,b->texture)||word(s->out,b->flags)||word(s->out,b->vertices)||word(s->out,b->indices)||word(s->out,b->stride))return -1;
 if(fwrite(b->vertex_bytes,b->stride,b->vertices,s->out)!=b->vertices || fwrite(b->index_bytes,2,b->indices,s->out)!=b->indices)return -1;
 return (s->mode&32) && fwrite(b->positions,8,b->vertices*3,s->out)!=b->vertices*3?-1:0;}
static int visit(const JgrModelNode *n,const double world[16],unsigned ordinal,int parent,void *opaque){State *s=opaque;JgrModelOptions o;jgr_model_options_default(&o);s->ordinal=ordinal;
 if(word(s->out,n->address)||word(s->out,ordinal)||word(s->out,(uint32_t)parent))return -1;
 long count_at=ftell(s->out);if(word(s->out,0))return -1;long before=ftell(s->out);
 o.texture_count=8192;o.owner=s->legacy?-1:(int)ordinal;o.cache_vertices=1;o.source_material=s->environment;o.source_environment=s->environment;
 if(s->mode&4){o.owner=-1;o.cache_vertices=0;memcpy(o.matrix,world,sizeof o.matrix);}
 if(s->mode&8)o.max_edge=0.3;
 JgrModelLight light={{0.2,0.9,0.2},0.7,0.3,{1,1,1},{0.2,0.2,0.2},{{210,210,210},{80,80,150}}};
 if(s->mode&16)o.baked_light=&light;
 memcpy(o.bind,world,sizeof o.bind);if(jgr_model_inverse(o.inverse_bind,world))return -1;
 if(n->model && jgr_model_mesh(s->cache,s->d,&s->file,s->base,n->model,&o,emit,s))return -1;
 long end=ftell(s->out);if(end<before || fseek(s->out,count_at,SEEK_SET) || word(s->out,(uint32_t)(end-before)) || fseek(s->out,end,SEEK_SET))return -1;return 0;}
int model(const unsigned char *data,unsigned length,unsigned base,unsigned root,int mode,const char *path){
 Input in={data,length};State s;memset(&s,0,sizeof s);s.d=(JgrDisc*)&in;s.file.size=length;s.base=base;s.legacy=mode&1;s.environment=!!(mode&2);s.mode=mode;s.cache=jgr_model_cache_create();s.out=fopen(path,"wb");
 if(!s.cache || !s.out){if(s.out)fclose(s.out);jgr_model_cache_free(s.cache);return -1;}
 double identity[16];jgr_model_identity(identity);int result=jgr_model_walk(s.d,&s.file,base,root,identity,s.legacy,1,visit,&s);
 if(fclose(s.out))result=-1;jgr_model_cache_free(s.cache);return result;}
int compact(const char *input,const char *output,int mesh_only){FILE *a=fopen(input,"rb"),*b=fopen(output,"wb");if(!a||!b){if(a)fclose(a);if(b)fclose(b);return -1;}int r=jgr_model_compact(a,b,mesh_only);fclose(a);fclose(b);return r;}
static unsigned getword(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static int texture(unsigned index,FILE *pixels,JgrJgcTexture *t,void *opaque){FILE *input=opaque;unsigned char rec[20],block[4096];
 if(fseek(input,4+index*20,SEEK_SET)||fread(rec,1,20,input)!=20)return -1;
 t->width=getword(rec);t->height=getword(rec+4);t->flags=getword(rec+8);t->clut=ftell(pixels);t->pixels=t->clut+1024;
 unsigned offsets[]={getword(rec+12),getword(rec+16)},sizes[]={1024,t->width*t->height};
 for(unsigned part=0;part<2;part++){if(fseek(input,offsets[part],SEEK_SET))return -1;unsigned left=sizes[part];while(left){unsigned n=left<sizeof block?left:sizeof block;if(fread(block,1,n,input)!=n||fwrite(block,1,n,pixels)!=n)return -1;left-=n;}}return 0;}
static int native_texture(unsigned index,FILE *pixels,JgrJgcTexture *t,void *opaque){Input *input=opaque;unsigned at=0,count=0;
 for(;at+8<input->length;at++){if(memcmp(input->data+at,"GBIX",4))continue;if(count++==index)break;at+=7;}
 if(at+8>=input->length)return -1;unsigned pvr=at+8;for(;pvr+16<=input->length&&pvr<at+64;pvr++)if(!memcmp(input->data+pvr,"PVRT",4))break;
 if(pvr+16>input->length||pvr>=at+64)return -1;const unsigned char *h=input->data+pvr;unsigned size=getword(h+4),sw=h[12]|(unsigned)h[13]<<8,sh=h[14]|(unsigned)h[15]<<8;
 if(size<8)return -1;size-=8;if(size>input->length-pvr-16)size=input->length-pvr-16;
 unsigned char *rgba=jgr_pvr_decode_payload(h+16,size,h[8],h[9],sw,sh);if(!rgba)return -1;unsigned w=sw,height=sh;while(w>128||height>128){w/=2;height/=2;}if(w<16)w=16;if(height<8)height=8;
 int result=jgr_indexed_texture(rgba,sw,sh,w,height,pixels,t);free(rgba);return result;}
int play_bank(const unsigned char *data,unsigned length,unsigned dat_length,unsigned lod,unsigned motions,const char *texture_path,const char *output){
 Input input={data,length};JgrDiscFile dat={0,dat_length},tex={dat_length,length-dat_length};FILE *sections[10]={0},*out=NULL,*pixels=NULL;JgrJgcBuild *build=NULL;int result=-1;
 for(unsigned i=0;i<10;i++)if(!(sections[i]=tmpfile()))goto done;
 build=jgr_jgc_create((JgrDisc*)&input,sections,0);out=fopen(output,"wb");if(texture_path)pixels=fopen(texture_path,"rb");if(!build||!out||(texture_path&&!pixels))goto done;
 JgrJgcCharacter c;uint16_t map[512];memset(&c,0,sizeof c);jgr_model_options_default(&c.model);strcpy(c.name,"Test");c.motion_count=motions;
 Input texture_input={data+dat_length,length-dat_length};
 if(jgr_jgc_resolve_play((JgrDisc*)&input,&dat,&tex,lod,0,map,&c)||jgr_jgc_add(build,&c)||jgr_jgc_finish(build,out,texture_path?texture:native_texture,texture_path?(void*)pixels:(void*)&texture_input))goto done;result=0;
done:jgr_jgc_free(build);for(unsigned i=0;i<10;i++)if(sections[i])fclose(sections[i]);if(out)fclose(out);if(pixels)fclose(pixels);return result;}
'''


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Models(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc')
        if not compiler:
            raise unittest.SkipTest('Host C compiler unavailable')
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-model-native-')
        cls.work = Path(cls.temp.name)
        wrapper = cls.work / 'wrapper.c'
        wrapper.write_text(WRAPPER)
        lib = cls.work / 'models.so'
        subprocess.run([compiler, '-std=c99', '-O2', '-ffp-contract=off', '-Wall', '-Wextra',
                        '-shared', '-fPIC', '-I', str(ROOT / 'psp/src'), str(wrapper),
                        str(ROOT / 'psp/src/portable_models.c'), str(ROOT / 'psp/src/portable_convert.c'),
                        str(ROOT / 'psp/src/portable_indexed.c'), '-lm', '-o', str(lib)], check=True)
        cls.native = ctypes.CDLL(str(lib))
        cls.native.model.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint,
                                    ctypes.c_uint, ctypes.c_int, ctypes.c_char_p]
        cls.native.compact.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
        cls.native.play_bank.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint,
                                        ctypes.c_uint, ctypes.c_uint, ctypes.c_char_p, ctypes.c_char_p]
        cls.nl = load_module('model_reference_ninja', ROOT / 'decompile/tools/ninjalib.py')
        cls.legacy = load_module('model_reference_legacy', ROOT / 'tools/portable/legacy_ninjalib.py')
        cls.compactor = load_module('model_reference_compact', ROOT / 'decompile/tools/jgc_compact.py')

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_mesh(self, data, base, root, mode=0):
        out = self.work / 'mesh.bin'
        buffer = ctypes.create_string_buffer(data)
        rc = self.native.model(buffer, len(data), base, root, mode, os.fsencode(out))
        return rc, out.read_bytes()

    def reference(self, data, base, root, mode):
        nl = self.legacy if mode & 1 else self.nl
        bld = nl.JgsBuilder()
        nl.MAXEDGE = 0.3 if mode & 8 else 0
        cache = {}
        identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        stack = [(root, -1, identity)]
        ordinal = 0
        result = bytearray()
        positions = []
        original_struct = nl.struct
        class RecordingStruct:
            def __getattr__(self, key):
                return getattr(original_struct, key)
            def pack(self, fmt, *values):
                if fmt in ('<ffIfff', '<ffIfffHH'):
                    positions.append(values[3:6])
                return original_struct.pack(fmt, *values)
        nl.struct = RecordingStruct()
        while stack:
            address, parent, pm = stack.pop()
            if not address:
                continue
            n = nl.read_object(data, address, base)
            world = nl.mmul(pm, nl.node_matrix(n['ev'], n['pos'], n['ang'], n['scl']))
            first = len(bld.batches)
            kw = {} if mode & 1 else dict(owner=None if mode & 4 else ordinal, source_material=bool(mode & 2), source_environment=bool(mode & 2))
            if n['model']:
                bld.add_model(data, n['model'], base, world if mode & 4 else identity, lambda slot: min(slot, 8191),
                              cache=None if mode & 4 else cache, bindm=world, bindm_inv=nl.minv(world), **kw)
            blob = bytearray()
            stride = 24 if mode & 5 else 40 if mode & 2 else 28
            for vo, io, nv, ni, tex, flags in bld.batches[first:]:
                blob += struct.pack('<5I', tex, flags, nv, ni, stride)
                blob += bld.verts[vo*stride:(vo+nv)*stride]
                blob += bld.idx[io*2:(io+ni)*2]
                if mode & 32:
                    blob += b''.join(struct.pack('<3d', *point) for point in positions[vo:vo+nv])
            result += struct.pack('<4I', address, ordinal, parent & 0xffffffff, len(blob)) + blob
            if n['sib']:
                stack.append((n['sib'], parent, pm))
            if n['child']:
                stack.append((n['child'], ordinal, world))
            ordinal += 1
        nl.MAXEDGE = 0
        nl.struct = original_struct
        return bytes(result)

    @staticmethod
    def fixture():
        # This small authored triangle exercises indexed strip, UV and normal
        # decoding without any game data in the public test.
        data = bytearray(512)
        struct.pack_into('<II3f3i3fII', data, 32, 0, 96, 2, 3, 4, 1900, -900, 500, 1, 1, 1, 0, 0)
        struct.pack_into('<II4f', data, 96, 128, 256, 0, 0, 0, 1)
        struct.pack_into('<4H', data, 128, 0x29, 19, 2, 3)
        for i, pos in enumerate(((0, 0, 0), (1, 0, 0), (0, 1, 0))):
            struct.pack_into('<6f', data, 136+24*i, *pos, 0, 1, 0)
        data[208] = 255
        struct.pack_into('<2H', data, 256, 8, 3)
        struct.pack_into('<4H', data, 260, 65, 12, 1, 3)
        for i in range(3):
            struct.pack_into('<Hhh', data, 268+i*6, i+2, i*127, (2-i)*100)
        data[288] = 255
        return bytes(data)

    def test_synthetic_mesh_all_formats(self):
        for mode in (0, 1, 2):
            with self.subTest(mode=mode):
                data = self.fixture()
                rc, result = self.run_mesh(data, 0, 32, mode)
                self.assertEqual(rc, 0)
                self.assertEqual(result, self.reference(data, 0, 32, mode))

    def test_signed_uv_extremes_match_reference(self):
        for chunk in (65, 66):
            data = bytearray(self.fixture())
            struct.pack_into('<H', data, 260, chunk)
            for i, (u, v) in enumerate(((-32768, 32767), (127, -128), (-1, 1))):
                struct.pack_into('<hh', data, 270+i*6, u, v)
            with self.subTest(chunk=chunk):
                rc, output = self.run_mesh(bytes(data), 0, 32)
                self.assertEqual(rc, 0)
                self.assertEqual(output, self.reference(bytes(data), 0, 32, 0))

    def test_static_subdivision_and_baked_lighting(self):
        sys.path.insert(0, str(ROOT/'decompile/tools'))
        import baked_light
        original = baked_light.PROFILES
        baked_light.PROFILES = {'fixture': dict(direction=(0.2, 0.9, 0.2), diffuse=0.7, ambient=0.3,
                                               rgb=(1, 1, 1), cel_direction=(0.2, 0.2, 0.2),
                                               cel_colors=((210, 210, 210), (80, 80, 150)))}
        try:
            for mode in (4, 12, 20, 28, 44, 60):
                with self.subTest(mode=mode):
                    self.nl.BAKED_LIGHT = 'fixture' if mode & 16 else None
                    data = self.fixture()
                    rc, result = self.run_mesh(data, 0, 32, mode)
                    self.assertEqual(rc, 0)
                    self.assertEqual(result, self.reference(data, 0, 32, mode))
        finally:
            baked_light.PROFILES = original
            self.nl.BAKED_LIGHT = None
            self.nl.MAXEDGE = 0
            sys.path.pop(0)

    def test_invalid_chunk_and_cycle_fail(self):
        for field, value in ((128, 0x77), (44+32, 32)):
            with self.subTest(field=field):
                data = bytearray(self.fixture())
                struct.pack_into('<I', data, field, value)
                self.assertEqual(self.run_mesh(bytes(data), 0, 32)[0], -1)
        data = bytearray(self.fixture())
        struct.pack_into('<I', data, 96, len(data)-2)
        self.assertEqual(self.run_mesh(bytes(data), 0, 32)[0], -1)

    def test_compaction_and_mesh_only(self):
        # Two motions use two identical channel tables referencing one own
        # synthetic key stream. A single texture makes pointer rebasing visible.
        h = [1, 1, 0, 0, 0, 1, 2, 80, 128, 176, 176, 176, 176, 192, 272, 288]
        data = bytearray(288+1024+128)
        data[:4] = b'JGC2'
        struct.pack_into('<16I', data, 4, *h)
        struct.pack_into('<4I', data, 80, 0, 1, 0, 2)
        struct.pack_into('<4H2I', data, 176, 16, 8, 0, 0, 288, 1312)
        struct.pack_into('<4I', data, 192, 1, 208, 1, 232)
        for offset in (208, 232):
            struct.pack_into('<6I', data, offset, 1, 0, 0, 0, 0, 0)
        struct.pack_into('<3f', data, 272, 1, 2, 3)
        source, out = self.work / 'bank.jgc', self.work / 'compact.jgc'
        source.write_bytes(data)
        for mesh_only in (0, 1):
            self.assertEqual(self.native.compact(os.fsencode(source), os.fsencode(out), mesh_only), 0)
            expected = (self.compactor.mesh_only if mesh_only else self.compactor.compact)(data)
            self.assertEqual(out.read_bytes(), expected)
        struct.pack_into('<I', data, 208, 0xffffffff)
        source.write_bytes(data)
        self.assertEqual(self.native.compact(os.fsencode(source), os.fsencode(out), 0), -1)

    def compare_play_container(self, data, texture_data, motions=12, lod=0):
        from PIL import Image
        sys.path.insert(0, str(ROOT / 'decompile/tools'))
        try:
            from pvr_decode import decode
        finally:
            sys.path.pop(0)
        records, payload = [], bytearray()
        offset = 0
        while True:
            gbix = texture_data.find(b'GBIX', offset)
            if gbix < 0:
                break
            offset = gbix+8
            pvr = texture_data.find(b'PVRT', gbix+8, gbix+64)
            length = struct.unpack_from('<I', texture_data, pvr+4)[0]
            im = decode(texture_data[pvr:pvr+8+length])
            w, h = im.size
            while max(w, h) > 128:
                w //= 2
                h //= 2
            w, h = max(w, 16), max(h, 8)
            im = im.resize((w, h), Image.Resampling.LANCZOS)
            q = im.quantize(256, method=Image.Quantize.FASTOCTREE)
            palette = bytes(q.getpalette('RGBA'))
            palette = (palette+bytes(1024))[:1024]
            clut = len(payload)
            payload += palette
            pixels = len(payload)
            payload += self.nl.swizzle8(q.tobytes(), w, h)
            records.append((w, h, int(im.getextrema()[3][0] < 255), clut, pixels))
        prefix = 4+len(records)*20
        palette_path = self.work/'indexed.bin'
        palette_path.write_bytes(struct.pack('<I', len(records))+b''.join(
            struct.pack('<5I', w, h, alpha, prefix+clut, prefix+pixels)
            for w, h, alpha, clut, pixels in records)+payload)
        dat, txr, native, expected = [self.work/name for name in ('play.bin', 'ptex.bin', 'native.jgc', 'expected.jgc')]
        dat.write_bytes(data)
        txr.write_bytes(texture_data)
        subprocess.run([sys.executable, str(ROOT/'decompile/tools/jgc_convert.py'), str(expected),
                        f'--motions={motions}', f'--lod={lod}', f'Test:{dat}:{txr}:0,0,0,0'],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        buffer = ctypes.create_string_buffer(data+texture_data)
        rc = self.native.play_bank(buffer, len(data)+len(texture_data), len(data), lod,
                                   motions, os.fsencode(palette_path), os.fsencode(native))
        self.assertEqual(rc, 0)
        self.assertEqual(native.read_bytes(), expected.read_bytes())
        rc = self.native.play_bank(buffer, len(data)+len(texture_data), len(data), lod,
                                   motions, None, os.fsencode(native))
        self.assertEqual(rc, 0)
        self.assertEqual(native.read_bytes(), expected.read_bytes())
        return len(native.read_bytes()), hashlib.sha256(native.read_bytes()).hexdigest()

    def test_complete_synthetic_play_container(self):
        data = bytearray(self.fixture())+bytearray(512)
        data[:8] = struct.pack('<4sI', b'PLAY', 768)
        struct.pack_into('<6I', data, 768, 1, 32, 3, 800, 0, 0)
        struct.pack_into('<3I', data, 800, 824, 824, 0)
        struct.pack_into('<2I', data, 824, 2 << 16 | 1, 848)
        struct.pack_into('<4I', data, 848, 2, 920, 0, 0)
        struct.pack_into('<6f', data, 920, 0, 1, 2, 3, 4, 5)
        pixels = struct.pack('<128H', *(i*31 for i in range(128)))
        texture = struct.pack('<4sIII4sIBBHHH', b'GBIX', 8, 1, 0, b'PVRT', 8+len(pixels), 1, 9, 0, 16, 8)+pixels
        self.compare_play_container(bytes(data), texture, 2)

    @unittest.skipUnless(os.environ.get('JGR_TEST_SOURCE_TREE'), 'Private source tree not supplied')
    def test_private_player_mesh_parity(self):
        tree = Path(os.environ['JGR_TEST_SOURCE_TREE'])
        rows = []
        for source in sorted((tree / 'decompile/afs/PLAYER').glob('*dat.bin')):
            data = source.read_bytes()
            if data[:4] != b'PLAY':
                continue
            table = struct.unpack_from('<I', data, 4)[0]
            roots = []
            while True:
                kind, address = struct.unpack_from('<2I', data, table)
                table += 8
                if not kind:
                    break
                if kind == 1:
                    roots.append(address)
            for lod, root in enumerate(roots):
                for mode in (0, 1, 2):
                    with self.subTest(source=source.name, lod=lod, mode=mode):
                        rc, result = self.run_mesh(data, 0, root, mode)
                        self.assertEqual(rc, 0)
                        expected = self.reference(data, 0, root, mode)
                        self.assertEqual(result, expected)
                        rows.append(dict(source=source.relative_to(tree).as_posix(), lod=lod, mode=mode,
                                         bytes=len(result), sha256=hashlib.sha256(result).hexdigest()))
        self.assertTrue(rows)
        report = os.environ.get('JGR_MODEL_REPORT')
        if report:
            output = Path(report)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps({'kind': 'intermediate-mesh-parity', 'cases': rows,
                                          'final_asset_count': 0}, indent=2)+'\n')

    @unittest.skipUnless(os.environ.get('JGR_TEST_ASSETS'), 'Private generated banks not supplied')
    def test_private_compact_banks(self):
        assets = Path(os.environ['JGR_TEST_ASSETS'])
        paths = list(assets.glob('*_compact.jgc')) + list(assets.glob('spraychar*.jgc'))
        self.assertTrue(paths)
        for path in paths:
            source = assets / path.name.replace('_compact', '') if '_compact' in path.name else path
            for mesh_only in (0, 1):
                with self.subTest(source=source.name, mesh_only=mesh_only):
                    out = self.work / 'native.jgc'
                    self.assertEqual(self.native.compact(os.fsencode(source), os.fsencode(out), mesh_only), 0)
                    expected = (self.compactor.mesh_only if mesh_only else self.compactor.compact)(source.read_bytes())
                    self.assertEqual(out.read_bytes(), expected)
                    if not mesh_only and '_compact' in path.name:
                        self.assertEqual(out.read_bytes(), path.read_bytes())

    @unittest.skipUnless(os.environ.get('JGR_TEST_SOURCE_TREE'), 'Private source tree not supplied')
    def test_private_play_containers(self):
        tree = Path(os.environ['JGR_TEST_SOURCE_TREE'])
        records = []
        for source in sorted((tree/'decompile/afs/PLAYER').glob('*dat.bin')):
            data = source.read_bytes()
            if data[:4] != b'PLAY':
                continue
            number = int(source.name.split('_', 1)[0])+1
            textures = list(source.parent.glob(f'{number:03d}_*txr.bin'))
            self.assertEqual(len(textures), 1)
            with self.subTest(source=source.name):
                size, sha = self.compare_play_container(data, textures[0].read_bytes(), 176)
                records.append(dict(source=source.relative_to(tree).as_posix(), bytes=size, sha256=sha))
        self.assertTrue(records)
        report = os.environ.get('JGR_MODEL_REPORT')
        if report:
            output = Path(report).with_name('containers.json')
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps({'kind': 'complete-native-base-PLAY-container-parity',
                                          'cases': records, 'final_catalog_asset_count': 0}, indent=2)+'\n')


if __name__ == '__main__':
    unittest.main()
