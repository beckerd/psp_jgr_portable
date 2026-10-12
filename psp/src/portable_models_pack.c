/* Deterministic shared-cast packing. Numeric selectors here describe the port's
 * public model IDs and historical container layout; all payloads are streamed
 * from the model bank just converted from the user's disc. */
#include "portable_models_pack.h"
#include "portable_crc32.h"
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define HASH_SIZE 131072u
#ifndef JGR_MODEL_SNAPSHOT_LIMIT
#define JGR_MODEL_SNAPSHOT_LIMIT (12u*1024u*1024u)
#endif
#define MAX_ENTRIES 65536u
#define SOURCE_CACHE 4096u

typedef struct {uint64_t hash;uint32_t size,alignment,offset;} Entry;
typedef struct {uint32_t source,size,alignment,output;} SourceEntry;
typedef struct {FILE *file;const unsigned char *data;unsigned size;} ReadSource;
typedef struct {FILE *file;ReadSource contents;ReadSource *immutable;uint32_t *hash;Entry *entries;SourceEntry *source_cache;unsigned count,capacity,bytes;} Store;
typedef struct {unsigned w,h,flags,pad,clut,pixels;uint64_t hash;} Texture;
static const unsigned far_pairs[][2]={
 {16,51},{17,52},{48,53},{18,54},{30,55},{32,56},{49,57},{31,58},
 {27,59},{28,45},{46,47},{33,60},{50,61},{38,62},{39,63},{40,64},
 {42,65},{43,66},{44,67}
};
static unsigned word(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static unsigned half(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static void w32(unsigned char *p,unsigned n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8));}
static void w16(unsigned char *p,unsigned n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);}
static int read_at(ReadSource *s,unsigned at,void *p,unsigned n)
{
 if(s->data){if(at>s->size || n>s->size-at)return -1;memcpy(p,s->data+at,n);return 0;}
 return fseek(s->file,at,SEEK_SET)||fread(p,1,n,s->file)!=n?-1:0;
}
static int write(FILE *f,const void *p,unsigned n){return fwrite(p,1,n,f)==n?0:-1;}
static int span(unsigned at,unsigned n,unsigned size){return at<=size && n<=size-at;}
static int pad(FILE *f,unsigned alignment)
{long at=ftell(f);if(at<0)return -1;while((unsigned long)at++&(alignment-1))if(fputc(0,f)==EOF)return -1;return 0;}
static int copy(ReadSource *from,unsigned at,unsigned size,FILE *to)
{unsigned char data[4096];while(size){unsigned n=size<sizeof data?size:sizeof data;if(read_at(from,at,data,n)||write(to,data,n))return -1;at+=n;size-=n;}return 0;}
static int hash_span(ReadSource *f,unsigned at,unsigned n,uint64_t *out)
{/* This is an internal lookup filter, never serialized. Keep full equality
   checks below, including when two different byte ranges share this CRC. */
 unsigned char data[4096];uint32_t hash=UINT32_MAX;while(n){unsigned count=n<sizeof data?n:sizeof data;if(read_at(f,at,data,count))return -1;hash=jgr_crc32_add(hash,data,count);at+=count;n-=count;}*out=hash;return 0;}
static int equal(ReadSource *a,unsigned ao,ReadSource *b,unsigned bo,unsigned n)
{unsigned char x[4096],y[4096];while(n){unsigned count=n<sizeof x?n:sizeof x;if(read_at(a,ao,x,count)||read_at(b,bo,y,count))return -1;if(memcmp(x,y,count))return 0;ao+=count;bo+=count;n-=count;}return 1;}
static int intern_uncached(Store *s,ReadSource *input,unsigned at,unsigned bytes,unsigned alignment,unsigned *out)
{
 uint64_t hash;if(hash_span(input,at,bytes,&hash))return -1;unsigned slot=((unsigned)hash^alignment)&(HASH_SIZE-1);
 while(s->hash[slot]){
  Entry *e=s->entries+s->hash[slot]-1;
  if(e->hash==hash && e->size==bytes && e->alignment==alignment){int same=equal(input,at,&s->contents,e->offset,bytes);if(same<0)return -1;if(same){*out=e->offset;return 0;}}
  slot=(slot+1)&(HASH_SIZE-1);
 }
 if(s->count>=MAX_ENTRIES || bytes>UINT32_MAX-s->bytes-16)return -1;
 if(s->count==s->capacity){unsigned n=s->capacity?s->capacity*2:256;Entry *p=realloc(s->entries,n*sizeof *p);if(!p)return -1;s->entries=p;s->capacity=n;}
 if(fseek(s->file,s->bytes,SEEK_SET)||pad(s->file,alignment))return -1;
 long pos=ftell(s->file);if(pos<0 || (unsigned long)pos>UINT32_MAX)return -1;
 *out=(unsigned)pos;if(copy(input,at,bytes,s->file))return -1;s->bytes=*out+bytes;
 s->entries[s->count]=(Entry){hash,bytes,alignment,*out};s->hash[slot]=++s->count;return 0;
}
static int intern(Store *s,ReadSource *input,unsigned at,unsigned bytes,unsigned alignment,unsigned *out)
{
 /* Only the completed input bank is immutable. Channel scratch is rewritten
    for each motion and must always take the ordinary content comparison. */
 SourceEntry *entry=NULL;
 if(input==s->immutable && s->source_cache){
  uint32_t key=at^(bytes*2654435761u)^alignment;key^=key>>16;key*=2246822519u;key^=key>>13;
  entry=s->source_cache+(key&(SOURCE_CACHE-1));
  if(entry->alignment==alignment && entry->source==at && entry->size==bytes){*out=entry->output;return 0;}
 }
 int result=intern_uncached(s,input,at,bytes,alignment,out);
 if(!result && entry)*entry=(SourceEntry){at,bytes,alignment,*out};
 return result;
}
static int texture_equal(ReadSource *f,const Texture *a,const Texture *b)
{
 if(a->w!=b->w || a->h!=b->h || a->flags!=b->flags || a->pad!=b->pad || a->hash!=b->hash)return 0;
 int same=equal(f,a->clut,f,b->clut,1024);if(same!=1)return same;return equal(f,a->pixels,f,b->pixels,a->w*a->h);
}
int jgr_models_pack_cast(FILE *input,FILE *output,FILE *scratch[10])
{return jgr_models_pack_cast_view(input,output,scratch,NULL,0);}
int jgr_models_pack_cast_view(FILE *input,FILE *output,FILE *scratch[10],const void *input_data,size_t input_bytes)
{
 unsigned char head[80],row[48],batch[16],mr[8];unsigned h[16],size,owner[68],first_motion[68],motion_count[68],directory[51]={0};
 unsigned char *snapshot=NULL,*channel_table=NULL;ReadSource bank_input={input,NULL,0},work[10];
 Texture *textures=NULL;unsigned *texture_ids=NULL,*unique=NULL;Store keys={0},channels={0};int result=-1;
 if(!input || !output || !scratch || fseek(input,0,SEEK_END))return -1;
 long total=ftell(input);
 if(total<80 || (unsigned long)total>UINT32_MAX || read_at(&bank_input,0,head,80) || memcmp(head,"JGC2",4))return -1;
 size=(unsigned)total;bank_input.size=size;
 if(input_data){if(input_bytes!=size)return -1;bank_input.data=input_data;}
 for(unsigned i=0;i<16;i++)h[i]=word(head+4+i*4);
 if(h[0]!=68 || h[1]>32767 || h[2]>65535 || h[3]>1000000 || h[4]>3000000 || h[5]>8192 || h[6]>8192)return -1;
 const unsigned stride[9]={48,48,16,28,2,16,8,1,1};
 for(unsigned i=0;i<9;i++){unsigned count=i<7?h[i==5?5:i==6?6:i]:0;if(count>UINT32_MAX/stride[i] || !span(h[7+i],count*stride[i],size))return -1;}
 for(unsigned i=0;i<10;i++){if(!scratch[i] || fseek(scratch[i],0,SEEK_SET))return -1;work[i]=(ReadSource){scratch[i],NULL,0};}
 /* The completed bank is immutable and the geometry builder has released its
  * working set. An optional bounded snapshot turns the many shared-pose and
  * key comparisons below into RAM reads; low-memory systems keep streaming. */
 channel_table=malloc(h[1]*24u);if(!channel_table)goto done;
 keys.file=scratch[7];keys.contents=work[7];channels.file=scratch[9];channels.contents=work[9];keys.hash=calloc(HASH_SIZE,sizeof *keys.hash);channels.hash=calloc(HASH_SIZE,sizeof *channels.hash);
 keys.immutable=&bank_input;keys.source_cache=calloc(SOURCE_CACHE,sizeof *keys.source_cache);
 textures=calloc(h[5],sizeof *textures);texture_ids=calloc(h[5],sizeof *texture_ids);unique=calloc(h[5],sizeof *unique);
 if(!keys.hash||!channels.hash||!textures||!texture_ids||!unique)goto done;
 if(!input_data && size<=JGR_MODEL_SNAPSHOT_LIMIT){
  /* Keep room for growing intern tables as well as caller-owned setup work.
   * A failed optional allocation must leave the ordinary streaming path. */
  /* An indirect volatile call keeps the compiler from deleting this
     * allocation/free pair: its occupied heap is the purpose of the guard. */
    int saved=errno;void *(*volatile reserve)(size_t)=malloc;
    void (*volatile release)(void *)=free;
    void *headroom=reserve(6u*1024u*1024u);
  if(headroom){snapshot=malloc(size);release(headroom);}
  if(snapshot){if(fseek(input,0,SEEK_SET)||fread(snapshot,1,size,input)!=size)goto done;bank_input.data=snapshot;}
  else errno=saved;
 }
 unsigned ntextures=0;
 for(unsigned i=0;i<h[5];i++){
  if(read_at(&bank_input,h[12]+i*16,batch,16))goto done;
  Texture *t=textures+i;
  t->w=half(batch);t->h=half(batch+2);t->flags=half(batch+4);t->pad=half(batch+6);t->clut=word(batch+8);t->pixels=word(batch+12);
  if(!t->w||!t->h||t->w>1024||t->h>1024||!span(t->clut,1024,size)||!span(t->pixels,t->w*t->h,size))goto done;
  uint64_t a,b;if(hash_span(&bank_input,t->clut,1024,&a)||hash_span(&bank_input,t->pixels,t->w*t->h,&b))goto done;t->hash=a^b;
  unsigned j;for(j=0;j<ntextures;j++){int same=texture_equal(&bank_input,t,textures+unique[j]);if(same<0)goto done;if(same)break;}
  if(j==ntextures)unique[ntextures++]=i;
  texture_ids[i]=j;
 }
 /* The old shared container kept the unused 256-square van atlas between
  * early enemy banks; car/beacon repair appended its 128-square car atlas.
  * Preserve that port container convention without retaining old payloads. */
 if(ntextures!=44)goto done;
 unsigned order[44],reorder[44];
 for(unsigned i=0;i<44;i++){unsigned to=i;if(i==23)to=24;else if(i==24)to=34;else if(i==25)to=43;else if(i>=26&&i<=34)to=i-1;else if(i==36)to=23;else if(i>=37)to=i-1;reorder[i]=to;order[to]=unique[i];}
 for(unsigned i=0;i<h[5];i++)texture_ids[i]=reorder[texture_ids[i]];
 for(unsigned i=0;i<68;i++){owner[i]=i;first_motion[i]=UINT32_MAX;}
 for(unsigned i=0;i<sizeof far_pairs/sizeof *far_pairs;i++){unsigned near=far_pairs[i][0],far=far_pairs[i][1];owner[far]=near;directory[near]=far;}
 unsigned nindices=0,nmotions=0;
 for(unsigned ci=0;ci<68;ci++){
  if(read_at(&bank_input,h[7]+ci*48,row,48))goto done;
  unsigned first=word(row),bones=word(row+4),source_motion=word(row+8),count=word(row+12),o=owner[ci];
  if(first>h[1] || bones>h[1]-first || source_motion>h[6] || count>h[6]-source_motion)goto done;
  if(first_motion[o]==UINT32_MAX){
   if(o!=ci)goto done;
   first_motion[o]=nmotions;motion_count[o]=count;
   for(unsigned mi=0;mi<count;mi++){
    if(read_at(&bank_input,h[13]+(source_motion+mi)*8,mr,8))goto done;
    unsigned frames=word(mr),at=word(mr+4);
    if(!span(at,bones*24,size) || read_at(&bank_input,at,channel_table,bones*24) || fseek(scratch[5],0,SEEK_SET))goto done;
    for(unsigned bi=0;bi<bones;bi++){
     unsigned char *channel=channel_table+bi*24;
     for(unsigned kind=0;kind<3;kind++){
      unsigned n=word(channel+kind*8),source=word(channel+kind*8+4),stride_=kind==1?6:12,stored;
      /* Compatibility with the original port's zero-relative helicopter
       * scale pointer. These unused clips retain no scale channel. */
      if((ci==35||ci==37)&&mi==0&&bi==1&&kind==2)n=source=0;
      if(n>65535 || source>size-h[14] || !span(h[14]+source,n*stride_,size) || intern(&keys,&bank_input,h[14]+source,n*stride_,kind==1?2:4,&stored))goto done;
      w32(channel+kind*8,n);w32(channel+kind*8+4,stored);
     }
    }
    unsigned offset;if(write(scratch[5],channel_table,bones*24)||fflush(scratch[5])||intern(&channels,&work[5],0,bones*24,1,&offset))goto done;
    w32(mr,frames);w32(mr+4,offset);if(write(scratch[6],mr,8))goto done;nmotions++;
   }
  }
  unsigned empty_texture=0;
  if(ci>=51){
   unsigned minimum=UINT32_MAX;
   for(unsigned bi=0;bi<bones;bi++){
    unsigned char bone_row[48];if(read_at(&bank_input,h[8]+(first+bi)*48,bone_row,48))goto done;
    unsigned bf=word(bone_row+40),nb=word(bone_row+44);if(bf>h[2] || nb>h[2]-bf)goto done;
    for(unsigned j=bf;j<bf+nb;j++){if(read_at(&bank_input,h[9]+j*16,batch,16))goto done;if(!(half(batch+14)&4) && half(batch+12)<minimum)minimum=half(batch+12);}
   }
   if(minimum!=UINT32_MAX)empty_texture=minimum;
  }
  w32(row+8,first_motion[o]);w32(row+12,motion_count[o]);if(write(scratch[0],row,48))goto done;
  for(unsigned bi=0;bi<bones;bi++){
   if(read_at(&bank_input,h[8]+(first+bi)*48,row,48))goto done;
   unsigned bf=word(row+40),nb=word(row+44);
   if(bf>h[2] || nb>h[2]-bf || write(scratch[1],row,48))goto done;
   for(unsigned j=bf;j<bf+nb;j++){
    if(read_at(&bank_input,h[9]+j*16,batch,16))goto done;
    unsigned vo=word(batch),io=word(batch+4),nv=half(batch+8),ni=half(batch+10),tex=half(batch+12),flags=half(batch+14);
    if(vo>h[3]||nv>h[3]-vo||io>h[4]||ni>h[4]-io||tex>=h[5])goto done;
    w32(batch+4,nindices);nindices+=ni;w16(batch+12,texture_ids[ci>=51&&(flags&4)?empty_texture:tex]);if(ci==26&&(flags&4))w16(batch+14,flags&15);
    if(write(scratch[2],batch,16)||copy(&bank_input,h[10]+vo*28,nv*28,scratch[3])||copy(&bank_input,h[11]+io*2,ni*2,scratch[4]))goto done;
   }
  }
 }
 unsigned lengths[9]={h[0]*48,h[1]*48,h[2]*16,h[3]*28,nindices*2,ntextures*16,nmotions*8+channels.bytes,keys.bytes,0};
 unsigned offsets[9],cursor=288;for(unsigned i=0;i<9;i++){offsets[i]=cursor;if(lengths[i]>UINT32_MAX-cursor-15)goto done;cursor=(cursor+lengths[i]+15)&~15u;}
 memset(head,0,80);memcpy(head,"JGC2",4);unsigned counts[7]={h[0],h[1],h[2],h[3],nindices,ntextures,nmotions};
 for(unsigned i=0;i<7;i++)w32(head+4+i*4,counts[i]);
 for(unsigned i=0;i<9;i++)w32(head+32+i*4,offsets[i]);
 memcpy(head+68,"JCL1",4);w32(head+72,51);w32(head+76,80);
 if(write(output,head,80))goto done;
 for(unsigned i=0;i<51;i++){w32(mr,directory[i]);if(write(output,mr,4))goto done;}if(pad(output,16))goto done;
 for(unsigned i=0;i<5;i++)if(copy(&work[i],0,lengths[i],output)||pad(output,16))goto done;
 unsigned pixel_at=offsets[8];
 for(unsigned i=0;i<44;i++){
  Texture *t=textures+order[i];pixel_at=(pixel_at+63)&~63u;w16(batch,t->w);w16(batch+2,t->h);w16(batch+4,t->flags);w16(batch+6,t->pad);w32(batch+8,pixel_at);pixel_at+=1024;w32(batch+12,pixel_at);pixel_at+=t->w*t->h;
  if(write(output,batch,16))goto done;
 }
 if(pad(output,16))goto done;
 for(unsigned i=0;i<nmotions;i++){if(read_at(&work[6],i*8,mr,8))goto done;w32(mr+4,word(mr+4)+offsets[6]+nmotions*8);if(write(output,mr,8))goto done;}
 if(copy(&channels.contents,0,channels.bytes,output)||pad(output,16)||copy(&keys.contents,0,keys.bytes,output)||pad(output,16))goto done;
 for(unsigned i=0;i<44;i++){Texture *t=textures+order[i];if(pad(output,64)||copy(&bank_input,t->clut,1024,output)||pad(output,64)||copy(&bank_input,t->pixels,t->w*t->h,output))goto done;}
 result=0;
done:free(snapshot);free(channel_table);free(textures);free(texture_ids);free(unique);free(keys.hash);free(keys.entries);free(keys.source_cache);free(channels.hash);free(channels.entries);return result;
}
