"""Payload-free collision and transform validation for setup model builders."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MODEL = r'''
#include <assert.h>
#include <float.h>
#include <math.h>
static unsigned sqrt_calls;
static double counted_sqrt(double x){sqrt_calls++;return sqrt(x);}
#define sqrt counted_sqrt
#include "portable_models.c"
#undef sqrt
typedef struct {unsigned char data[128];} Input;
int jgr_disc_read(JgrDisc *d,const JgrDiscFile *f,uint32_t at,void *out,size_t n){
 if(at>f->size||n>f->size-at||f->offset+at+n>128)return -1;memcpy(out,((Input*)d)->data+f->offset+at,n);return 0;
}
int jgr_disc_find(JgrDisc *d,const char *p,JgrDiscFile *f){(void)d;(void)p;(void)f;return -1;}
void key_test(void){
 Input in;memset(&in,0,sizeof in);memcpy(in.data,"ABCDEFuvwxyzABCDEFABCDEF",24);
 JgrDiscFile file={0,24};JgrJgcBuild b;memset(&b,0,sizeof b);b.disc=(JgrDisc*)&in;b.section[7]=tmpfile();b.key_hash=calloc(JGC_KEY_HASH,sizeof *b.key_hash);assert(b.section[7]&&b.key_hash);
 unsigned a,z,again,aligned,empty,empty_again;
 assert(!key_stream(&b,&file,0,6,2,&a)&&a==0);
 assert(!key_stream(&b,&file,6,6,2,&z)&&z==6);
 assert(!key_stream(&b,&file,12,6,2,&again)&&again==a);
 assert(!key_stream(&b,&file,18,6,4,&aligned)&&aligned==12);
 assert(!key_stream(&b,&file,24,0,2,&empty)&&empty==18);
 assert(!key_stream(&b,&file,24,0,2,&empty_again)&&empty==empty_again);
 assert(key_stream(&b,&file,23,2,2,&again));
 rewind(b.section[7]);unsigned char out[18];assert(fread(out,1,sizeof out,b.section[7])==sizeof out&&!memcmp(out,"ABCDEFuvwxyzABCDEF",18));
 fclose(b.section[7]);free(b.keys);free(b.key_hash);
}
void transform_test(void){
 Input in;memset(&in,0,sizeof in);in.data[0]=41;w16(in.data+2,7);w16(in.data+6,1);
 wf(in.data+8,2);wf(in.data+12,3);wf(in.data+16,4);wf(in.data+24,1);in.data[32]=255;
 JgrDiscFile file={0,36};JgrModelOptions o;jgr_model_options_default(&o);o.cache_vertices=1;o.bind[3]=5;
 JgrModelCache *cache=jgr_model_cache_create();Reader reader;memset(&reader,0,sizeof reader);reader.disc=(JgrDisc*)&in;reader.file=&file;
 o.owner=-1;assert(!parse_vertices(&reader,0,cache,&o));assert(cache->vertices[0].wp[0]==7&&cache->vertices[0].wp[1]==3&&cache->vertices[0].wn[1]==1);
 JgrModelLight light;memset(&light,0,sizeof light);o.owner=0;o.baked_light=&light;
 assert(!parse_vertices(&reader,0,cache,&o));assert(cache->vertices[0].wn[1]==1);
 jgr_model_cache_free(cache);
 FILE *streams[10];for(unsigned i=0;i<10;i++){streams[i]=tmpfile();assert(streams[i]);}
 JgrJgcBuild *build=jgr_jgc_create((JgrDisc*)&in,streams,0);assert(build);JgrJgcCharacter c;memset(&c,0,sizeof c);jgr_model_options_default(&c.model);build->current=&c;
 JgrModelNode node;memset(&node,0,sizeof node);double world[16];jgr_model_identity(world);world[3]=42;
 assert(!jgc_node(&node,world,0,-1,build));
 world[3]=INFINITY;assert(jgc_node(&node,world,0,-1,build));
 world[3]=0;world[0]=NAN;assert(jgc_node(&node,world,0,-1,build));
 jgr_model_identity(world);world[0]=2;assert(!jgc_node(&node,world,0,-1,build));
 /* Legacy still solves the transform needed by cross-bone vertices. */
 build->legacy=1;assert(!jgc_node(&node,world,0,-1,build));
 jgr_jgc_free(build);for(unsigned i=0;i<10;i++)fclose(streams[i]);
}
void position_test(void){
 double m[16],expected[3],actual[3];jgr_model_identity(m);assert(position_identity(m));
 const double cases[][3]={{0.0,-0.0,0.0},{-0.0,-0.0,-0.0},
  {1.25,-2.5,DBL_MIN},{DBL_MAX,0x1p-1074,-0x1p-1074},
  {INFINITY,2,3},{1,-INFINITY,3},{1,2,NAN}};
 for(unsigned i=0;i<sizeof cases/sizeof *cases;i++){
  transform(expected,m,cases[i],0);transform_position(actual,m,cases[i],1);
  assert(!memcmp(expected,actual,sizeof actual));
  unsigned char a[12],b[12];for(unsigned k=0;k<3;k++){wf(a+4*k,expected[k]);wf(b+4*k,actual[k]);}assert(!memcmp(a,b,sizeof a));
 }
 /* A negative zero coefficient must not be treated as the canonical matrix. */
 m[1]=-0.0;assert(!position_identity(m));
 transform(expected,m,cases[1],0);transform_position(actual,m,cases[1],position_identity(m));assert(!memcmp(expected,actual,sizeof actual));
 m[1]=0;m[3]=5;assert(!position_identity(m));
 transform(expected,m,cases[2],0);transform_position(actual,m,cases[2],position_identity(m));assert(!memcmp(expected,actual,sizeof actual));
 m[3]=0;m[4]=NAN;assert(!position_identity(m));
}
static uint32_t reference_color(const Vertex *v,const JgrModelOptions *o,unsigned flags,uint32_t diffuse){
 double normal[3],legacy_normal[3];memcpy(normal,v->n,sizeof normal);
 if(v->flags&2)transform(normal,o->light_matrix?o->light_matrix:o->matrix,normal,1);
 memcpy(legacy_normal,normal,sizeof normal);
 if(o->baked_light&&o->owner>=0&&o->cache_vertices&&(v->flags&2))memcpy(normal,v->wn,sizeof normal);
 if(!o->palette_mix)return shade(v,normal,diffuse,o,flags);
 JgrModelOptions legacy=*o,baked=*o;legacy.baked_light=NULL;baked.toon=1;
 uint32_t old=shade(v,legacy_normal,diffuse,&legacy,flags),color=old;
 if(!(flags&1)){
  uint32_t corrected=shade(v,normal,diffuse,&baked,flags);legacy.light=0;
  uint32_t unlit=shade(v,legacy_normal,diffuse,&legacy,flags);color=old&0xff000000u;
  for(unsigned shift=0;shift<24;shift+=8){
   double a=(old>>shift)&255,b=(corrected>>shift)&255;
   unsigned mixed=(unsigned)(a+(b-a)*0.5+0.5);
   mixed=(unsigned)(mixed+(((unlit>>shift)&255)-(double)mixed)*o->shadow_lift+0.5);
   color|=mixed<<shift;
  }
 }return color;
}
void normal_test(void){
 /* Exhaust lighting consumers and flags, including palette-mixed alpha
  * faces that still need the transformed legacy normal. */
 for(unsigned source=0;source<3;source++)for(unsigned lit=0;lit<2;lit++)
 for(unsigned colored=0;colored<2;colored++)for(unsigned baked=0;baked<2;baked++)
 for(unsigned alpha=0;alpha<2;alpha++)for(unsigned mix=0;mix<2;mix++){
  JgrModelOptions o;jgr_model_options_default(&o);o.owner=0;o.source_lighting=source;
  o.source_environment=1;o.light=lit;o.palette_mix=mix;o.shadow_lift=.2;
  JgrModelLight light;memset(&light,0,sizeof light);light.direction[0]=-.5;light.direction[1]=.8;
  light.ambient=.6;light.diffuse=.4;light.rgb[0]=.5;light.rgb[1]=.8;light.rgb[2]=.9;
  light.cel_direction[0]=1;for(unsigned i=0;i<3;i++){light.cel_colors[0][i]=100;light.cel_colors[1][i]=240;}
  o.baked_light=baked?&light:NULL;o.matrix[0]=2;o.matrix[4]=.5;o.matrix[10]=.7;
  Vertex v;memset(&v,0,sizeof v);v.flags=3|(colored?4:0);v.generation=1;v.owner=0;
  v.p[0]=2;v.n[0]=.3;v.n[1]=.4;v.n[2]=.5;v.color=0xff806040;memcpy(v.wn,v.n,sizeof v.n);
  JgrModelCache cache={&v,1,1};Group groups[1];uint32_t hash[128]={0};
  Assembly a;memset(&a,0,sizeof a);a.cache=&cache;a.options=&o;a.groups=groups;a.collect=1;a.hash=hash;a.hash_mask=127;
  Corner c[3]={{0,0,0,1},{0,0,0,1},{0,0,0,1}};uint32_t diffuse=0xffe0b080;
  assert(!triangle(&a,65535,alpha,diffuse,c));a.collect=0;
  unsigned before=sqrt_calls;assert(!triangle(&a,65535,alpha,diffuse,c));unsigned actual_calls=sqrt_calls-before;
  before=sqrt_calls;uint32_t expected=reference_color(&v,&o,alpha,diffuse);unsigned reference_calls=sqrt_calls-before;
  int uses=lit&&!source&&!colored&&(!baked||!alpha||mix);
  assert(actual_calls==reference_calls-(uses?0:1));
  assert(a.nv==1&&a.vertices[0].color==expected&&!memcmp(a.vertices[0].n,v.n,sizeof v.n));
  free(a.vertices);free(a.indices);
 }
}
void compact_test(void){
 unsigned char data[1472];memset(data,0,sizeof data);memcpy(data,"JGC2",4);
 unsigned h[]={1,1,0,0,0,1,3,80,128,176,176,176,176,192,288,320};
 for(unsigned i=0;i<16;i++)w32(data+4+4*i,h[i]);w32(data+84,1);w32(data+92,3);
 w16(data+176,16);w16(data+178,8);w32(data+184,320);w32(data+188,1344);
 for(unsigned i=0;i<3;i++){w32(data+192+8*i,1);w32(data+196+8*i,216+24*i);w32(data+216+24*i,1);w32(data+220+24*i,i==1?12:0);}
 for(unsigned i=0;i<6;i++)wf(data+288+4*i,i+1);
 FILE *in=tmpfile(),*out=tmpfile();assert(in&&out&&fwrite(data,1,sizeof data,in)==sizeof data);
 assert(!jgr_model_compact(in,out,0));unsigned char header[80],motions[24];rewind(out);assert(fread(header,1,80,out)==80);unsigned at=u32(header+56);assert(!fseek(out,at,SEEK_SET)&&fread(motions,1,24,out)==24);
 assert(u32(motions+4)==u32(motions+20)&&u32(motions+4)!=u32(motions+12));
 rewind(out);unsigned char buf[512];size_t n;while((n=fread(buf,1,sizeof buf,out)))assert(fwrite(buf,1,n,stdout)==n);fclose(in);fclose(out);
}
'''
PACK = r'''
#include <assert.h>
#include "portable_models_pack.c"
void pack_test(void){
 unsigned char bytes[]="ABCDEFuvwxyzABCDEFABCDEF";ReadSource input={NULL,bytes,24};Store s;memset(&s,0,sizeof s);
 s.file=tmpfile();s.contents.file=s.file;s.hash=calloc(HASH_SIZE,sizeof *s.hash);assert(s.file&&s.hash);
 unsigned a,z,again,aligned;
 assert(!intern(&s,&input,0,6,2,&a)&&a==0);assert(!intern(&s,&input,6,6,2,&z)&&z==6);
 assert(!intern(&s,&input,12,6,2,&again)&&again==a);assert(!intern(&s,&input,18,6,4,&aligned)&&aligned==12);
 assert(intern(&s,&input,23,2,2,&again));rewind(s.file);unsigned char out[18];assert(fread(out,1,18,s.file)==18&&!memcmp(out,"ABCDEFuvwxyzABCDEF",18));
 /* Equal hashes and descriptors do not merge different palette/pixel bytes. */
 unsigned char pixels[2304];memset(pixels,0,sizeof pixels);pixels[1152]=1;ReadSource bank={NULL,pixels,sizeof pixels};
 Texture x={16,8,0,0,0,1024,0},y={16,8,0,0,1152,2176,0};assert(texture_equal(&bank,&x,&y)==0);pixels[1152]=0;assert(texture_equal(&bank,&x,&y)==1);y.flags=1;assert(texture_equal(&bank,&x,&y)==0);
 fclose(s.file);free(s.hash);free(s.entries);
}
'''


class ModelIntern(unittest.TestCase):
    def test_forced_hash_collisions_keep_bytes_alignment_and_validation(self):
        if not shutil.which('cc'):
            self.skipTest('Host compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='jgr-model-intern-') as tmp:
            work = Path(tmp)
            (work/'model.c').write_text(MODEL)
            (work/'pack.c').write_text(PACK)
            (work/'main.c').write_text('void key_test(void),transform_test(void),position_test(void),normal_test(void),compact_test(void),pack_test(void);\nint main(void){key_test();transform_test();position_test();normal_test();pack_test();compact_test();return 0;}\n')
            (work/'collision.h').write_text('#include "portable_crc32.h"\nstatic inline uint32_t collision_hash(uint32_t h,const void*p,size_t n){(void)h;(void)p;(void)n;return 0;}\n#define jgr_crc32_add collision_hash\n')
            outputs = []
            for collide in (False, True):
                exe = work/('collisions' if collide else 'ordinary')
                command = ['cc', '-std=gnu99', '-O1', '-g', '-ffp-contract=off',
                           '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                           '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'psp/src')]
                if collide:
                    command += ['-include', str(work/'collision.h')]
                subprocess.run(command+[str(work/n) for n in ('model.c', 'pack.c', 'main.c')]+['-lm', '-o', str(exe)], check=True)
                run = subprocess.run([exe], capture_output=True)
                self.assertEqual(run.returncode, 0, run.stderr.decode())
                outputs.append(run.stdout)
            self.assertEqual(outputs[0], outputs[1])
            self.assertGreater(len(outputs[0]), 1000)


if __name__ == '__main__':
    unittest.main()
