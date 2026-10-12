#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#include "portable_stage_pack.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct {const unsigned char *bytes;uint32_t count,source,destination,id;} Span;
static uint32_t rd(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
static unsigned half(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static int order(const void *av,const void *bv)
{const Span *a=av,*b=bv;if(a->count!=b->count)return a->count>b->count?-1:1;return memcmp(a->bytes,b->bytes,a->count);}
static int read_at(FILE *f,unsigned at,void *p,unsigned n)
{return fseek(f,at,SEEK_SET)||fread(p,1,n,f)!=n?-1:0;}
static int bounded(unsigned at,unsigned count,unsigned stride,unsigned length)
{return at<=length&&count<=(length-at)/stride;}
static int copy_range(FILE *source,unsigned at,unsigned count,unsigned length,FILE *out)
{
 unsigned char buffer[16384];
 if(!bounded(at,count,1,length)||fseek(source,at,SEEK_SET))return -1;
 while(count){unsigned n=count<sizeof buffer?count:sizeof buffer;
  if(fread(buffer,1,n,source)!=n||fwrite(buffer,1,n,out)!=n)return -1;
  count-=n;
 }
 return 0;
}
static int equal_range(FILE *source,unsigned at,const unsigned char *data,unsigned count)
{
 unsigned char buffer[16384];
 if(fseek(source,at,SEEK_SET))return -1;
 while(count){unsigned n=count<sizeof buffer?count:sizeof buffer;
  if(fread(buffer,1,n,source)!=n||memcmp(buffer,data,n))return -1;
  data+=n;count-=n;
 }
 return 0;
}
static int pad(FILE *f,unsigned at)
{long n=ftell(f);if(n<0||(unsigned long)n>at)return -1;unsigned char zero[16]={0};while((unsigned long)n<at){unsigned k=at-n;if(k>16)k=16;if(fwrite(zero,1,k,f)!=k)return -1;n+=k;}return 0;}
static uint32_t find_span(const unsigned char *data,unsigned length,const unsigned char *key,unsigned count,unsigned stride)
{
 if(!count)return 0;
 if(count>length)return UINT32_MAX;
 /* Bounded Horspool search, rejecting unaligned byte occurrences. */
 unsigned shift[256];for(unsigned i=0;i<256;i++)shift[i]=count;
 for(unsigned i=0;i+1<count;i++)shift[key[i]]=count-1-i;
 unsigned at=0;while(at<=length-count){unsigned last=data[at+count-1];
  if(last==key[count-1]&&!memcmp(data+at,key,count)){if(!(at%stride))return at;at++;}
  else at+=shift[last];
 }
 return UINT32_MAX;
}
/* Index aligned prefixes, not whole spans: a match can begin inside an older
 * span or cross two appended spans. Insertion order keeps each bucket sorted
 * by byte offset, so the first full comparison match is the original search's
 * earliest aligned match. The optional bounded allocation never owns bytes. */
#define SPAN_INDEX_BYTES (2u*1024u*1024u)
typedef struct {unsigned *head,*tail,*next,slots,count,stride,anchor;} SpanIndex;
static unsigned span_hash(const unsigned char *p,unsigned bytes)
{unsigned hash=2166136261u;for(unsigned i=0;i<bytes;i+=4)hash=(hash^rd(p+i))*16777619u;return hash;}
static void span_index_begin(SpanIndex *index,unsigned capacity,unsigned stride)
{
 memset(index,0,sizeof *index);unsigned positions=capacity/stride,slots=128;
 if(positions<256)return;
 while(slots<positions/4&&slots<16384)slots*=2;
 if(positions>SPAN_INDEX_BYTES/sizeof(unsigned)-slots*2)return;
 int saved_errno=errno;unsigned *data=calloc(positions+slots*2,sizeof *data);
 if(!data){errno=saved_errno;return;}
 index->head=data;index->tail=data+slots;index->next=data+slots*2;
 index->slots=slots;index->stride=stride;index->anchor=stride==24?24:8;
}
static void span_index_append(SpanIndex *index,const unsigned char *data,unsigned length)
{
 if(!index->head || length<index->anchor)return;
 for(unsigned at=index->count*index->stride;at<=length-index->anchor;at+=index->stride){
  unsigned slot=span_hash(data+at,index->anchor)&(index->slots-1),id=++index->count;
  if(index->tail[slot])index->next[index->tail[slot]-1]=id;
  else index->head[slot]=id;
  index->tail[slot]=id;
 }
}
static unsigned span_index_find(const SpanIndex *index,const unsigned char *data,unsigned length,
                                const unsigned char *key,unsigned count,unsigned stride)
{
 if(!index->head || count<index->anchor)return find_span(data,length,key,count,stride);
 if(count>length)return UINT32_MAX;
 unsigned slot=span_hash(key,index->anchor)&(index->slots-1);
 for(unsigned id=index->head[slot];id;id=index->next[id-1]){
  unsigned at=(id-1)*stride;if(at>length-count)break;
  if(!memcmp(data+at,key,count))return at;
 }
 return UINT32_MAX;
}
int jgr_stage_pack(FILE *source,FILE *key_file,FILE *out,int kogane)
{return jgr_stage_pack_view(source,key_file,out,kogane,NULL,0);}
int jgr_stage_pack_view(FILE *source,FILE *key_file,FILE *out,int kogane,const void *key_view,size_t key_size)
{
 unsigned char header[80],*data=NULL,*owned_keys=NULL,*packed[2]={NULL,NULL};
 const unsigned char *keys=NULL;Span *spans=NULL;SpanIndex search={0};
 unsigned length=0,counts[5],offset[6],newcount[2]={0,0};int result=-1;
 if(!source||!out||source==out||(key_view&&!key_file)||fseek(source,0,SEEK_END))return -1;
 long end=ftell(source);
 if(end<80||end>12*1024*1024)return -1;
 length=(unsigned)end;if(read_at(source,0,header,sizeof header)||memcmp(header,"JGS1",4))goto done;
 for(unsigned i=0;i<5;i++)counts[i]=rd(header+4+i*4);
 for(unsigned i=0;i<6;i++)offset[i]=rd(header+24+i*4);
 unsigned strides[5]={16,32,16,24,2};
 for(unsigned i=0;i<5;i++)if(!bounded(offset[i],counts[i],strides[i],length)||offset[i]<80||offset[i]+counts[i]*strides[i]>offset[i+1])goto done;
 if(offset[5]>length||counts[2]>65536)goto done;
 /* Pixels are only copied after packing. Keeping them in the raw scratch
  * stream avoids a second large allocation at the city's peak heap usage. */
 /* Ordering keys already describe vertex spans. Read corrected vertices
  * only when emitting/checking a packed span instead of retaining both copies. */
 unsigned prefix=key_file?offset[3]:offset[5],index_bytes=counts[4]*2;
 data=malloc(prefix+(key_file?index_bytes:0));if(!data){errno=ENOMEM;goto done;}
 if(read_at(source,0,data,prefix))goto done;
 if(key_file&&read_at(source,offset[4],data+prefix,index_bytes))goto done;
 const unsigned char *source_bytes[2]={key_file?NULL:data+offset[3],key_file?data+prefix:data+offset[4]};
 if(key_file){unsigned bytes=counts[3]*24;
  if(key_view){if(key_size<bytes)goto done;keys=key_view;}
  else{owned_keys=malloc(bytes?bytes:1);if(!owned_keys){errno=ENOMEM;goto done;}if(read_at(key_file,0,owned_keys,bytes))goto done;keys=owned_keys;}
 }
 spans=calloc(counts[2]?counts[2]:1,sizeof *spans);if(!spans){errno=ENOMEM;goto done;}
 for(unsigned field=0;field<2;field++){
  unsigned stride=field?2:24,base=offset[3+field],capacity=counts[3+field]*stride;
  packed[field]=malloc(capacity?capacity:1);if(!packed[field]){errno=ENOMEM;goto done;}unsigned used=0;
  for(unsigned i=0;i<counts[2];i++){
   unsigned char *r=data+offset[2]+i*16;unsigned at=rd(r+field*4),count=half(r+8+field*2);
   if(at>counts[3+field]||count>counts[3+field]-at)goto done;
   spans[i]=(Span){(field==0&&keys?keys:source_bytes[field])+at*stride,count*stride,at*stride,0,i};
  }
  qsort(spans,counts[2],sizeof *spans,order);
  span_index_begin(&search,capacity,stride);
  for(unsigned i=0;i<counts[2];i++){
   Span *s=spans+i;unsigned at;
   if(i&&s->count==spans[i-1].count&&!memcmp(s->bytes,spans[i-1].bytes,s->count))at=spans[i-1].destination;
   else at=span_index_find(&search,packed[field],used,s->bytes,s->count,stride);
   if(at==UINT32_MAX){at=used;if(s->count>capacity-used)goto done;
    if(field==0&&keys)memcpy(packed[field]+at,s->bytes,s->count);
    else memcpy(packed[field]+at,source_bytes[field]+s->source,s->count);
    used+=s->count;
    span_index_append(&search,packed[field],used);
   }
   s->destination=at;wr(data+offset[2]+s->id*16+field*4,at/stride);
  }
  free(search.head);memset(&search,0,sizeof search);
  if(field==0&&keys){
   /* Finish every key lookup before replacing packed keys with corrected
    * vertices in the same buffer. New spans append in traversal order;
    * shared spans only reference bytes written by an earlier new span. */
   free(owned_keys);owned_keys=NULL;keys=NULL;
   unsigned written=0;
   for(unsigned i=0;i<counts[2];i++){
    const Span *s=spans+i;unsigned at=s->destination;
    if(at==written){
     if(read_at(source,base+s->source,packed[field]+at,s->count))goto done;
     written+=s->count;
    }else if(at>written||s->count>written-at||equal_range(source,base+s->source,packed[field]+at,s->count))goto done;
   }
   if(written!=used)goto done;
  }
  newcount[field]=used/stride;
 }
 unsigned oldpixels=offset[5],newindices=(offset[3]+newcount[0]*24+15)&~15u,newpixels=(newindices+newcount[1]*2+15)&~15u;
 wr(data+16,newcount[0]);wr(data+20,newcount[1]);wr(data+40,newindices);wr(data+44,newpixels);
 if(kogane){
  if(counts[0]!=243)goto done;
  unsigned char textures[28*16];memcpy(textures,data+offset[0]+215*16,sizeof textures);
  for(unsigned old=215;old<243;old++){unsigned dest=old<226?old+17:old-11;memcpy(data+offset[0]+dest*16,textures+(old-215)*16,16);}
  for(unsigned i=0;i<counts[2];i++){unsigned char *r=data+offset[2]+i*16;unsigned tex=half(r+12);if(tex>=215&&tex<243){tex=tex<226?tex+17:tex-11;r[12]=tex;r[13]=tex>>8;}}
 }
 if(kogane){
  unsigned at=newpixels;for(unsigned i=0;i<counts[0];i++){unsigned char *r=data+offset[0]+i*16;unsigned w=half(r),h=half(r+2);if(!w||!h||w>1024||h>1024||!bounded(rd(r+8),1024,1,length)||!bounded(rd(r+12),w,h,length))goto done;at=(at+15)&~15u;at+=1024;at=(at+15)&~15u;at+=w*h;}
  /* Descriptor addresses are patched while streaming the reordered pixels. */
  unsigned char *old=malloc(counts[0]*16);if(!old){errno=ENOMEM;goto done;}memcpy(old,data+offset[0],counts[0]*16);at=newpixels;
  for(unsigned i=0;i<counts[0];i++){unsigned char *r=data+offset[0]+i*16;unsigned w=half(r),h=half(r+2);at=(at+15)&~15u;wr(r+8,at);at+=1024;at=(at+15)&~15u;wr(r+12,at);at+=w*h;}
  int bad=fwrite(data,1,offset[3],out)!=offset[3]||fwrite(packed[0],24,newcount[0],out)!=newcount[0]||pad(out,newindices)||fwrite(packed[1],2,newcount[1],out)!=newcount[1]||pad(out,newpixels);
  for(unsigned i=0;i<counts[0]&&!bad;i++){const unsigned char *r=old+i*16,*updated=data+offset[0]+i*16;unsigned n=half(r)*half(r+2);bad=pad(out,rd(updated+8))||copy_range(source,rd(r+8),1024,length,out)||pad(out,rd(updated+12))||copy_range(source,rd(r+12),n,length,out);}
  free(old);if(bad)goto done;
 }else{
  for(unsigned i=0;i<counts[0];i++){unsigned char *r=data+offset[0]+i*16;unsigned clut=rd(r+8),pixel=rd(r+12);if(clut<oldpixels||pixel<oldpixels||clut>length||pixel>length)goto done;wr(r+8,clut-oldpixels+newpixels);wr(r+12,pixel-oldpixels+newpixels);}
  if(fwrite(data,1,offset[3],out)!=offset[3]||fwrite(packed[0],24,newcount[0],out)!=newcount[0]||pad(out,newindices)||fwrite(packed[1],2,newcount[1],out)!=newcount[1]||pad(out,newpixels)||copy_range(source,oldpixels,length-oldpixels,length,out))goto done;
 }
 result=0;
 done:{int failure_errno=errno;free(search.head);free(data);free(owned_keys);free(packed[0]);free(packed[1]);free(spans);if(result)errno=failure_errno;return result;}
}
int jgr_stage_ownership_hash(FILE *geometry,FILE *ownership)
{
 unsigned char head[48],buffer[4096];if(!geometry||!ownership||read_at(geometry,0,head,48)||memcmp(head,"JGS1",4))return -1;
 unsigned bytes=rd(head+36);uint32_t hash=2166136261u;if(bytes<80||bytes>4*1024*1024||fseek(geometry,0,SEEK_SET))return -1;
 while(bytes){unsigned n=bytes<sizeof buffer?bytes:sizeof buffer;if(fread(buffer,1,n,geometry)!=n)return -1;for(unsigned i=0;i<n;i++){hash^=buffer[i];hash*=16777619u;}bytes-=n;}
 wr(buffer,hash);return fseek(ownership,28,SEEK_SET)||fwrite(buffer,1,4,ownership)!=4?-1:0;
}
