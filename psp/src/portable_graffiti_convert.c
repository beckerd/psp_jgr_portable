/* Original attached/wall/final graffiti geometry and reveal graphs. No model
 * positions, material values, reveal words or descriptor payloads are compiled. */
#include "portable_graffiti_convert.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define MAX_NODES 4096u
#define MAX_VERTICES 262144u
#define MB 0x8cde6000u
#define SB 0x8cb00000u
#define EB 0x8c010000u
static unsigned u32(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static unsigned u16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static int s16(const unsigned char *p){unsigned n=u16(p);return n>=32768?(int)n-65536:(int)n;}
static void w32(unsigned char *p,unsigned n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8));}
static void w16(unsigned char *p,unsigned n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);}
static void real(unsigned char *p,double n){float f=(float)n;unsigned bits;memcpy(&bits,&f,4);w32(p,bits);}
static int write(FILE *f,const void *p,unsigned n){return fwrite(p,1,n,f)==n?0:-1;}
static int put(FILE *f,unsigned n){unsigned char b[4];w32(b,n);return write(f,b,4);}
typedef struct {unsigned char xyz[12];unsigned reveal,owner,valid;} Position;
typedef struct {unsigned slot;int u,v;unsigned divisor;} Corner;
typedef struct {unsigned node,graph;int parent;} Pending;
typedef struct {
 JgrDisc *disc;JgrDiscFile file;unsigned base,window_at,window_size;unsigned char window[4096];
 Position *cache;unsigned cache_size;unsigned char *bones,*vertices,*materials;unsigned nb,nv,bcap,vcap,minimum,first_flags;
 unsigned seen[MAX_NODES],nseen;Pending pending[128];unsigned depth;int attached;
} Graph;
static int read_at(Graph *g,unsigned a,void *out,unsigned n)
{
 if(a<g->base||a-g->base>g->file.size||n>g->file.size-(a-g->base))return -1;
 unsigned at=a-g->base;
 if(n>sizeof g->window)return jgr_disc_read(g->disc,&g->file,at,out,n);
 if(at<g->window_at||at-g->window_at>g->window_size||n>g->window_size-(at-g->window_at)){
  g->window_at=at;g->window_size=g->file.size-at;if(g->window_size>sizeof g->window)g->window_size=sizeof g->window;
  if(jgr_disc_read(g->disc,&g->file,at,g->window,g->window_size))return -1;
 }
 memcpy(out,g->window+(at-g->window_at),n);return 0;
}
static int word(Graph *g,unsigned a,unsigned *v){unsigned char b[4];if(read_at(g,a,b,4))return -1;*v=u32(b);return 0;}
static int resize_cache(Graph *g,unsigned n)
{
 if(n>65536)return -1;
 if(n<=g->cache_size)return 0;
 unsigned cap=g->cache_size?g->cache_size:64;while(cap<n)cap*=2;
 Position *p=realloc(g->cache,cap*sizeof *p);if(!p)return -1;memset(p+g->cache_size,0,(cap-g->cache_size)*sizeof *p);g->cache=p;g->cache_size=cap;return 0;
}
static int vertices(Graph *g,unsigned at,unsigned graph,unsigned owner)
{
 static const unsigned char stride[19]={16,32,12,16,16,16,16,16,16,24,28,28,28,28,28,28,16,16,16};
 unsigned char b[32];unsigned first,reveal;if(read_at(g,at,b,8)||((b[0]&63)!=35&&(b[0]&63)!=42)||word(g,graph,&reveal))return -1;first=u16(b+4);
 for(unsigned guard=0;guard<65536;guard++){
  if(read_at(g,at,b,4))return -1;
 unsigned type=b[0];if(type==255)return 0;if(type==0){at+=4;continue;}
  if(type<32||type>50||read_at(g,at,b,8))return -1;
 unsigned bytes=4+u16(b+2)*4,start=u16(b+4),count=u16(b+6),step=stride[type-32];
  if(bytes<8||count>(bytes-8)/step||start<first||resize_cache(g,start+count)||at-g->base>g->file.size||bytes>g->file.size-(at-g->base))return -1;
  for(unsigned i=0;i<count;i++){
   Position *p=g->cache+start+i;if(read_at(g,at+8+i*step,p->xyz,12)||word(g,reveal+4*(start+i-first),&p->reveal))return -1;
   for(unsigned k=0;k<3;k++){unsigned bits=u32(p->xyz+4*k);float f;memcpy(&f,&bits,4);if(!isfinite(f))return -1;}
   p->owner=owner;p->valid=1;
  }at+=bytes;
 }return -1;
}
static int triangle(Graph *g,const Corner *corners,unsigned texture,unsigned flags,unsigned diffuse)
{
 if(g->attached&&(texture!=0||diffuse!=0xffffffffu))return -1;
 if(g->nv>MAX_VERTICES-3)return -1;
 if(g->nv+3>g->vcap){unsigned n=g->vcap?g->vcap*2:192;if(n>MAX_VERTICES)n=MAX_VERTICES;unsigned char *v=realloc(g->vertices,n*28);if(!v)return -1;g->vertices=v;unsigned char *m=realloc(g->materials,((n+2)/3)*12);if(!m)return -1;g->materials=m;g->vcap=n;}
 if(!g->nv){g->minimum=flags;g->first_flags=flags;}else{if(g->attached&&flags!=g->first_flags)return -1;
 if(flags<g->minimum)g->minimum=flags;}
 unsigned char *m=g->materials+(g->nv/3)*12;w32(m,texture);w32(m+4,flags);w32(m+8,diffuse);
 for(unsigned i=0;i<3;i++){
  const Corner *c=corners+i;if(c->slot>=g->cache_size||!g->cache[c->slot].valid)return -1;const Position *p=g->cache+c->slot;unsigned char *v=g->vertices+g->nv++*28;
  real(v,(double)c->u/c->divisor);real(v+4,(double)c->v/c->divisor);w32(v+8,p->reveal);memcpy(v+12,p->xyz,12);w32(v+24,p->owner);
 }return 0;
}
static int polygons(Graph *g,unsigned at)
{
 static const unsigned char extra[12]={0,2,2,3,5,5,2,4,4,1,3,3};unsigned texture=0,diffuse=0xffffffffu,blend=0x25;unsigned char b[8];
 for(unsigned guard=0;guard<262144;guard++){
  if(read_at(g,at,b,2))return -1;
 unsigned type=b[0],flags=b[1];if(type==255)return 0;
  if(type<=5){if(type==1)blend=flags;at+=2;continue;}
  if(type==8||type==9){if(read_at(g,at,b,4))return -1;texture=u16(b+2)&8191;at+=4;continue;}
  if((type>=16&&type<=31)||(type>=56&&type<=58)){
   if(read_at(g,at,b,4))return -1;
 unsigned bytes=4+2*u16(b+2);if(bytes>g->file.size-(at-g->base))return -1;
   if(type<=31){blend=flags;if(type&1){if(bytes<8||word(g,at+4,&diffuse))return -1;}}at+=bytes;continue;
  }
  if(type<64||type>75||read_at(g,at,b,6))return -1;
 unsigned bytes=4+2*u16(b+2),n=u16(b+4),end=at+bytes,p=at+6;
  if(bytes<6||bytes>g->file.size-(at-g->base))return -1;
  unsigned textured=type==65||type==66||type==68||type==69||type==71||type==72;
  unsigned divisor=type==65||type==68||type==71||type==73?255:type==66||type==69||type==72||type==74?1023:1;
  unsigned material_flags=flags|((16|((blend>>3&7)<<5)|((blend&7)<<8))<<8);
  for(unsigned strip=0;strip<(n&16383);strip++){
   if(p>end||end-p<2||read_at(g,p,b,2))return -1;
 int signed_count=s16(b);unsigned count=signed_count<0?(unsigned)-signed_count:(unsigned)signed_count,step=2+extra[type-64]*2;p+=2;
   if(count>(end-p)/step)return -1;
 Corner prev[2]={{0}};
   for(unsigned k=0;k<count;k++){
    if(read_at(g,p,b,textured?6:2))return -1;
 p+=step;Corner c={u16(b),textured?s16(b+2):0,textured?s16(b+4):0,divisor};
    if(k>=2){Corner tri[3]={prev[0],prev[1],c};if(((k-2)&1)^(signed_count<0)){Corner t=tri[0];tri[0]=tri[1];tri[1]=t;}if(triangle(g,tri,textured?texture:65535,material_flags,diffuse))return -1;}
    prev[0]=prev[1];prev[1]=c;
   }
   unsigned skip=(count>2?count-2:0)*(n>>14)*2;if(skip>end-p)return -1;
 p+=skip;
  }at=end;
 }return -1;
}
static int mesh(Graph *g,unsigned descriptor)
{
 unsigned root,graph;if(word(g,descriptor+0xfc,&root)||word(g,descriptor+0x100,&graph))return -1;
 g->nb=g->nv=g->nseen=g->depth=0;if(g->cache_size)memset(g->cache,0,g->cache_size*sizeof *g->cache);
 if(root)g->pending[g->depth++]=(Pending){root,graph,-1};
 while(g->depth){
  Pending p=g->pending[--g->depth];unsigned char b[52],gr[12];if(read_at(g,p.node,b,52)||read_at(g,p.graph,gr,12)||g->nseen>=MAX_NODES)return -1;
  for(unsigned i=0;i<g->nseen;i++)if(g->seen[i]==p.node)return -1;
 g->seen[g->nseen++]=p.node;
  if(g->nb==g->bcap){unsigned n=g->bcap?g->bcap*2:16;unsigned char *q=realloc(g->bones,n*48);if(!q)return -1;g->bones=q;g->bcap=n;}
  unsigned bone=g->nb++;unsigned char *dest=g->bones+bone*48;memset(dest,0,48);w16(dest,(unsigned)p.parent);if(u32(b)>65535)return -1;w16(dest+2,u32(b));memcpy(dest+4,b+8,36);
  unsigned model=u32(b+4);if(model){unsigned vp,pp;if(word(g,model,&vp)||word(g,model+4,&pp)||vertices(g,vp,p.graph,bone)||polygons(g,pp))return -1;}
  unsigned child=u32(b+44),sibling=u32(b+48);if(g->depth+!!child+!!sibling>128)return -1;
  if(sibling)g->pending[g->depth++]=(Pending){sibling,u32(gr+8),p.parent};
 if(child)g->pending[g->depth++]=(Pending){child,u32(gr+4),(int)bone};
 }
 return !g->nv?-1:0;
}
static int emit(Graph *g,unsigned descriptor,unsigned bank,unsigned chain,unsigned index,unsigned form,FILE *out)
{
 unsigned char desc[0x104];if(read_at(g,descriptor,desc,sizeof desc)||mesh(g,descriptor))return -1;
 unsigned kind=u32(desc),anchors=u32(desc+0xf0),volumes=u32(desc+0xf8);
 if(anchors>7||volumes>5||(form==0&&(kind>1||anchors||volumes))||(form==1&&(kind<1||kind>3)))return -1;
 unsigned bytes=260+g->nb*48+g->nv*28+(form?g->nv*4:0);if(form==1){if(put(out,bytes+4))return -1;}else if(form==2){if(put(out,bytes+16+anchors*32+volumes*176)||put(out,u32(desc+0xe8))||put(out,anchors)||put(out,volumes))return -1;}
 unsigned header[8]={bank,chain,index,descriptor,kind,g->nv,g->minimum,g->nb};for(unsigned i=0;i<8;i++)if(put(out,header[i]))return -1;
 if(write(out,desc+4,228)||write(out,g->bones,g->nb*48)||write(out,g->vertices,g->nv*28)||(form&&write(out,g->materials,g->nv*4)))return -1;
 if(form==2)for(unsigned k=0;k<2;k++){unsigned n=k?volumes:anchors,at=u32(desc+(k?0xf4:0xec)),stride=k?176:32;unsigned char b[176];for(unsigned i=0;i<n;i++)if(read_at(g,at+i*stride,b,stride)||write(out,b,stride))return -1;}
 return 0;
}
static int count_header(FILE *out,const char *magic){return write(out,magic,4)||put(out,0)?-1:0;}
static int finish(FILE *out,unsigned count){long end=ftell(out);return end<0||fseek(out,4,SEEK_SET)||put(out,count)||fseek(out,end,SEEK_SET)?-1:0;}
int jgr_graffiti_convert(JgrDisc *d,const JgrDiscFile *f,unsigned op,unsigned selector,FILE *out)
{
 if(!d||!f||!out||op<84||op>87)return -1;
 Graph *g=calloc(1,sizeof *g);if(!g)return -1;g->disc=d;g->file=*f;g->attached=op==84||op==85;unsigned total=0;int result=-1;
 if(op==84){
  if(count_header(out,"GFO2"))goto done;
 g->base=MB;JgrDiscFile archive;if(jgr_disc_find(d,"JETRADIO/MISSIONSET.AFS",&archive))goto done;
  for(unsigned bank=0;bank<64;bank++){
   if(jgr_disc_afs(d,&archive,bank,&g->file))goto done;
 g->window_size=0;unsigned char tag[4];if(read_at(g,MB,tag,4))goto done;if(!memcmp(tag,"JMSb",4))continue;
   unsigned root=MB,seen[64],nseen=0;
   while(root){
    if(nseen>=64||read_at(g,root,tag,4)||memcmp(tag,"JMSc",4))goto done;
 for(unsigned i=0;i<nseen;i++)if(seen[i]==root)goto done;
 seen[nseen++]=root;
    unsigned list;if(word(g,root+0x1c,&list))goto done;
    for(unsigned index=0;list;list+=4,index++){unsigned a;if(index>=4096||word(g,list,&a))goto done;if(!a)break;if(emit(g,a,bank,nseen-1,index,0,out))goto done;total++;}
    if(word(g,root+0x50,&root))goto done;
   }
  }
 }else if(op==85){
  unsigned char tag[4];unsigned table=0,character=16,a,found=0;JgrDiscFile archive;
  if(selector>=16||jgr_disc_find(d,"JETRADIO/PLAYER.AFS",&archive))goto done;
  unsigned char archive_head[8];if(jgr_disc_read(d,&archive,0,archive_head,8))goto done;unsigned members=u32(archive_head+4);if(members>512)goto done;
  for(unsigned member=0;member<members;member++){
   if(jgr_disc_afs(d,&archive,member,&g->file))goto done;
   g->window_size=0;
   if(read_at(g,0,tag,4))goto done;
   if(memcmp(tag,"PLAY",4))continue;
   if(word(g,8,&table)||word(g,table,&character))goto done;
   if(character==selector){found=1;break;}
  }
  if(!found||word(g,table+0x74,&a)||count_header(out,"GFO2")||emit(g,a,63,7,character,0,out))goto done;
  total=1;
 }else if(op==86){
  unsigned table=selector==1?0x8cddf04c:selector==2?0x8cd9b4c8:selector==3?0x8cb21d58:selector==5?0x8cb0cdb0:selector==6?0x8cb09708:0;if(!table||count_header(out,"CWT1"))goto done;
 g->base=SB;
  for(unsigned area=0;area<6;area++){unsigned list;if(word(g,table+area*4,&list))goto done;
 for(unsigned index=0;list;list+=4,index++){unsigned a;if(index>=4096||word(g,list,&a))goto done;if(!a)break;if(emit(g,a,selector,area,index,1,out))goto done;total++;}}
 }else{
  JgrDiscFile exe;unsigned char b[4];unsigned tables[2];if(jgr_disc_find(d,"1ST_READ.BIN",&exe)||jgr_disc_read(d,&exe,0x8c1d0e54-EB,b,4))goto done;tables[0]=u32(b);if(jgr_disc_read(d,&exe,0x8c1d0ea0-EB,b,4)||count_header(out,"FGT1"))goto done;tables[1]=u32(b);g->base=SB;
  for(unsigned i=0;i<5;i++){unsigned a;if(word(g,tables[i==4]+(i<4?i*4:0),&a)||emit(g,a,4,0,i,2,out))goto done;total++;}
 }
 if(finish(out,total))goto done;
 result=0;
done:free(g->cache);free(g->bones);free(g->vertices);free(g->materials);free(g);return result;
}
