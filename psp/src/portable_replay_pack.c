/* Bounded streaming serializer for resolved native and observed skater routes. */
#include "portable_replay_pack.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define JOINTS 20u
#define FLOATS 120u
#define ROW_BYTES 920u
#define MAX_FRAMES 30000u
typedef struct {unsigned char angles[8],value[24];} Bind;
struct JgrReplayPack {
 FILE *scratch;
 Bind bind[2][JOINTS];
 unsigned char base[FLOATS*4],varying[FLOATS];
 unsigned character,count,has_spray,observed,failed,finished,sections,part_count[3];
};
static uint32_t get32(const unsigned char *p)
{return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned get16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static void put32(unsigned char *p,uint32_t n)
{p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static void put16(unsigned char *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void put_real(unsigned char *p,float value)
{uint32_t bits;memcpy(&bits,&value,4);put32(p,bits);}
static int finite32(const unsigned char *p)
{return (get32(p)&0x7f800000u)!=0x7f800000u;}
static int read_at(FILE *f,unsigned long bytes,unsigned at,void *data,unsigned count)
{return at>bytes||count>bytes-at||fseek(f,at,SEEK_SET)||fread(data,1,count,f)!=count?-1:0;}
static int bind_pose(FILE *f,unsigned model,Bind bind[JOINTS])
{
 unsigned char h[64],c[48],b[48];long size;
 if(!f||fseek(f,0,SEEK_END)||(size=ftell(f))<64||
    read_at(f,size,0,h,sizeof h)||memcmp(h,"JGC2",4)||model>=get32(h+4))return -1;
 unsigned chars=get32(h+32),bones=get32(h+36);
 if(chars>(unsigned long)size||model>((unsigned long)size-chars)/48||
    read_at(f,size,chars+model*48,c,sizeof c)||get32(c+4)!=JOINTS)return -1;
 unsigned start=get32(c);
 if(bones>(unsigned long)size||start>((unsigned long)size-bones)/48)return -1;
 for(unsigned j=0;j<JOINTS;j++){
  if(start+j<start||start+j>((unsigned long)size-bones)/48||read_at(f,size,bones+(start+j)*48,b,sizeof b))return -1;
  for(unsigned k=0;k<3;k++){
   if(!finite32(b+4+k*4)||!finite32(b+28+k*4))return -1;
   memcpy(bind[j].value+k*4,b+4+k*4,4);memcpy(bind[j].value+12+k*4,b+28+k*4,4);
   put16(bind[j].angles+k*2,get32(b+16+k*4));
  }
  memcpy(bind[j].angles+6,b+2,2);
 }
 return 0;
}
JgrReplayPack *jgr_replay_pack_create_native(FILE *scratch,unsigned character)
{
 if(!scratch||character>=16||ftell(scratch)!=0)return NULL;
 JgrReplayPack *p=calloc(1,sizeof *p);if(!p)return NULL;
 p->scratch=scratch;p->character=character;return p;
}
JgrReplayPack *jgr_replay_pack_create(FILE *scratch,FILE *normal,unsigned model,FILE *spray,unsigned character)
{
 JgrReplayPack *p=jgr_replay_pack_create_native(scratch,character);if(!p)return NULL;
 if(bind_pose(normal,model,p->bind[0])||(spray&&bind_pose(spray,0,p->bind[1]))){free(p);return NULL;}
 p->has_spray=spray!=NULL;p->observed=1;return p;
}
int jgr_replay_pack_rival(JgrReplayPack *p,unsigned sections)
{if(!p||p->count||p->failed||p->finished||sections<2||sections>3)return -1;p->sections=sections;return 0;}
static int pose_at(const unsigned char *pose,unsigned pointer,unsigned address,unsigned size,unsigned char *out)
{
 if(address<pointer||address-pointer>0x400||size>0x400-(address-pointer))return -1;
 if(out)memcpy(out,pose+address-pointer,size);
 return 0;
}
static int sample_begin(JgrReplayPack *p,unsigned part,unsigned cut,unsigned state)
{
 if(!p||p->failed||p->finished)return -1;
 if(p->count>=MAX_FRAMES||part>65535||cut>1||state>65535)goto fail;
 if(p->sections){
  if(part>=p->sections||(part&&!p->part_count[part-1]))goto fail;
  for(unsigned i=part+1;i<p->sections;i++)if(p->part_count[i])goto fail;
 }
 return 0;
 fail:p->failed=1;return -1;
}
static int sample_write(JgrReplayPack *p,const unsigned char row[ROW_BYTES],unsigned part)
{
 for(unsigned k=0;k<3;k++)if(!finite32(row+k*4))goto fail;
 for(unsigned k=0;k<6;k++)if(!finite32(row+20+k*4))goto fail;
 if(!finite32(row+48)||!finite32(row+52))goto fail;
 for(unsigned k=0;k<FLOATS;k++)if(!finite32(row+224+k*4))goto fail;
 for(unsigned k=0;k<41;k++)if(!finite32(row+704+28+k*4))goto fail;
 if(p->sections&&get32(row+704+24))for(unsigned k=0;k<3;k++)if(!finite32(row+908+k*4))goto fail;
 if(!p->count)memcpy(p->base,row+224,FLOATS*4);
 else for(unsigned k=0;k<FLOATS;k++)if(memcmp(p->base+k*4,row+224+k*4,4))p->varying[k]=1;
 unsigned bytes=p->sections?ROW_BYTES:ROW_BYTES-12;
 if(fwrite(row,1,bytes,p->scratch)!=bytes)goto fail;
 p->count++;if(p->sections)p->part_count[part]++;return 0;
 fail:p->failed=1;return -1;
}
int jgr_replay_pack_sample(JgrReplayPack *p,const unsigned char a[0x900],const unsigned char c[0x290],const unsigned char pose[0x400],
                           unsigned part,unsigned cut,unsigned variant)
{
 if(!p||p->failed||p->finished)return -1;
 if(!p->observed||!a||!c||!pose||variant>p->has_spray)goto fail;
 if(sample_begin(p,part,cut,get32(a+0x70)))return -1;
 unsigned char row[ROW_BYTES]={0};
 memcpy(row,a+0xb8,12);
 for(unsigned k=0;k<3;k++)put16(row+12+k*2,get32(a+0x678+k*4));
 put16(row+18,get32(a+0x70));memcpy(row+20,c+0x190,24);
 put16(row+44,get32(c+0x1a8));put16(row+46,get32(c+0x1ac));
 put32(row+48,get32(c+0x1b0)^0x80000000u);put32(row+52,get32(c+0x1b4)^0x80000000u);
 put32(row+56,get32(a+0x98));put16(row+60,part);put16(row+62,cut);
 unsigned pointer=get32(a+0x594),table=get32(pose+4);
 if(pose_at(pose,pointer,table,JOINTS*16,NULL))goto fail;
 for(unsigned j=0;j<JOINTS;j++){
  unsigned char entry[16],*angles=row+64+j*8,*values=row+224+j*24;
  if(pose_at(pose,pointer,table+j*16,16,entry))goto fail;
  memcpy(angles,p->bind[variant][j].angles,8);memcpy(values,p->bind[variant][j].value,24);
  unsigned flags=get32(entry),eval=get16(angles+6);
  if(flags&1){if(pose_at(pose,pointer,get32(entry+4),12,values))goto fail;eval&=~1u;}
  if(flags&16){if(pose_at(pose,pointer,get32(entry+8),6,angles))goto fail;eval&=~2u;}
  if(flags&4){if(pose_at(pose,pointer,get32(entry+12),12,values+12))goto fail;eval&=~4u;}
  if(p->sections){if(eval>=8192)goto fail;eval|=((flags&1)|((flags&16)>>3)|(flags&4))<<13;}
  put16(angles+6,eval);
 }
 unsigned char *fx=row+704;
 static const unsigned ints[7]={0x5cc,0x78,0x1dc,0x1e4,0x108,0x10c,0x548};
 for(unsigned k=0;k<7;k++)memcpy(fx+k*4,a+ints[k],4);
 memcpy(fx+28,a+0x5ac,4);memcpy(fx+32,a+0xdc,4);memcpy(fx+36,a+0x820,12);
 for(unsigned k=0;k<12;k++)memcpy(fx+48+k*12,a+0x684+(k<8?k:k+11)*12,12);
 memcpy(fx+192,a+0x16c,4);memcpy(fx+196,a+0x168,4);memcpy(fx+200,a+0x170,4);
 if(p->sections&&get32(a+0x548)){
  memcpy(row+908,a+0x54c,12);
 }
 return sample_write(p,row,part);
 fail:p->failed=1;return -1;
}
int jgr_replay_pack_native_sample(JgrReplayPack *p,const JgrReplayNativeSample *s)
{
 if(!p||p->failed||p->finished)return -1;
 if(!s)goto fail;
 if(sample_begin(p,s->part,s->cut,s->state))return -1;
 unsigned char row[ROW_BYTES]={0};
 for(unsigned k=0;k<3;k++){
  put_real(row+k*4,s->position[k]);put16(row+12+k*2,(uint32_t)s->angle[k]);
  put_real(row+20+k*4,s->camera_position[k]);put_real(row+32+k*4,s->camera_direction[k]);
 }
 put16(row+18,s->state);put16(row+44,(uint32_t)s->camera_roll);put16(row+46,(uint32_t)s->camera_fov);
 put_real(row+48,s->near_clip);put_real(row+52,s->far_clip);
 put32(row+56,s->tick);put16(row+60,s->part);put16(row+62,s->cut);
 for(unsigned j=0;j<JOINTS;j++){
  const JgrReplayNativePose *v=s->pose+j;unsigned eval=v->evaluation;
  if(eval>65535 || v->keyed>7 || (p->sections&&eval>=8192))goto fail;
  for(unsigned k=0;k<3;k++){
   put16(row+64+j*8+k*2,(uint32_t)v->angle[k]);
   put_real(row+224+j*24+k*4,v->position[k]);put_real(row+236+j*24+k*4,v->scale[k]);
  }
  if(p->sections)eval|=v->keyed<<13;
  put16(row+70+j*8,eval);
 }
 /* Fixed-width members cover the complete payload without host padding or
  * pointer fields. Convert each word explicitly for the route's LE format. */
 typedef char effects_size_must_be_204[(sizeof(JgrReplayNativeEffects)==204)?1:-1];
 (void)sizeof(effects_size_must_be_204);
 uint32_t effects[51];memcpy(effects,&s->effects,sizeof effects);
 for(unsigned k=0;k<51;k++)put32(row+704+k*4,effects[k]);
 if(p->sections&&s->effects.obstruction){
  if(!s->rival_target)goto fail;
  for(unsigned k=0;k<3;k++)put_real(row+908+k*4,s->rival_target[k]);
 }
 return sample_write(p,row,s->part);
 fail:p->failed=1;return -1;
}
int jgr_replay_pack_finish(JgrReplayPack *p,FILE *out)
{
 if(!p||!out||p->failed||p->finished||!p->count)return -1;
 unsigned channels=0;for(unsigned k=0;k<FLOATS;k++)channels+=p->varying[k];
 unsigned char header[32]={0},row[ROW_BYTES],word[4];memcpy(header,p->sections?"TRT3":"TRT2",4);
 put32(header+4,p->count);put32(header+8,p->character);put32(header+12,JOINTS);
 put32(header+16,428+channels*4+(p->sections?12:0));put32(header+20,channels);
 if(p->sections){
  for(unsigned i=0;i<p->sections;i++)if(!p->part_count[i])goto fail;
  put32(header+24,p->part_count[0]);put32(header+28,p->part_count[0]+(p->sections==3?p->part_count[1]:0));
 }
 if(fflush(p->scratch)||fseek(p->scratch,0,SEEK_SET)||fwrite(header,1,32,out)!=32||
    fwrite(p->base,1,sizeof p->base,out)!=sizeof p->base)goto fail;
 for(unsigned k=0;k<FLOATS;k++)if(p->varying[k]){put32(word,k);if(fwrite(word,1,4,out)!=4)goto fail;}
 for(unsigned i=0;i<p->count;i++){
  unsigned bytes=p->sections?ROW_BYTES:ROW_BYTES-12;
  if(fread(row,1,bytes,p->scratch)!=bytes)goto fail;
  /* Gather forward in the existing row: every destination precedes its
     source, so compaction needs no second buffer or per-channel stdio calls. */
  unsigned size=224;
  for(unsigned k=0;k<FLOATS;k++)if(p->varying[k]){
   memmove(row+size,row+224+k*4,4);size+=4;
  }
  memmove(row+size,row+704,204);size+=204;
  if(p->sections){memmove(row+size,row+908,12);size+=12;}
  if(fwrite(row,1,size,out)!=size)goto fail;
 }
 p->finished=1;return 0;
 fail:p->failed=1;return -1;
}
unsigned jgr_replay_pack_count(const JgrReplayPack *p){return p?p->count:0;}
void jgr_replay_pack_destroy(JgrReplayPack *p){free(p);}
