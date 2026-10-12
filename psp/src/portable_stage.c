/* First-launch JGS conversion. Geometry, placement and texture bindings remain
 * source reads; only source-format addresses and port rendering rules are code. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#include "portable_stage.h"
#include "portable_models.h"
#include "portable_scratch.h"
#include "portable_convert.h"
#include "portable_indexed.h"
#include "portable_stage_pack.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef __PSP__
#include <pspkernel.h>
#else
#include <sys/time.h>
#endif
#ifndef JGR_STAGE_STATS
#define JGR_STAGE_STATS JGR_DEVICE_SETUP
#endif
static JgrStageStats stage_stats;
static JgrStageFailure stage_failure;
static unsigned stage_failure_depth;
void jgr_stage_failure(JgrStageFailure *out)
{if(out)*out=stage_failure;}
static void stage_failed(unsigned stage,unsigned step,unsigned section,FILE *source,FILE *output)
{
 if(stage_failure.step)return;
 int error=errno;
 stage_failure=(JgrStageFailure){.stage=stage,.step=step,.section=section,.error=error,
                               .source_offset=-1,.output_offset=-1};
 if(source){stage_failure.source_error=ferror(source);stage_failure.source_offset=ftell(source);}
 if(output){stage_failure.output_error=ferror(output);stage_failure.output_offset=ftell(output);}
#ifdef __PSP__
 stage_failure.heap_free=(unsigned)jgr_scratch_heap_available();
 stage_failure.kernel_free=sceKernelTotalFreeMemSize();
 stage_failure.kernel_max_free=sceKernelMaxFreeMemSize();
#endif
 errno=error;
}
#if JGR_STAGE_STATS
static uint64_t stage_tick,stage_started;
static uint64_t stage_clock(void)
{
#ifdef JGR_STAGE_STATS_TESTING
 extern uint64_t jgr_stage_test_clock(void);return jgr_stage_test_clock();
#elif defined(__PSP__)
 return sceKernelGetSystemTimeWide();
#else
 struct timeval t;gettimeofday(&t,NULL);return (uint64_t)t.tv_sec*1000000u+(unsigned)t.tv_usec;
#endif
}
static void stage_charge(JgrStageStats *s,uint64_t elapsed)
{
 s->total_us+=elapsed;
 switch(s->phase){case JGR_STAGE_PHASE_INPUT:s->input_us+=elapsed;break;
 case JGR_STAGE_PHASE_GEOMETRY:s->geometry_us+=elapsed;break;
 case JGR_STAGE_PHASE_TEXTURE:s->texture_us+=elapsed;break;
 case JGR_STAGE_PHASE_POSTPROCESS:s->postprocess_us+=elapsed;break;
 default:s->assembly_us+=elapsed;break;}
}
static uint64_t stage_settle(void)
{uint64_t now=stage_clock();if(stage_stats.active)stage_charge(&stage_stats,now-stage_tick);stage_tick=now;return now;}
#endif
void jgr_stage_stats_reset(void)
{memset(&stage_stats,0,sizeof stage_stats);memset(&stage_failure,0,sizeof stage_failure);stage_failure_depth=0;}
void jgr_stage_stats(JgrStageStats *out)
{
 if(!out)return;
 *out=stage_stats;
#if JGR_STAGE_STATS
 if(out->active){uint64_t now=stage_clock();stage_charge(out,now-stage_tick);out->current_us=now-stage_started;}
#endif
}
void jgr_stage_stats_begin(JgrStageStatsScope *scope,unsigned stage,unsigned kind)
{
 if(!stage_failure_depth++)memset(&stage_failure,0,sizeof stage_failure);
 *scope=(JgrStageStatsScope){stage_stats.active,stage_stats.stage,stage_stats.kind,stage_stats.phase};
#if JGR_STAGE_STATS
 uint64_t now=stage_settle();
 if(!stage_stats.active){stage_stats.jobs++;stage_started=now;}
 stage_stats.active=1;stage_stats.stage=stage;stage_stats.kind=kind;stage_stats.phase=JGR_STAGE_PHASE_INPUT;
#else
 (void)stage;(void)kind;
#endif
}
void jgr_stage_stats_phase(unsigned phase)
{
#if JGR_STAGE_STATS
 if(stage_stats.active){stage_settle();stage_stats.phase=phase;}
#else
 (void)phase;
#endif
}
void jgr_stage_stats_end(const JgrStageStatsScope *scope,int result)
{
 if(stage_failure_depth)stage_failure_depth--;
#if JGR_STAGE_STATS
 uint64_t now=stage_settle();
 if(!scope->active){stage_stats.current_us=now-stage_started;if(result)stage_stats.failed++;else stage_stats.completed++;}
 stage_stats.active=scope->active;stage_stats.stage=scope->stage;stage_stats.kind=scope->kind;stage_stats.phase=scope->phase;
#else
 (void)scope;(void)result;
#endif
}
#define EB 0x8c010000u
#define SB 0x8cb00000u
#define MAX_TEX 8192u
#define MAX_LIST 4096u
#define MAX_NODE 8192u
#define MAX_POINTS 524288u

typedef struct {FILE *geometry,*output;unsigned atlas,count,total,texture,batches,optional,valid;uint16_t indices[128];} Tunnel;
static struct {JgrDisc *disc;JgrStageCityCapture function;void *user;} city_capture;
void jgr_stage_city_capture(JgrDisc *disc,JgrStageCityCapture function,void *user)
{city_capture.disc=disc;city_capture.function=function;city_capture.user=user;}
static struct {JgrDisc *disc;JgrStageTextureCapture function;void *user;} texture_capture;
void jgr_stage_texture_capture(JgrDisc *disc,JgrStageTextureCapture function,void *user)
{texture_capture.disc=disc;texture_capture.function=function;texture_capture.user=user;}
typedef struct {uint32_t root,address;unsigned category,pid,sector;double position[3],scale[3];int32_t angle[3];} Placement;
typedef struct {Placement place;unsigned near_first,near_count,far_first,far_count,kind;double lod;} Owner;
typedef struct {uint32_t root;unsigned kind;} Origin;
typedef struct {JgrDiscFile source;uint32_t *offset;unsigned count,first,list,member;char name[64];} Pack;
typedef struct {uint32_t address;uint16_t map[128];unsigned count;} Texlist;
typedef struct {uint32_t node;unsigned list;} NodeList;
typedef struct {
 JgrDisc *disc;JgrDiscFile source,exe;unsigned stage;unsigned char *data,*code;
 Pack *packs;unsigned npacks,ntex;Texlist *lists;unsigned nlists;NodeList *nodes;unsigned nnodes;
 FILE *sections[7];JgrModelCache *model;unsigned objects,batches,vertices,indices,textures;
 uint16_t *remap;unsigned char *used;JgrJgcTexture *tex;
 double low[3],high[3],*points;unsigned npoints,capacity;unsigned root_list;
 unsigned city,ground;JgrModelLight light;const double *post,*lighting;
 unsigned char *node_flags;
 Owner *owners;unsigned nowners,owner_capacity,record,object_flags,force_list,near_objects;
 Origin *origins;unsigned origin_capacity;uint32_t root;unsigned origin_kind;
 unsigned final_city,key_batch;Tunnel *tunnel;

} Stage;
static uint32_t word(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void encode(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i));}
static void real(unsigned char *p,double d){float f=(float)d;uint32_t n;memcpy(&n,&f,4);encode(p,n);}
static int words(FILE *f,const uint32_t *p,unsigned n){unsigned char b[4];while(n--){encode(b,*p++);if(fwrite(b,1,4,f)!=4)return -1;}return 0;}
static int valid_code(Stage *s,uint32_t a,unsigned n){return a>=EB && a-EB<=s->exe.size && n<=s->exe.size-(a-EB);}
static int valid_data(Stage *s,uint32_t a,unsigned n){return a>=SB && a-SB<=s->source.size && n<=s->source.size-(a-SB);}
static uint32_t ew(Stage *s,uint32_t a){return valid_code(s,a,4)?word(s->code+(a-EB)):0;}
static int stage_pointer(uint32_t a){return a>=SB && a<SB+0x400000 && !(a&3);}
static int record_pointer(Stage *s,uint32_t a){return !(a&3) && valid_code(s,a,8) && valid_code(s,ew(s,a),1) && ew(s,a+4)>0 && ew(s,a+4)<=64;}
static int list_id(Stage *s,uint32_t address,int create)
{
 for(unsigned i=0;i<s->nlists;i++)if(s->lists[i].address==address)return (int)i;
 if(!create)return -1;
 unsigned count=ew(s,address+4);if(!valid_code(s,address,8)||!count||count>128||s->nlists>=MAX_LIST)return -1;
 Texlist *list=s->lists+s->nlists;list->address=address;list->count=count;
 for(unsigned i=0;i<128;i++)list->map[i]=0xffff;
 return (int)s->nlists++;
}
static int binding(Stage *s,uint32_t address,unsigned slot,unsigned texture)
{int i=list_id(s,address,1);if(i<0||texture>=MAX_TEX)return -1;/* Unused source references beyond texlist count are ignored by the loader. */if(slot>=s->lists[i].count)return 0;s->lists[i].map[slot]=texture;return 0;}
static int offsets(Stage *s,Pack *pack)
{
 unsigned char buffer[4096];uint32_t at=0;pack->offset=malloc(MAX_TEX*sizeof *pack->offset);if(!pack->offset)return -1;
 while(at+4<=pack->source.size){unsigned n=pack->source.size-at;if(n>sizeof buffer)n=sizeof buffer;
  if(jgr_disc_read(s->disc,&pack->source,at,buffer,n))return -1;
  unsigned i=0;while(i+4<=n){if(!memcmp(buffer+i,"GBIX",4)){if(pack->count>=MAX_TEX)return -1;pack->offset[pack->count++]=at+i;i+=8;}else i++;}at+=i;
 }
 if(!pack->count || pack->count>MAX_TEX-s->ntex)return -1;
 pack->first=s->ntex;s->ntex+=pack->count;return 0;
}
static int pack_index(const Pack *p,uint32_t offset)
{for(unsigned i=0;i<p->count;i++)if(p->offset[i]==offset)return (int)(p->first+i);return -1;}
static int pack_order(const void *a,const void *b)
{
 const Pack *x=a,*y=b;int xa=strstr(x->name,"_ALL")!=NULL,ya=strstr(y->name,"_ALL")!=NULL;
 if(xa!=ya)return ya-xa;
 int n=strcmp(x->name,y->name);return n?n:x->member<y->member?-1:x->member>y->member;
}
static int textures(Stage *s)
{
 char (*names)[64]=calloc(1024,64);unsigned count=0;int result=-1;if(!names)return -1;
 for(unsigned i=0;i<s->exe.size;) {
  unsigned begin=i;
  while(i<s->exe.size && ((s->code[i]>='A'&&s->code[i]<='Z')||(s->code[i]>='0'&&s->code[i]<='9')||s->code[i]=='_'))i++;
  unsigned n=i-begin;
  if(n && n<64 && i+4<=s->exe.size && !memcmp(s->code+i,".AFS",4)){if(count>=1024)goto done;memcpy(names[count++],s->code+begin,n);i+=4;}
  else if(i<s->exe.size)i++;
 }
 char prefix[20];snprintf(prefix,sizeof prefix,"STAGE%uTXP",s->stage);
 for(unsigned at=0;at+64<s->exe.size;at+=4) {
  unsigned id=word(s->code+at),member=word(s->code+at+4),base=word(s->code+at+8),list=word(s->code+at+12);
  if(word(s->code+at+20)!=1 || id>=count || member>=256 || base<0x8c800000 || base>=0x8d000000 || !valid_code(s,list,1) || strncmp(names[id],prefix,strlen(prefix)))continue;
  unsigned i;for(i=0;i<s->npacks;i++)if(!strcmp(s->packs[i].name,names[id]) && s->packs[i].member==member)break;
  if(i<s->npacks)continue;
  if(s->npacks>=128)goto done;
  Pack *p=s->packs+s->npacks++;strcpy(p->name,names[id]);p->member=member;p->list=list;
 }
 qsort(s->packs,s->npacks,sizeof *s->packs,pack_order);
 if(!s->npacks && (s->stage==5 || s->stage==6)) {
  Pack *p=s->packs;s->npacks=1;char archive[40];JgrDiscFile afs;
  snprintf(archive,sizeof archive,"JETRADIO/STAGE%u.AFS",s->stage);
  if(jgr_disc_find(s->disc,archive,&afs)||jgr_disc_afs(s->disc,&afs,1,&p->source)||offsets(s,p))goto done;
  uint32_t at=s->stage==5?0x8c209908:0x8c219698;unsigned guard=0;
  while(ew(s,at)) {
   if(++guard>MAX_LIST || !valid_code(s,at,8))goto done;
   uint32_t list=ew(s,at),pointers=ew(s,at+4);unsigned n=ew(s,list+4);if(n>128 || !valid_code(s,pointers,n*4))goto done;
   for(unsigned slot=0;slot<n;slot++){int texture=pack_index(p,ew(s,pointers+slot*4)-0x8c800000);if(texture>=0 && binding(s,list,slot,texture))goto done;}
   at+=8;
  }
 } else for(unsigned i=0;i<s->npacks;i++) {
  Pack *p=s->packs+i;JgrDiscFile afs;char archive[100];snprintf(archive,sizeof archive,"JETRADIO/%s.AFS",p->name);
  if(jgr_disc_find(s->disc,archive,&afs)||jgr_disc_afs(s->disc,&afs,p->member,&p->source)||offsets(s,p))goto done;
  uint32_t base=0xffffffffu,at=p->list;unsigned guard=0;
  while(ew(s,at)){if(!valid_code(s,at,8)||++guard>MAX_TEX)goto done;unsigned data=ew(s,at+4);if(data<base)base=data;at+=8;}
  for(at=p->list;ew(s,at);at+=8) {
   int texture=pack_index(p,ew(s,at+4)-base);if(texture<0)continue;
   uint32_t refs=ew(s,at);unsigned limit=0;
   while(valid_code(s,refs,8)&&ew(s,refs)) {if(++limit>MAX_LIST||binding(s,ew(s,refs),ew(s,refs+4),texture))goto done;refs+=8;}
  }
 }
 /* Original object/texlist runs use suffix alignment, except the documented
  * Shibuya ground table's trailing untextured object. */
 for(uint32_t at=EB;valid_code(s,at,9);) {
  if(!stage_pointer(ew(s,at))){at+=4;continue;}
  uint32_t start=at;while(valid_code(s,at,4)&&stage_pointer(ew(s,at)))at+=4;
  unsigned n=(at-start)/4;uint32_t refs=at;
  while(valid_code(s,refs,4)&&!ew(s,refs)&&refs<at+4*n)refs+=4;
  uint32_t r0=refs;while(valid_code(s,refs,4)&&(record_pointer(s,ew(s,refs))||!ew(s,refs))&&refs<r0+4*n)refs+=4;
  uint32_t end=refs;while(end>r0&&!ew(s,end-4))end-=4;unsigned nr=(end-r0)/4;
  if(nr && nr<=n)for(unsigned j=0;j<nr;j++) {
   int list=list_id(s,ew(s,r0+4*j),0);if(list<0)continue;
   unsigned index=(s->stage==1 && start==0x8c106e0c && r0==0x8c106ed4)?j:n-nr+j;
   uint32_t node=ew(s,start+4*index);unsigned k;
   for(k=0;k<s->nnodes;k++)if(s->nodes[k].node==node)break;
   if(k==s->nnodes){if(s->nnodes>=MAX_NODE)goto done;s->nnodes++;}
   s->nodes[k].node=node;s->nodes[k].list=(unsigned)list;
  }
  if(refs>at)at=refs;
 }
 result=s->npacks?0:-1;
done:free(names);return result;
}
#include "portable_stage_water.inc"
static int emit(const JgrModelBatch *b,void *opaque)
{
 Stage *s=opaque;if(b->stride!=24 || !b->positions || b->vertices>MAX_POINTS-s->npoints)return -1;
 if(s->npoints+b->vertices>s->capacity){unsigned n=s->capacity?s->capacity:1024;while(n<s->npoints+b->vertices)n*=2;if(n>MAX_POINTS)n=MAX_POINTS;double *p=realloc(s->points,(size_t)n*3*sizeof *p);if(!p)return -1;s->points=p;s->capacity=n;}
 memcpy(s->points+3*s->npoints,b->positions,(size_t)b->vertices*3*sizeof(double));s->npoints+=b->vertices;
 for(unsigned i=0;i<b->vertices;i++)for(unsigned k=0;k<3;k++){double d=b->positions[3*i+k];if(d<s->low[k])s->low[k]=d;if(d>s->high[k])s->high[k]=d;}
 unsigned char row[16];encode(row,s->vertices);encode(row+4,s->indices);row[8]=b->vertices;row[9]=b->vertices>>8;row[10]=b->indices;row[11]=b->indices>>8;
 unsigned texture=b->texture,flags=b->flags;if(texture==s->ntex)flags|=16;
 if(s->tunnel&&texture==s->tunnel->atlas){
  s->tunnel->total++;
  if(!(flags&1)){
   if(s->tunnel->count>=128||s->batches>65535){
    if(!s->tunnel->optional)return -1;
    s->tunnel->valid=0;s->tunnel=NULL;
   }else s->tunnel->indices[s->tunnel->count++]=(uint16_t)s->batches;
  }
 }
 if(texture!=0xffff){if(texture>s->ntex)return -1;s->used[texture]=1;}
 row[12]=texture;row[13]=texture>>8;row[14]=flags;row[15]=flags>>8;
 if(fwrite(row,1,16,s->sections[1])!=16 || fwrite(b->vertex_bytes,24,b->vertices,s->sections[2])!=b->vertices || fwrite(b->index_bytes,2,b->indices,s->sections[3])!=b->indices)return -1;
 if(s->final_city&&s->stage==2&&fwrite(b->vertex_bytes,24,b->vertices,s->sections[6])!=b->vertices){stage_failed(s->stage,JGR_STAGE_FAILURE_KEY_WRITE,6,NULL,s->sections[6]);return -1;}
 s->vertices+=b->vertices;s->indices+=b->indices;s->batches++;
 if(s->indices&1){if(fputc(0,s->sections[3])==EOF||fputc(0,s->sections[3])==EOF)return -1;s->indices++;}return 0;
}
static int legacy_key_emit(const JgrModelBatch *b,void *opaque)
{
 Stage *s=opaque;unsigned char row[16];
 if(b->stride!=24||fseek(s->sections[1],s->key_batch++*16,SEEK_SET)||fread(row,1,16,s->sections[1])!=16){stage_failed(s->stage,JGR_STAGE_FAILURE_KEY_UPDATE,1,s->sections[1],s->sections[6]);return -1;}
 unsigned first=word(row),count=row[8]|(unsigned)row[9]<<8;if(count!=b->vertices){stage_failed(s->stage,JGR_STAGE_FAILURE_KEY_UPDATE,1,s->sections[1],s->sections[6]);return -1;}
 unsigned bytes=count*24;unsigned char *vertices=malloc(bytes?bytes:1);if(!vertices){errno=ENOMEM;stage_failed(s->stage,JGR_STAGE_FAILURE_KEY_UPDATE,6,s->sections[6],NULL);return -1;}int result=-1;
 if(fseek(s->sections[6],first*24,SEEK_SET)||fread(vertices,1,bytes,s->sections[6])!=bytes)goto done;
 for(unsigned i=0;i<count;i++){if(memcmp(vertices+i*24,b->vertex_bytes+i*24,8))goto done;memcpy(vertices+i*24+12,b->vertex_bytes+i*24+12,12);}
 if(fseek(s->sections[6],first*24,SEEK_SET)||fwrite(vertices,1,bytes,s->sections[6])!=bytes)goto done;
 result=0;
 done:if(result)stage_failed(s->stage,JGR_STAGE_FAILURE_KEY_UPDATE,6,s->sections[6],s->sections[6]);free(vertices);return result;
}
static int visit(const JgrModelNode *node,const double matrix[16],unsigned ordinal,int parent,void *opaque)
{
 (void)ordinal;(void)parent;Stage *s=opaque;if(!node->model || node->eval&8)return 0;
 unsigned list=s->root_list;for(unsigned i=0;!s->force_list&&i<s->nnodes;i++)if(s->nodes[i].node==node->address){list=s->nodes[i].list;break;}
 uint16_t map[128];for(unsigned i=0;i<128;i++)map[i]=(list<s->nlists && i<s->lists[list].count && s->lists[list].map[i]!=0xffff)?s->lists[list].map[i]:s->ntex;
 JgrModelOptions o;jgr_model_options_default(&o);o.owner=-1;o.cache_vertices=0;o.texture_count=s->ntex+1;o.texture_map=map;o.texture_slots=128;memcpy(o.matrix,matrix,sizeof o.matrix);
 if(s->city){o.max_edge=120;o.two_sided=s->ground;o.baked_light=s->stage==1?&s->light:NULL;o.post_matrix=s->post;}
 if(s->city==4){o.light=0;o.source_material=1;o.baked_light=NULL;}
 double lm[16];if(s->lighting){jgr_model_multiply(lm,s->lighting,matrix);o.light_matrix=lm;}
 unsigned first=s->batches;s->npoints=0;
 if(jgr_model_mesh(s->model,s->disc,node->model>=SB?&s->source:&s->exe,node->model>=SB?SB:EB,node->model,&o,emit,s))return -1;
 if(s->final_city&&s->stage==2&&s->lighting){
  JgrModelOptions old=o;jgr_model_multiply(old.matrix,s->lighting,matrix);old.post_matrix=NULL;old.light_matrix=NULL;
  s->key_batch=first;unsigned expected=s->batches;
  if(jgr_model_mesh(s->model,s->disc,node->model>=SB?&s->source:&s->exe,node->model>=SB?SB:EB,node->model,&old,legacy_key_emit,s)||s->key_batch!=expected||fseek(s->sections[1],0,SEEK_END)||fseek(s->sections[6],0,SEEK_END))return -1;
 }

 if(!s->npoints)return 0;
 double center[3]={0},radius=0;for(unsigned i=0;i<s->npoints;i++)for(unsigned k=0;k<3;k++)center[k]+=s->points[3*i+k];
 for(unsigned k=0;k<3;k++)center[k]/=s->npoints;
 for(unsigned i=0;i<s->npoints;i++){double r=0;for(unsigned k=0;k<3;k++){double d=s->points[3*i+k]-center[k];r+=d*d;}if(r>radius)radius=r;}
 unsigned char row[32]={0};for(unsigned k=0;k<3;k++)real(row+4*k,center[k]);real(row+12,sqrt(radius));encode(row+16,first);encode(row+20,s->batches-first);encode(row+24,s->object_flags);encode(row+28,s->record);
 if(s->final_city){
  if(s->objects>=s->origin_capacity){unsigned n=s->origin_capacity?s->origin_capacity*2:2048;if(n>65536)return -1;Origin *p=realloc(s->origins,n*sizeof *p);if(!p)return -1;s->origins=p;s->origin_capacity=n;}
  s->origins[s->objects]=(Origin){s->root,s->origin_kind};
 }
 if(fwrite(row,1,32,s->sections[0])!=32)return -1;
 s->objects++;return 0;
}
static int write_texture(Stage *s,unsigned index)
{
 unsigned sw=16,sh=16;unsigned char *rgba=NULL,*raw=NULL;int result=-1;
 JgrStageTextureSource captured={0};unsigned capture=0;
 if(index==s->ntex){rgba=malloc(1024);if(!rgba)return -1;for(unsigned i=0;i<256;i++){rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=160;rgba[i*4+3]=255;}}
 else {
  Pack *pack=NULL;for(unsigned i=0;i<s->npacks;i++)if(index>=s->packs[i].first&&index-s->packs[i].first<s->packs[i].count){pack=s->packs+i;break;}
  if(!pack)return -1;
  unsigned at=pack->offset[index-pack->first],n=pack->source.size-at;unsigned char head[80];if(n>sizeof head)n=sizeof head;
  if(jgr_disc_read(s->disc,&pack->source,at,head,n))return -1;
  unsigned pv=8;while(pv+16<=n && memcmp(head+pv,"PVRT",4))pv++;
  if(pv+16>n)return -1;
  unsigned size=word(head+pv+4);if(size<8 || size>4*1024*1024)return -1;size-=8;
  if(size>pack->source.size-at-pv-16)size=pack->source.size-at-pv-16;
  sw=head[pv+12]|(unsigned)head[pv+13]<<8;sh=head[pv+14]|(unsigned)head[pv+15]<<8;raw=malloc(size?size:1);if(!raw)return -1;
  if(jgr_disc_read(s->disc,&pack->source,at+pv+16,raw,size))goto done;
  rgba=jgr_pvr_decode_payload(raw,size,head[pv+8],head[pv+9],sw,sh);if(!rgba)goto done;
  unsigned df=head[pv+9];
  if(texture_capture.disc==s->disc && texture_capture.function && pv<64 && sw==sh && sw>=64 &&
     (df==2 || df==4 || df==0x11 || df==0x12)) {
   captured.file=pack->source;captured.gbix=at;captured.source_width=sw;captured.source_height=sh;capture=1;
  }
 }
 unsigned w=sw,h=sh,limit=s->city==4?512:128;while(w>limit||h>limit){w/=2;h/=2;}if(w<16)w=16;if(h<8)h=8;
 result=jgr_indexed_texture(rgba,sw,sh,w,h,s->sections[5],s->tex+s->textures);
 if(!result && capture && w==h && w>=64 && w<=256) {
  JgrJgcTexture *t=s->tex+s->textures;FILE *stream=s->sections[5];long position=ftell(stream);int had_error=ferror(stream);
  captured.width=t->width;captured.height=t->height;captured.clut=t->clut;captured.pixels=t->pixels;
  if(position>=0) {
   texture_capture.function(s->disc,&captured,rgba,stream,texture_capture.user);
   if(!had_error)clearerr(stream);
   if(fseek(stream,position,SEEK_SET))result=-1;
  }
 }
 if(!result){s->remap[index]=s->textures;s->textures++;}
 done:free(raw);free(rgba);return result;
}
static int pad(FILE *out,unsigned n)
{long at=ftell(out);if(at<0||(unsigned long)at>n)return -1;while((unsigned long)at++<n)if(fputc(0,out)==EOF)return -1;return 0;}
static int stream(FILE *from,FILE *to)
{unsigned char b[4096];if(fseek(from,0,SEEK_SET))return -1;size_t n;while((n=fread(b,1,sizeof b,from)))if(fwrite(b,1,n,to)!=n)return -1;return ferror(from)?-1:0;}
static int stream_bytes(FILE *from,FILE *to,unsigned count)
{unsigned char b[4096];if(fseek(from,0,SEEK_SET))return -1;while(count){unsigned n=count<sizeof b?count:sizeof b;if(fread(b,1,n,from)!=n||fwrite(b,1,n,to)!=n)return -1;count-=n;}return 0;}
static int finish(Stage *s,FILE *out)
{
 jgr_stage_stats_phase(JGR_STAGE_PHASE_TEXTURE);
 for(unsigned i=0;i<=s->ntex;i++)if(s->used[i]&&s->remap[i]==0xffff&&write_texture(s,i))return -1;
 jgr_stage_stats_phase(JGR_STAGE_PHASE_ASSEMBLY);
 unsigned sizes[6]={s->textures*16,s->objects*32,s->batches*16,s->vertices*24,s->indices*2,0};uint32_t offsets_[6];unsigned cursor=80;
 for(unsigned i=0;i<6;i++){offsets_[i]=cursor;if(sizes[i]>0x70000000-cursor)return -1;cursor=(cursor+sizes[i]+15)&~15u;}
 uint32_t counts[5]={s->textures,s->objects,s->batches,s->vertices,s->indices};unsigned char bounds[24];for(unsigned i=0;i<3;i++){real(bounds+4*i,s->low[i]);real(bounds+12+4*i,s->high[i]);}
 if(fwrite("JGS1",1,4,out)!=4||words(out,counts,5)||words(out,offsets_,6)||fwrite(bounds,1,24,out)!=24||pad(out,offsets_[0]))return -1;
 for(unsigned i=0;i<s->textures;i++){JgrJgcTexture *t=s->tex+i;unsigned char r[16]={t->width,t->width>>8,t->height,t->height>>8,t->flags,0,0,0};encode(r+8,offsets_[5]+t->clut);encode(r+12,offsets_[5]+t->pixels);if(fwrite(r,1,16,out)!=16)return -1;}
 if(pad(out,offsets_[1])||stream_bytes(s->sections[0],out,s->objects*32)||pad(out,offsets_[2])||fseek(s->sections[1],0,SEEK_SET))return -1;
 for(unsigned i=0;i<s->batches;i++){unsigned char r[16];if(fread(r,1,16,s->sections[1])!=16)return -1;unsigned tex=r[12]|r[13]<<8,flags=r[14]|r[15]<<8;unsigned mapped=tex==0xffff?0:s->remap[tex];if(tex!=0xffff&&s->tex[mapped].flags)flags|=1;r[12]=mapped;r[13]=mapped>>8;r[14]=flags;r[15]=flags>>8;if(fwrite(r,1,16,out)!=16)return -1;}
 int result=pad(out,offsets_[3])||stream(s->sections[2],out)||pad(out,offsets_[4])||stream(s->sections[3],out)||pad(out,offsets_[5])||stream(s->sections[5],out)?-1:0;
#if JGR_STAGE_STATS
 if(!result){uint64_t bytes=offsets_[5];if(s->textures){JgrJgcTexture *last=s->tex+s->textures-1;bytes+=last->pixels+last->width*last->height;}stage_stats.output_bytes+=bytes;}
#endif
 return result;
}
static int merge_object(Stage *s,unsigned first_object,unsigned first_batch)
{
 unsigned parts=s->objects-first_object;double center[3]={0},radius=0;unsigned char row[32];
 if(fseek(s->sections[0],first_object*32,SEEK_SET))return -1;
 for(unsigned i=0;i<parts;i++){if(fread(row,1,32,s->sections[0])!=32)return -1;for(unsigned k=0;k<3;k++){uint32_t u=word(row+4*k);float f;memcpy(&f,&u,4);center[k]+=f;}}
 if(parts)for(unsigned k=0;k<3;k++)center[k]/=parts;
 if(fseek(s->sections[0],first_object*32,SEEK_SET))return -1;
 for(unsigned i=0;i<parts;i++){double d=0;float f;if(fread(row,1,32,s->sections[0])!=32)return -1;for(unsigned k=0;k<3;k++){uint32_t u=word(row+4*k);memcpy(&f,&u,4);double delta=f-center[k];d+=delta*delta;}uint32_t u=word(row+12);memcpy(&f,&u,4);d=sqrt(d)+f;if(d>radius)radius=d;}
 memset(row,0,sizeof row);for(unsigned k=0;k<3;k++)real(row+4*k,center[k]);real(row+12,radius);encode(row+16,first_batch);encode(row+20,s->batches-first_batch);
 if(fseek(s->sections[0],first_object*32,SEEK_SET)||fwrite(row,1,32,s->sections[0])!=32)return -1;
 s->objects=first_object+1;return 0;
}
/* Category/source descriptors mirror the original stage loader tables. */
typedef struct {uint32_t table,near,far;unsigned stride,count;} Category;
static const Category categories[7][4]={
 {{0}},
 {{0x8c105e98,0x8c1063b4,0x8c1068dc,40,165},{0x8c105f94,0x8c106e0c,0x8c106f9c,40,50},{0x8c106090,0x8c10712c,0x8c1072dc,40,54},{0x8c10618c,0x8c107564,0,40,28}},
 {{0x8c107ef4,0x8c108cf0,0,40,327},{0x8c1080bc,0x8c109728,0x8c109990,52,77},{0x8c107d2c,0x8c1086a0,0x8c1089c8,52,101},{0x8c108284,0x8c10a2dc,0,40,91}},
 {{0x8c1ba6f0,0x8c1bab40,0x8c1baed8,40,115},{0x8c1ba860,0x8c1bb530,0,52,239},{0x8c1ba7a8,0x8c1bb270,0,40,88},{0x8c1ba9d0,0x8c1bc4cc,0,52,32}},
 {{0}},
 {{0x8c202e28,0x8c2033b4,0x8c20357c,40,56},{0x8c203060,0x8c203c74,0x8c203e24,52,35},{0x8c202f44,0x8c203744,0x8c2039dc,40,82},{0x8c203298,0x8c204084,0,52,20}},
 {{0x8c20a8f8,0x8c20af4c,0x8c20b114,40,56},{0x8c20aa3c,0x8c20b2dc,0,40,1},{0x8c20ab80,0x8c20b2ec,0x8c20b334,52,8},{0}}
};
typedef struct {Placement *rows;unsigned count,capacity;} Placements;
static uint32_t dw(Stage *s,uint32_t at){return valid_data(s,at,4)?word(s->data+(at-SB)):0;}
static double df(Stage *s,uint32_t at){uint32_t n=dw(s,at);float f;memcpy(&f,&n,4);return f;}
static int dataptr(Stage *s,uint32_t at){return !(at&3)&&valid_data(s,at,1);}
static int add_placement(Stage *s,Placements *p,uint32_t root,uint32_t at,unsigned category,unsigned pid,unsigned stride,unsigned sector)
{
 if(!valid_data(s,at,stride)||p->count>=32768)return -1;
 if(p->count==p->capacity){unsigned n=p->capacity?p->capacity*2:1024;Placement *rows=realloc(p->rows,n*sizeof *rows);if(!rows)return -1;p->rows=rows;p->capacity=n;}
 Placement *row=p->rows+p->count++;memset(row,0,sizeof *row);row->root=root;row->address=at;row->category=category;row->pid=pid;row->sector=sector;
 for(unsigned k=0;k<3;k++){row->position[k]=df(s,at+4+4*k);row->angle[k]=(int32_t)dw(s,at+16+4*k);row->scale[k]=stride==52?df(s,at+28+4*k):1;if(!isfinite(row->position[k])||!isfinite(row->scale[k]))return -1;}
 return 0;
}
static int placements(Stage *s,Placements *out,const Category *cat,unsigned sectors,unsigned category,int group)
{
 uint32_t roots[600];unsigned count=cat->count;
 if(count>600)return -1;
 if(!count){while(count<600&&dataptr(s,ew(s,cat->near+4*count)))count++;}
 for(unsigned i=0;i<count;i++){uint32_t r=ew(s,cat->near+4*i);roots[i]=dataptr(s,r)?r:0;if(group&&roots[i])roots[i]=dataptr(s,dw(s,r+4))?dw(s,r+4):0;}
 if(!group) {
  for(unsigned i=0;i<count;i++)if(roots[i])s->node_flags[(roots[i]-SB)/4]|=8|(category==1?64:0);
  if(cat->far)for(unsigned i=0;i<600;i++){uint32_t r=ew(s,cat->far+4*i);if(!dataptr(s,r))break;s->node_flags[(r-SB)/4]|=16;}
 }
 for(unsigned sector=0;sector<sectors;sector++) {
  uint32_t first=ew(s,cat->table+4*sector);if(!dataptr(s,first))continue;
  int pointers=dataptr(s,dw(s,first));unsigned guard=0;
  for(uint32_t at=first;;at+=pointers?4:cat->stride) {
   uint32_t row=pointers?dw(s,at):at;
   if(pointers&&!dataptr(s,row))break;
   if(!valid_data(s,row,cat->stride))break;
   int32_t pid=(int32_t)dw(s,row);if(!pointers&&pid< -1)break;
   if(++guard>10000)return -1;
   if(pid>=0&&(unsigned)pid<count&&roots[pid]&&add_placement(s,out,roots[pid],row,group?4:category,pid,cat->stride,sector))return -1;
  }
 }
 return 0;
}
static void rotate(double out[16],unsigned axis,double angle)
{
 jgr_model_identity(out);double c=cos(angle),sn=sin(angle);
 if(axis==0){out[5]=c;out[6]=-sn;out[9]=sn;out[10]=c;}
 if(axis==1){out[0]=c;out[2]=sn;out[8]=-sn;out[10]=c;}
 if(axis==2){out[0]=c;out[1]=-sn;out[4]=sn;out[5]=c;}
}
static void placed(double out[16],const Placement *p,int audited,int scale)
{
 double t[16],rx[16],ry[16],rz[16],a[16],b[16],sc[16];
 const double a2r=3.141592653589793*2/65536;
 jgr_model_identity(t);t[3]=p->position[0];t[7]=p->position[1];t[11]=p->position[2];
 rotate(rx,0,p->angle[0]*a2r);rotate(ry,1,p->angle[1]*a2r);rotate(rz,2,p->angle[2]*a2r);
 if(audited){jgr_model_multiply(a,ry,rx);jgr_model_multiply(b,rz,a);}else{jgr_model_multiply(a,ry,rz);jgr_model_multiply(b,rx,a);}
 jgr_model_multiply(out,t,b);
 if(scale){jgr_model_identity(sc);sc[0]=p->scale[0];sc[5]=p->scale[1];sc[10]=p->scale[2];jgr_model_multiply(a,out,sc);memcpy(out,a,sizeof a);}
}
static void placement_matrices(Stage *s,const Placement *p,double matrix[16],double post[16],double light[16])
{
 unsigned special=p->address==0x8cd6b198||p->address==0x8cd6bbb8||p->address==0x8cd6bb84;
 unsigned roof=s->stage==2&&p->category==2&&(p->pid==87||p->pid==88||special);
 static const unsigned benten[]={7,8,9,22,23,24,25,30,49,59,72,73,74,86};unsigned changed=roof;
 if(s->stage==3&&p->category==2)for(unsigned i=0;i<sizeof benten/sizeof benten[0];i++)changed|=p->pid==benten[i];
 placed(matrix,p,s->stage==1||changed,roof);s->post=NULL;s->lighting=NULL;
 if(changed){double old[16],inverse[16];placed(old,p,0,0);jgr_model_inverse(inverse,matrix);jgr_model_multiply(light,old,inverse);s->lighting=light;}
 if(roof&&special){double old[16],inverse[16];placed(old,p,0,0);jgr_model_inverse(inverse,old);jgr_model_multiply(post,matrix,inverse);memcpy(matrix,old,sizeof old);s->post=post;jgr_model_identity(light);}
}
typedef struct {Stage *stage;int uv,textured;} Classification;
/* The discovery pass inspects polygon headers without decoding vertex data.
 * Non-rendered source roots can legitimately carry non-render vertex lists. */
static int has_uv(Stage *s,uint32_t model)
{
 if(!valid_data(s,model,8))return -1;
 uint32_t at=dw(s,model+4);if(!at)return 0;
 for(unsigned guard=0;guard<1048576;guard++){
  if(!valid_data(s,at,2))return -1;
  unsigned t=s->data[at-SB];if(t==255)return 0;
  if(t<=5){at+=2;continue;}
  if(t==8||t==9){if(!valid_data(s,at,4))return -1;at+=4;continue;}
  if(!((t>=16&&t<=31)||(t>=56&&t<=58)||(t>=64&&t<=75))||!valid_data(s,at,4))return -1;
  unsigned n=s->data[at-SB+2]|(unsigned)s->data[at-SB+3]<<8;
  if(!valid_data(s,at,4+2*n))return -1;
  if(t==65||t==66||t==68||t==69||t==71||t==72){
   if(n<1)return -1;
   unsigned strips=s->data[at-SB+4]|(unsigned)s->data[at-SB+5]<<8;
   if(strips&0x3fff)return 1;
  }
  at+=4+2*n;
 }
 return -1;
}
static int group_chunk_model(Stage *s,uint32_t model)
{
 static const unsigned char strides[]={16,32,12,16,16,16,16,16,16,24,28,28,28,28,28,28,16,16,16};
 if(!valid_data(s,model,24))return 0;
 uint32_t at=dw(s,model);if(!at||!dw(s,model+4))return 0;
 for(unsigned guard=0;guard<65536;guard++){
  if(!valid_data(s,at,4))return 0;
  unsigned t=s->data[at-SB];if(t==255)return has_uv(s,model)>=0;
  if(!t){at+=4;continue;}
  if(t<32||t>50||!valid_data(s,at,8))return 0;
  unsigned size=s->data[at-SB+2]|(unsigned)s->data[at-SB+3]<<8;
  unsigned count=s->data[at-SB+6]|(unsigned)s->data[at-SB+7]<<8;
  if(!size||count>(size*4-4)/strides[t-32]||!valid_data(s,at,4+size*4))return 0;
  at+=4+size*4;
 }
 return 0;
}
static int classify_node(const JgrModelNode *node,const double matrix[16],unsigned ordinal,int parent,void *user)
{
 (void)matrix;(void)ordinal;(void)parent;Classification *c=user;Stage *s=c->stage;
 for(unsigned i=0;i<s->nnodes;i++)if(s->nodes[i].node==node->address){Texlist *t=s->lists+s->nodes[i].list;for(unsigned k=0;k<t->count;k++)if(t->map[k]!=0xffff)c->textured=1;}
 if(node->model){int uv=has_uv(s,node->model);if(uv<0)return -1;c->uv|=uv;}
 return 0;
}
/* Match the stage scanner's candidate filter while retaining cycle/budget
 * checks. Object banks use the stricter ordinary model walk instead. */
static int city_walk(Stage *s,uint32_t root,const double matrix[16],JgrModelVisit visit,void *user)
{
 typedef struct{uint32_t address;int parent;double matrix[16];} Pending;
 Pending *stack=malloc(128*sizeof *stack);uint32_t *seen=malloc(4096*sizeof *seen);unsigned sp=0,n=0;int result=-1;
 if(!stack||!seen)goto done;
 stack[sp].address=root;stack[sp].parent=-1;memcpy(stack[sp++].matrix,matrix,128);
 while(sp){Pending p=stack[--sp];JgrModelNode node;double local[16],world[16];
  if(!dataptr(s,p.address)||!(s->node_flags[(p.address-SB)/4]&1))continue;
  if(n>=4096||jgr_model_node(s->disc,&s->source,SB,p.address,&node))goto done;
  for(unsigned i=0;i<n;i++)if(seen[i]==p.address)goto done;
  seen[n]=p.address;jgr_model_node_matrix(local,&node,0);jgr_model_multiply(world,p.matrix,local);
  if(visit(&node,world,n,p.parent,user))goto done;
  if(node.sibling){if(sp>=128)goto done;stack[sp].address=node.sibling;stack[sp].parent=p.parent;memcpy(stack[sp++].matrix,p.matrix,128);}
  if(node.child){if(sp>=128)goto done;stack[sp].address=node.child;stack[sp].parent=n;memcpy(stack[sp++].matrix,world,128);}n++;
 }
 result=0;
 done:free(stack);free(seen);return result;
}
static int add_owner(Stage *s,const Placement *p)
{
 if(s->nowners>=65534)return -1;
 if(s->nowners==s->owner_capacity){unsigned n=s->owner_capacity?s->owner_capacity*2:1024;Owner *r=realloc(s->owners,n*sizeof *r);if(!r)return -1;s->owners=r;s->owner_capacity=n;}
 Owner *r=s->owners+s->nowners;memset(r,0,sizeof *r);r->place=*p;r->near_first=s->objects;s->record=++s->nowners;return 0;
}
static int city_node_component_valid(uint32_t position,uint32_t scale)
{
 /* Discovery reads arbitrary words, including NaN sentinels. Keep rejected
  * candidates out of the PSP FPU. Positive IEEE-754 encodings are ordered;
  * 0x38d1b717 is the last binary32 value below the original double 1e-4
  * bound, while 1e6 and 1e3 are exactly representable. */
 return (position&0x7fffffffu)<0x49742400u &&
        scale>0x38d1b717u && scale<0x447a0000u;
}
static int city_geometry(Stage *s)
{
 static const unsigned sectors[7]={0,63,114,46,0,71,81};
 Placements p={0},groups={0};int result=-1;unsigned words_=s->source.size/4;s->node_flags=calloc(words_,1);if(!s->node_flags)return -1;
 if(s->stage==1&&jgr_model_baked_light(s->disc,&s->light))return -1;
 for(uint32_t at=SB;valid_data(s,at,53);at+=4) {
  uint32_t ev=dw(s,at),model=dw(s,at+4),child=dw(s,at+44),sib=dw(s,at+48);
  if(ev>0xffff||(!model&&!child&&!sib)||(model&&!dataptr(s,model))||(child&&!dataptr(s,child))||(sib&&!dataptr(s,sib)))continue;
  int valid=1;for(unsigned k=0;k<3;k++)valid&=city_node_component_valid(dw(s,at+8+4*k),dw(s,at+32+4*k));
  if(valid)s->node_flags[(at-SB)/4]|=1;
 }
 for(unsigned i=0;i<words_;i++)if(s->node_flags[i]&1){uint32_t children[2]={dw(s,SB+i*4+44),dw(s,SB+i*4+48)};for(unsigned k=0;k<2;k++)if(dataptr(s,children[k]))s->node_flags[(children[k]-SB)/4]|=2;}
 for(uint32_t at=SB;valid_data(s,at,257);at+=4)if(dw(s,at)>=1&&dw(s,at)<=3){uint32_t root=dw(s,at+252);if(dataptr(s,root)&&s->node_flags[(root-SB)/4]&1)s->node_flags[(root-SB)/4]|=4;}
 for(unsigned tag=0;tag<4;tag++){const Category *cat=&categories[s->stage][tag];if(cat->table&&placements(s,&p,cat,sectors[s->stage],tag,0))goto done;}
 if(s->stage==2||s->stage==3||s->stage==5){Category cat={0,0,0,52,0};cat.table=categories[s->stage][1].table;cat.near=s->stage==2?0x8c109bf8:s->stage==3?0x8c1bbca8:0x8c203d94;if(placements(s,&groups,&cat,sectors[s->stage],4,1))goto done;}
 if(s->stage==1)for(unsigned sector=0;sector<63;sector++) {
  unsigned special=0;for(unsigned i=0;i<4;i++)special|=ew(s,0x8c106384+i*4)==sector;
  uint32_t at=ew(s,0x8c106090+sector*4);if(!at)continue;unsigned guard=0;
  while(valid_data(s,at,40)) {int32_t kind=(int32_t)dw(s,at);if(kind< -1)break;if(++guard>10000)goto done;
   if(kind>=0&&(special||(kind>=0x2e&&kind<=0x34))){uint32_t record=ew(s,0x8c10748c+kind*4);if(record&&add_placement(s,&groups,dw(s,record+4),at,4,kind,40,sector))goto done;}at+=40;
  }
 }
 for(uint32_t at=EB;valid_code(s,at,4);) {
  if(!dataptr(s,ew(s,at))){at+=4;continue;}uint32_t start=at;while(valid_code(s,at,4)&&dataptr(s,ew(s,at)))at+=4;
  if(at-start>=32)for(uint32_t q=start;q<at;q+=4)s->node_flags[(ew(s,q)-SB)/4]|=32;
 }
 double identity[16];jgr_model_identity(identity);
 for(unsigned i=0;i<words_;i++) {
  unsigned flags=s->node_flags[i];if(!(flags&1)||(flags&2)||(flags&4)||(!(flags&8)&&(flags&(16|32))))continue;
  uint32_t root=SB+i*4;s->root_list=MAX_LIST;for(unsigned k=0;k<s->nnodes;k++)if(s->nodes[k].node==root)s->root_list=s->nodes[k].list;
  s->ground=!!(flags&64);s->post=NULL;s->lighting=NULL;s->root=root;s->origin_kind=!!(flags&8);s->record=0;
  if(flags&8) {
   for(unsigned k=0;k<p.count;k++)if(p.rows[k].root==root){double matrix[16],post[16],light[16];
    if(s->final_city&&add_owner(s,p.rows+k))goto done;
    placement_matrices(s,p.rows+k,matrix,post,light);if(city_walk(s,root,matrix,visit,s))goto done;
    if(s->final_city){Owner *r=s->owners+s->nowners-1;r->near_count=s->objects-r->near_first;if(!r->near_count)s->nowners--;}
   }
  } else {
   Classification c={s,0,0};if(city_walk(s,root,identity,classify_node,&c))goto done;
   if(c.uv&&c.textured&&city_walk(s,root,identity,visit,s))goto done;
  }
 }
 s->ground=0;s->post=NULL;s->lighting=NULL;s->root_list=MAX_LIST;s->record=0;s->origin_kind=2;
 for(unsigned i=0;i<groups.count;i++){
  /* Group tables also include non-chunk models, omitted by the existing
   * stage format. Only a valid CNK vertex-list header selects this decoder. */
  if(!group_chunk_model(s,groups.rows[i].root))continue;
  s->root=groups.rows[i].root;JgrModelNode n;memset(&n,0,sizeof n);n.model=groups.rows[i].root;double matrix[16];placed(matrix,groups.rows+i,1,0);if(visit(&n,matrix,0,-1,s))goto done;}
 result=0;
 done:free(p.rows);free(groups.rows);return result;
}

#include "portable_stage_lod.inc"

static int tunnel_output(Tunnel *tunnel)
{
 unsigned char header[48],record[16];
 if(tunnel->count!=26||tunnel->total!=48||fseek(tunnel->geometry,0,SEEK_SET)||fread(header,1,sizeof header,tunnel->geometry)!=sizeof header||memcmp(header,"JGS1",4)||word(header+12)<tunnel->batches)return -1;
 for(unsigned i=0;i<tunnel->count;i++){
  uint32_t batch=word(header+32)+tunnel->indices[i]*16;
  if(fseek(tunnel->geometry,batch,SEEK_SET)||fread(record,1,16,tunnel->geometry)!=16||(record[12]|(unsigned)record[13]<<8)!=tunnel->texture)return -1;
  unsigned char index[2]={(unsigned char)tunnel->indices[i],(unsigned char)(tunnel->indices[i]>>8)};
  if(tunnel->output&&fwrite(index,1,2,tunnel->output)!=2)return -1;
 }
 return 0;
}

static int convert_stage(JgrDisc *disc,unsigned stage,const char *directory,FILE *out,int city,FILE *ownership,JgrStageProp *props,unsigned prop_count,FILE *keys,Tunnel *tunnel)
{
 static const uint32_t specs[7][4]={{0},{18,0x8c107860,0x8c1078a8,0x8c1078f0},{8,0x8cc91d70,0x8cc93010,0x8c1ad900},{13,0x8c1bc7e4,0x8c1bc818,0x8c1c7428},{0},{17,0x8c2042a8,0x8c2042ec,0x8c20a7c8},{17,0x8c20b7f4,0x8c20b838,0x8c219ee0}};
 JgrScratchPool *scratch=NULL;JgrDiscView source_view={0};
 Stage s;memset(&s,0,sizeof s);s.disc=disc;s.stage=stage;s.city=(unsigned)city;s.final_city=city==2;s.tunnel=tunnel;int result=-1;char path[1024],source[80];
 if(stage>6||!specs[stage][0]||!directory||!out)return -1;
 JgrStageStatsScope stats_scope;jgr_stage_stats_begin(&stats_scope,stage,(unsigned)city);
 for(unsigned i=0;i<3;i++){s.low[i]=1e9;s.high[i]=-1e9;}
 snprintf(source,sizeof source,"decompile/afs/STAGE%u/000_st%udat.bin",stage,stage);
 if(jgr_disc_source(disc,source,&s.source)||jgr_disc_find(disc,"1ST_READ.BIN",&s.exe)||s.source.size>8*1024*1024||s.exe.size>4*1024*1024)goto done;
 s.code=malloc(s.exe.size);s.data=malloc(s.source.size);s.packs=calloc(128,sizeof *s.packs);s.lists=calloc(MAX_LIST,sizeof *s.lists);s.nodes=calloc(MAX_NODE,sizeof *s.nodes);s.used=calloc(MAX_TEX+1,1);s.remap=malloc((MAX_TEX+1)*2);s.tex=calloc(MAX_TEX+1,sizeof *s.tex);s.model=jgr_model_cache_create();
 if(!s.code||!s.data||!s.packs||!s.lists||!s.nodes||!s.used||!s.remap||!s.tex||!s.model)goto done;
 memset(s.remap,255,(MAX_TEX+1)*2);
 if(jgr_disc_read(disc,&s.exe,0,s.code,s.exe.size)||jgr_disc_read(disc,&s.source,0,s.data,s.source.size)||textures(&s))goto done;
 if(jgr_disc_view_begin(disc,&source_view,&s.source,s.data,s.source.size))goto done;
 if(tunnel){
  tunnel->valid=1;
  unsigned found=0;for(unsigned p=0;s.tunnel&&p<s.npacks;p++)for(unsigned i=0;s.tunnel&&i<s.packs[p].count;i++){
   unsigned char gbix[12];if(jgr_disc_read(disc,&s.packs[p].source,s.packs[p].offset[i],gbix,sizeof gbix)){
    if(!tunnel->optional)goto done;
    tunnel->valid=0;s.tunnel=NULL;break;
   }
   if(word(gbix+8)==20087){tunnel->atlas=s.packs[p].first+i;found++;}
  }
  if(stage!=2||found!=1){if(!tunnel->optional)goto done;tunnel->valid=0;s.tunnel=NULL;}
 }
 scratch=jgr_scratch_begin();if(!scratch)goto done;
 for(unsigned i=0;i<7;i++) {
  if(i==4||(i==6&&!(s.final_city&&stage==2)))continue;
  /* The caller retains the ordering keys through packing. Build directly in
   * that stream so assembly needs no second full-size key bank or copy. */
  if(i==6&&keys){s.sections[i]=keys;continue;}
  int n=snprintf(path,sizeof path,"%s/jgs-setup-%u.tmp",directory,i);if(n<0||(size_t)n>=sizeof path)goto done;
  struct stat st;
#ifdef __PSP__
  int exists=stat(path,&st);
#else
  int exists=lstat(path,&st);
#endif
  if((!exists&&!S_ISREG(st.st_mode))||(exists&&errno!=ENOENT))goto done;
  s.sections[i]=jgr_scratch_open(scratch,path);if(!s.sections[i])goto done;
 }
 jgr_stage_stats_phase(JGR_STAGE_PHASE_GEOMETRY);
 if(city==3||city==4){
  if(stage==1&&city==3&&jgr_model_baked_light(disc,&s.light))goto done;
  s.force_list=1;
  if(prop_count>32768||(!props&&prop_count))goto done;
  for(unsigned i=0;i<prop_count;i++){
   int list=list_id(&s,props[i].texlist,0);if(list<0)goto done;s.root_list=list;
   props[i].first_object=s.objects;
   if(props[i].root){if(source_walk(&s,props[i].root,props[i].matrix))goto done;}
   else if(props[i].mesh){JgrModelNode n;memset(&n,0,sizeof n);n.model=props[i].mesh;if(visit(&n,props[i].matrix,0,-1,&s))goto done;}
   else goto done;
   props[i].object_count=s.objects-props[i].first_object;
  }
 }else if(city){if(city_geometry(&s))goto done;}else for(unsigned lod=0;lod<2;lod++)for(unsigned i=0;i<specs[stage][0];i++) {
  uint32_t root=stage==2?specs[stage][lod+1]:ew(&s,specs[stage][lod+1]+4*i),list=stage==2?specs[stage][3]:ew(&s,specs[stage][3]+4*i);
  int lid=list_id(&s,list,0);s.root_list=lid<0?MAX_LIST:(unsigned)lid;
  unsigned first=s.objects,batch=s.batches;double identity[16];jgr_model_identity(identity);
  if(valid_data(&s,root,52) && jgr_model_walk(disc,&s.source,SB,root,identity,0,0,visit,&s))goto done;
  if(merge_object(&s,first,batch))goto done;
  if(stage==5 && i==16){long saved=ftell(s.sections[1]);for(unsigned b=batch;b<s.batches;b++){unsigned char r[16];if(fseek(s.sections[1],b*16,SEEK_SET)||fread(r,1,16,s.sections[1])!=16)goto done;r[14]&=~16;if(fseek(s.sections[1],b*16,SEEK_SET)||fwrite(r,1,16,s.sections[1])!=16)goto done;}if(fseek(s.sections[1],saved,SEEK_SET))goto done;}
 }
 /* The object stream can retain overwritten component records beyond its
  * logical end; final assembly copies only objects*32 bytes. */
 if(fflush(s.sections[0]))goto done;
 if(s.final_city&&city_lod(&s))goto done;
 (void)jgr_disc_view_end(&source_view);free(s.data);s.data=NULL;
 result=finish(&s,out);if(result)stage_failed(stage,JGR_STAGE_FAILURE_FINISH,0,NULL,out);
 if(!result&&(city==3||city==4))for(unsigned i=0;i<prop_count;i++){
  int list=list_id(&s,props[i].texlist,0);props[i].texture0=0xffff;
  if(list>=0&&s.lists[list].count){unsigned t=s.lists[list].map[0];if(t<=s.ntex)props[i].texture0=s.remap[t];}
 }
 if(!result&&s.final_city){result=write_ownership(&s,ownership);if(result)stage_failed(stage,JGR_STAGE_FAILURE_OWNERSHIP,0,NULL,ownership);}
 if(!result&&s.tunnel){
  tunnel->texture=s.remap[tunnel->atlas];tunnel->batches=s.batches;
  if(tunnel_output(tunnel)){
   if(tunnel->optional){tunnel->valid=0;clearerr(tunnel->geometry);}
   else result=-1;
  }
 }
 done:{int failure_errno=errno;(void)jgr_disc_view_end(&source_view);jgr_stage_stats_phase(JGR_STAGE_PHASE_ASSEMBLY);for(unsigned i=0;i<7;i++)if(s.sections[i]&&!(i==6&&s.sections[i]==keys)){if(fclose(s.sections[i])&&!result){result=-1;failure_errno=errno;stage_failed(stage,JGR_STAGE_FAILURE_SECTION_CLOSE,i,NULL,NULL);}snprintf(path,sizeof path,"%s/jgs-setup-%u.tmp",directory,i);remove(path);}
 if(scratch&&jgr_scratch_end()&&!result){result=-1;failure_errno=errno;stage_failed(stage,JGR_STAGE_FAILURE_SCRATCH_END,0,NULL,NULL);}
 for(unsigned i=0;i<s.npacks;i++)free(s.packs[i].offset);
 jgr_model_cache_free(s.model);free(s.code);free(s.data);free(s.packs);free(s.lists);free(s.nodes);free(s.used);free(s.remap);free(s.tex);free(s.points);free(s.node_flags);free(s.owners);free(s.origins);
#if JGR_STAGE_STATS
 stage_stats.builds++;stage_stats.objects+=s.objects;stage_stats.batches+=s.batches;stage_stats.vertices+=s.vertices;stage_stats.triangles+=s.indices/3;stage_stats.textures+=s.textures;
#endif
 jgr_stage_stats_end(&stats_scope,result);if(result)errno=failure_errno;return result;}
}

int jgr_stage_objects(JgrDisc *d,unsigned stage,const char *cache,FILE *out)
{return convert_stage(d,stage,cache,out,0,NULL,NULL,0,NULL,NULL);}
int jgr_stage_base(JgrDisc *d,unsigned stage,const char *cache,FILE *out)
{return convert_stage(d,stage,cache,out,1,NULL,NULL,0,NULL,NULL);}

int jgr_stage_render(JgrDisc *d,unsigned stage,const char *cache,FILE *out,FILE *owners)
{return convert_stage(d,stage,cache,out,2,owners,NULL,0,NULL,NULL);}

int jgr_stage_prop_bank(JgrDisc *d,unsigned stage,const char *cache,JgrStageProp *props,unsigned count,FILE *out)
{return convert_stage(d,stage,cache,out,3,NULL,props,count,NULL,NULL);}

int jgr_stage_light_bank(JgrDisc *d,unsigned stage,const char *cache,JgrStageProp *props,unsigned count,FILE *out)
{return convert_stage(d,stage,cache,out,4,NULL,props,count,NULL,NULL);}

/* Fully regenerate shared city geometry and sector ownership. The caller adds
 * the original rigid-prop instance layer before publishing the final assets. */
int jgr_stage_city_geometry(JgrDisc *d,unsigned stage,const char *cache,FILE *out,FILE *owners)
{
 FILE *raw=NULL,*keys=NULL;char raw_path[1024],key_path[1024];int result=-1;
 Tunnel tunnel={0};int capture=city_capture.function&&city_capture.disc==d&&(stage==1||stage==2);
 const void *key_view=NULL;size_t key_size=0;
 if(!cache||!out||!owners||stage>6||!sector_counts[stage])return -1;
 if(snprintf(raw_path,sizeof raw_path,"%s/jgs-city-raw.tmp",cache)>=(int)sizeof raw_path||snprintf(key_path,sizeof key_path,"%s/jgs-city-keys.tmp",cache)>=(int)sizeof key_path)return -1;
 const char *paths[2]={raw_path,key_path};
 for(unsigned i=0;i<2;i++){struct stat st;
#ifdef __PSP__
  int exists=stat(paths[i],&st);
#else
  int exists=lstat(paths[i],&st);
#endif
  if((!exists&&!S_ISREG(st.st_mode))||(exists&&errno!=ENOENT))return -1;
 }
 JgrScratchPool *scratch=jgr_scratch_begin();if(!scratch)return -1;
 JgrStageStatsScope stats_scope;jgr_stage_stats_begin(&stats_scope,stage,JGR_STAGE_JOB_PACK);
 raw=jgr_scratch_open(scratch,raw_path);if(stage==2)keys=jgr_scratch_open(scratch,key_path);if(!raw||(stage==2&&!keys))goto done;
 tunnel.geometry=raw;tunnel.optional=1;
 if(convert_stage(d,stage,cache,raw,2,owners,NULL,0,keys,capture&&stage==2?&tunnel:NULL))goto done;
 if(fflush(raw)){stage_failed(stage,JGR_STAGE_FAILURE_RAW_FLUSH,0,NULL,raw);goto done;}
 if(keys&&fflush(keys)){stage_failed(stage,JGR_STAGE_FAILURE_KEY_FLUSH,6,NULL,keys);goto done;}
 jgr_stage_stats_phase(JGR_STAGE_PHASE_POSTPROCESS);
 if(keys)key_view=jgr_scratch_view(scratch,keys,&key_size);
 if(jgr_stage_pack_view(raw,keys,out,stage==2,key_view,key_size)){stage_failed(stage,JGR_STAGE_FAILURE_PACK,0,raw,out);goto done;}
 if(fflush(out)){stage_failed(stage,JGR_STAGE_FAILURE_OUTPUT_FLUSH,0,NULL,out);goto done;}
 if(jgr_stage_ownership_hash(out,owners)){stage_failed(stage,JGR_STAGE_FAILURE_OWNERSHIP_HASH,0,out,owners);goto done;}
 if(capture&&stage==2&&tunnel.valid){tunnel.geometry=out;if(tunnel_output(&tunnel)){tunnel.valid=0;clearerr(out);}}
 result=0;
 done:{int failure_errno=errno;if(key_view)jgr_scratch_unview(scratch,keys);if(raw){if(fclose(raw)&&!result){result=-1;failure_errno=errno;}remove(raw_path);}if(keys){if(fclose(keys)&&!result){result=-1;failure_errno=errno;}remove(key_path);}if(jgr_scratch_end()&&!result){result=-1;failure_errno=errno;}
 /* Release geometry scratch before optional proxy extraction needs memory. */
 if(!result&&capture&&(stage==1||tunnel.valid)){
  long saved=ftell(out);int had_error=ferror(out);
  city_capture.function(d,stage,out,stage==2?tunnel.indices:NULL,tunnel.count,city_capture.user);
  if(!had_error)clearerr(out);
  if(saved<0||fseek(out,saved,SEEK_SET)){result=-1;failure_errno=errno;}
 }
 jgr_stage_stats_end(&stats_scope,result);if(result)errno=failure_errno;return result;}
}

int jgr_stage_tunnel_materials(JgrDisc *disc,const char *cache,FILE *geometry,FILE *output)
{
 FILE *raw=NULL,*owners=NULL;char path[2][1024];int result=-1;Tunnel tunnel={0};
 if(!disc||!cache||!geometry||!output)return -1;
 tunnel.geometry=geometry;tunnel.output=output;
 for(unsigned i=0;i<2;i++){
  if(snprintf(path[i],sizeof path[i],"%s/tunnel-setup-%u.tmp",cache,i)>=(int)sizeof path[i])return -1;
  struct stat st;
#ifdef __PSP__
  int exists=stat(path[i],&st);
#else
  int exists=lstat(path[i],&st);
#endif
  if((!exists&&!S_ISREG(st.st_mode))||(exists&&errno!=ENOENT))return -1;
 }
 JgrScratchPool *scratch=jgr_scratch_begin();if(!scratch)return -1;
 raw=jgr_scratch_open(scratch,path[0]);owners=jgr_scratch_open(scratch,path[1]);
 if(raw&&owners)result=convert_stage(disc,2,cache,raw,2,owners,NULL,0,NULL,&tunnel);
 int failure_errno=errno;
 if(raw){if(fclose(raw)&&!result){result=-1;failure_errno=errno;}remove(path[0]);}if(owners){if(fclose(owners)&&!result){result=-1;failure_errno=errno;}remove(path[1]);}if(jgr_scratch_end()&&!result){result=-1;failure_errno=errno;}if(result)errno=failure_errno;return result;
}
