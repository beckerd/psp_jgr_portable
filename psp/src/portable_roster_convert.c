#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#include "portable_roster_convert.h"
#include "portable_scratch.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#define MAX_KEYS 16384u
#define HASH_SIZE 32768u
#define SOURCE_LIMIT (8u*1024u*1024u)
#define SOURCE_HEADROOM (8u*1024u*1024u)
#define SOURCE_CHUNK (512u*1024u)
typedef struct {FILE *file;unsigned char *data;unsigned size;} RosterInput;
#ifdef JGR_ROSTER_TESTING
static unsigned source_disabled,source_loaded,source_reclaimed;
void jgr_roster_test_source_disabled(unsigned disabled){source_disabled=disabled;}
void jgr_roster_test_source_stats(unsigned *loaded,unsigned *reclaimed)
{if(loaded)*loaded=source_loaded;if(reclaimed)*reclaimed=source_reclaimed;}
#endif
static unsigned word(const unsigned char*p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static unsigned half(const unsigned char*p){return p[0]|(unsigned)p[1]<<8;}
static void w32(unsigned char*p,unsigned n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i));}
static void w16(unsigned char*p,unsigned n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);}
static int read_at(FILE*f,unsigned at,void*p,unsigned n){return fseek(f,at,SEEK_SET)||fread(p,1,n,f)!=n?-1:0;}
static int write(FILE*f,const void*p,unsigned n){return fwrite(p,1,n,f)==n?0:-1;}
static int put(FILE*f,unsigned n){unsigned char b[4];w32(b,n);return write(f,b,4);}
static int pad(FILE*f,unsigned alignment){long at=ftell(f);if(at<0)return -1;
 while((unsigned long)at++&(alignment-1))if(fputc(0,f)==EOF)return -1;
 return 0;}
static int span(unsigned at,unsigned n,unsigned size){return at<=size&&n<=size-at;}
static int copy(FILE*a,unsigned at,unsigned n,FILE*b){unsigned char buf[4096];while(n){unsigned count=n<sizeof buf?n:sizeof buf;if(read_at(a,at,buf,count)||write(b,buf,count))return -1;
 at+=count;n-=count;}return 0;}
static int input_read(RosterInput *input,unsigned at,void *out,unsigned bytes)
{
 if(!span(at,bytes,input->size))return -1;
 if(input->data){memcpy(out,input->data+at,bytes);return 0;}
 return read_at(input->file,at,out,bytes);
}
static int input_copy(RosterInput *input,unsigned at,unsigned bytes,FILE *out)
{
 /* A scratch write may reclaim the source cache. Keep the current chunk on
  * the stack instead of passing a borrowed pointer into that write. */
 unsigned char buffer[4096];
 while(bytes){unsigned n=bytes<sizeof buffer?bytes:sizeof buffer;
  if(input_read(input,at,buffer,n)||write(out,buffer,n))return -1;
  at+=n;bytes-=n;
 }
 return 0;
}
static void input_release(void *context)
{
 RosterInput *input=context;
#ifdef JGR_ROSTER_TESTING
 if(input->data)source_reclaimed++;
#endif
 free(input->data);input->data=NULL;
}
static int input_cache(RosterInput *input,JgrScratchPool *scratch,JgrScratchReclaim *reclaim)
{
#ifdef JGR_ROSTER_TESTING
 if(source_disabled)return 0;
#endif
 if(input->size<4096 || input->size>SOURCE_LIMIT ||
    jgr_scratch_heap_available()<(size_t)input->size+SOURCE_HEADROOM)return 0;
 int saved=errno;input->data=malloc(input->size);errno=saved;
 if(!input->data)return 0;
 if(fseek(input->file,0,SEEK_SET))return -1;
 for(unsigned at=0;at<input->size;){unsigned n=input->size-at;if(n>SOURCE_CHUNK)n=SOURCE_CHUNK;
  if(fread(input->data+at,1,n,input->file)!=n)return -1;
  at+=n;
 }
 if(jgr_scratch_reclaim_begin(scratch,reclaim,input_release,input))return -1;
#ifdef JGR_ROSTER_TESTING
 source_loaded++;
#endif
 return 0;
}
typedef struct {uint64_t hash;unsigned source,bytes,alignment,offset;} Entry;
typedef struct {FILE *out;Entry *entries;unsigned *hash,count,capacity,bytes;int by_source;} Intern;
static int digest(RosterInput*f,unsigned at,unsigned n,uint64_t*out){uint64_t h=UINT64_C(14695981039346656037);unsigned char b[4096];while(n){unsigned count=n<sizeof b?n:sizeof b;if(input_read(f,at,b,count))return -1;
 for(unsigned i=0;i<count;i++){h^=b[i];h*=UINT64_C(1099511628211);}at+=count;n-=count;}*out=h;return 0;}
static int equal(RosterInput*a,unsigned at,FILE*b,unsigned bt,unsigned n){unsigned char x[2048],y[2048];while(n){unsigned count=n<sizeof x?n:sizeof x;if(input_read(a,at,x,count)||read_at(b,bt,y,count))return -1;
 if(memcmp(x,y,count))return 0;
 at+=count;bt+=count;n-=count;}return 1;}
static int intern(Intern*s,RosterInput*source,unsigned at,unsigned bytes,unsigned alignment,unsigned*out)
{
 uint64_t hash=s->by_source?((uint64_t)at<<32)|bytes:0;if(!s->by_source&&digest(source,at,bytes,&hash))return -1;
 unsigned slot=((unsigned)hash^(unsigned)(hash>>32)^alignment)&(HASH_SIZE-1);
 while(s->hash[slot]){Entry*e=s->entries+s->hash[slot]-1;if(e->hash==hash&&e->bytes==bytes&&e->alignment==alignment){int same=s->by_source?e->source==at:equal(source,at,s->out,e->offset,bytes);if(same<0)return -1;
 if(same){*out=e->offset;return 0;}}slot=(slot+1)&(HASH_SIZE-1);}
 if(s->count>=MAX_KEYS||s->bytes>UINT32_MAX-bytes-16)return -1;
 if(s->count==s->capacity){unsigned n=s->capacity?s->capacity*2:256;Entry*p=realloc(s->entries,n*sizeof*p);if(!p)return -1;
 s->entries=p;s->capacity=n;}
 if(fseek(s->out,s->bytes,SEEK_SET)||pad(s->out,alignment))return -1;
 long pos=ftell(s->out);if(pos<0||(unsigned long)pos>UINT32_MAX)return -1;
 *out=(unsigned)pos;
 if(input_copy(source,at,bytes,s->out))return -1;
 s->bytes=*out+bytes;s->entries[s->count]=(Entry){hash,at,bytes,alignment,*out};s->hash[slot]=++s->count;return 0;
}
static int pack(RosterInput*input,const unsigned h[16],unsigned size,unsigned ci,unsigned far,FILE*out,FILE*s[11],unsigned*source_count)
{
 unsigned char rows[2][48],b[48],batch[16],mr[8],channel[24];unsigned selected[2]={ci,far},nc=far?2:1,counts[7]={nc,0,0,0,0,0,0};unsigned *texids=calloc(3*8192,sizeof(unsigned)),*channels_at=texids?texids+8192:NULL,*channels_to=texids?texids+16384:NULL,nchannels=0;
 Intern keys={0},channels={0};int result=-1;keys.out=s[7];keys.by_source=!far;channels.out=s[5];keys.hash=calloc(HASH_SIZE,sizeof*keys.hash);channels.hash=calloc(HASH_SIZE,sizeof*channels.hash);if(!keys.hash||!channels.hash||!texids)goto done;
 for(unsigned k=0;k<11;k++)if(!s[k]||fseek(s[k],0,SEEK_SET))goto done;
 for(unsigned k=0;k<nc;k++){
  if(selected[k]>=h[0]||input_read(input,h[7]+selected[k]*48,rows[k],48))goto done;
 unsigned first=word(rows[k]),bones=word(rows[k]+4);if(first>h[1]||bones>h[1]-first)goto done;
  unsigned bone_base=counts[1];
  for(unsigned bi=0;bi<bones;bi++){
   if(input_read(input,h[8]+(first+bi)*48,b,48))goto done;
 int parent=(int16_t)half(b);if(parent>=0&&((unsigned)parent<first||(unsigned)parent>=first+bi))goto done;
 w16(b,parent<0?65535:bone_base+(unsigned)parent-first);
   unsigned begin=word(b+40),n=word(b+44);if(begin>h[2]||n>h[2]-begin)goto done;
 w32(b+40,counts[2]);
   for(unsigned j=begin;j<begin+n;j++){
    if(input_read(input,h[9]+j*16,batch,16))goto done;
 unsigned v=word(batch),ix=word(batch+4),nv=half(batch+8),ni=half(batch+10),tex=half(batch+12);
    if(v>h[3]||nv>h[3]-v||ix>h[4]||ni>h[4]-ix||tex>=h[5]||counts[2]>=65535||counts[3]>1000000-nv||counts[4]>3000000-ni)goto done;
    unsigned ti;for(ti=0;ti<counts[5];ti++)if(texids[ti]==tex)break;if(ti==counts[5])texids[counts[5]++]=tex;
    w32(batch,counts[3]);w32(batch+4,counts[4]);w16(batch+12,ti);
    if(write(s[2],batch,16)||input_copy(input,h[10]+v*28,nv*28,s[3])||input_copy(input,h[11]+ix*2,ni*2,s[4])||put(s[9],v))goto done;
    counts[2]++;counts[3]+=nv;counts[4]+=ni;
   }
   if(write(s[1],b,48))goto done;
 counts[1]++;
  }
  w32(rows[k],bone_base);w32(rows[k]+8,0);if(write(s[0],rows[k],48))goto done;
 }
 unsigned first_motion=word(rows[0]+8);/* rows were rebased; read original motion coordinates. */
 if(input_read(input,h[7]+ci*48,b,48))goto done;
 first_motion=word(b+8);unsigned motions=word(b+12),bones=word(b+4);
 if(first_motion>h[6]||motions>h[6]-first_motion||motions>8192)goto done;
 counts[6]=motions;
 for(unsigned mi=0;mi<motions;mi++){
  if(input_read(input,h[13]+(first_motion+mi)*8,mr,8))goto done;
 unsigned source=word(mr+4),offset=0;if(!span(source,bones*24,size))goto done;
  unsigned known=nchannels;if(!far)for(known=0;known<nchannels;known++)if(channels_at[known]==source)break;
  if(!far&&known<nchannels)offset=channels_to[known];else{
   if(fseek(s[10],0,SEEK_SET))goto done;
   for(unsigned bi=0;bi<bones;bi++){
    if(input_read(input,source+bi*24,channel,24))goto done;
    for(unsigned kind=0;kind<3;kind++){
     unsigned n=word(channel+kind*8),at=word(channel+kind*8+4),stride=kind==1?6:12,target;
     if(n>65535||at>size-h[14]||!span(h[14]+at,n*stride,size)||intern(&keys,input,h[14]+at,n*stride,far?(kind==1?2:4):4,&target))goto done;
 w32(channel+kind*8+4,target);
    }
    if(write(s[10],channel,24))goto done;
   }
   if(far){RosterInput channel_input={s[10],NULL,bones*24};if(fflush(s[10])||intern(&channels,&channel_input,0,bones*24,1,&offset))goto done;}
   else{offset=channels.bytes;if(fseek(s[5],offset,SEEK_SET)||copy(s[10],0,bones*24,s[5]))goto done;
 channels.bytes+=bones*24;channels_at[nchannels]=source;channels_to[nchannels++]=offset;}
  }
  w32(mr+4,offset);if(write(s[6],mr,8))goto done;
 }
 unsigned lengths[9]={counts[0]*48,counts[1]*48,counts[2]*16,counts[3]*28,counts[4]*2,counts[5]*16,counts[6]*8+channels.bytes,keys.bytes,0};
 unsigned offsets[9],cursor=far?96:80;for(unsigned i=0;i<9;i++){offsets[i]=cursor;if(lengths[i]>UINT32_MAX-cursor-15)goto done;
 cursor=(cursor+lengths[i]+15)&~15u;}
 unsigned char header[96]={0};memcpy(header,"JGC2",4);for(unsigned i=0;i<7;i++)w32(header+4+i*4,counts[i]);for(unsigned i=0;i<9;i++)w32(header+32+i*4,offsets[i]);if(far){memcpy(header+68,"JCL1",4);w32(header+72,1);w32(header+76,80);w32(header+80,1);}
 if(write(out,header,far?96:80))goto done;
 for(unsigned i=0;i<5;i++)if(copy(s[i],0,lengths[i],out)||pad(out,16))goto done;
 unsigned pixel=offsets[8];for(unsigned i=0;i<counts[5];i++){
  if(input_read(input,h[12]+texids[i]*16,batch,16))goto done;
 unsigned w=half(batch),height=half(batch+2),clut=word(batch+8),pixels=word(batch+12);if(!w||!height||w>1024||height>1024||!span(clut,1024,size)||!span(pixels,w*height,size))goto done;
  pixel=(pixel+63)&~63u;w32(batch+8,pixel);pixel+=1024;w32(batch+12,pixel);pixel+=w*height;if(write(out,batch,16))goto done;
 }
 if(pad(out,16))goto done;
 for(unsigned i=0;i<motions;i++){if(read_at(s[6],i*8,mr,8))goto done;
 w32(mr+4,word(mr+4)+offsets[6]+motions*8);if(write(out,mr,8))goto done;}
 if(copy(s[5],0,channels.bytes,out)||pad(out,16)||copy(s[7],0,keys.bytes,out)||pad(out,16))goto done;
 for(unsigned i=0;i<counts[5];i++){if(input_read(input,h[12]+texids[i]*16,batch,16)||pad(out,64)||input_copy(input,word(batch+8),1024,out)||pad(out,64)||input_copy(input,word(batch+12),half(batch)*half(batch+2),out))goto done;}
 *source_count=counts[2];result=0;
 done:free(texids);free(keys.entries);free(keys.hash);free(channels.entries);free(channels.hash);return result;
}
int jgr_roster_convert(FILE*input,FILE*light,const char*cache,FILE*out)
{
 FILE*s[11]={0},*bank=NULL;char path[1024];int result=-1;unsigned h[16],size,far[51];unsigned char header[80],lh[20],buffer[4096];
#ifdef JGR_ROSTER_TESTING
 source_loaded=source_reclaimed=0;
#endif
 if(!input||!light||!cache||!out||fseek(input,0,SEEK_END))return -1;
 long total=ftell(input);if(total<80||(unsigned long)total>UINT32_MAX||read_at(input,0,header,80)||memcmp(header,"JGC2",4)||memcmp(header+68,"JCL1",4)||word(header+72)!=51)return -1;
 size=(unsigned)total;
 for(unsigned i=0;i<16;i++)h[i]=word(header+4+i*4);
 if(h[0]<51||h[0]>102||h[1]>32767||h[2]>65535||h[3]>1000000||h[4]>3000000||h[5]>8192||h[6]>8192)return -1;
 static const unsigned strides[7]={48,48,16,28,2,16,8};for(unsigned i=0;i<9;i++){unsigned n=i<7?h[i]*strides[i]:0;if(!span(h[7+i],n,size)||(i&&h[7+i]<h[6+i]))return -1;}
 if(fseek(light,0,SEEK_END)||ftell(light)!=(long)(20+h[3]*20)||read_at(light,0,lh,20)||memcmp(lh,"PLT1",4)||word(lh+4)!=h[3]||word(lh+8)!=h[0]||word(lh+16)!=20)return -1;
 JgrScratchPool *scratch=jgr_scratch_begin();if(!scratch)return -1;
 RosterInput source={input,NULL,size};JgrScratchReclaim reclaim={0};
 if(input_cache(&source,scratch,&reclaim))goto done;
 unsigned hash=2166136261u;for(unsigned at=0;at<h[3]*28;){unsigned n=h[3]*28-at;if(n>sizeof buffer)n=sizeof buffer;if(input_read(&source,h[10]+at,buffer,n))goto done;
 for(unsigned i=0;i<n;i++)if((at+i)%28<8||(at+i)%28>=12){hash^=buffer[i];hash*=16777619u;}
 at+=n;}if(hash!=word(lh+12))goto done;
 if(!span(word(header+76),51*4,h[7]))goto done;
 for(unsigned i=0;i<51;i++){if(input_read(&source,word(header+76)+i*4,buffer,4))goto done;
 far[i]=word(buffer);if(far[i]>=h[0]||(far[i]&&far[i]==i))goto done;}
 for(unsigned i=0;i<12;i++){
  if(snprintf(path,sizeof path,"%s/roster-setup-%u.tmp",cache,i)>=(int)sizeof path)goto done;
 struct stat st;
#ifdef __PSP__
  int exists=stat(path,&st);
#else
  int exists=lstat(path,&st);
#endif
  if((!exists&&!S_ISREG(st.st_mode))||(exists&&errno!=ENOENT))goto done;
 FILE*f=jgr_scratch_open(scratch,path);if(!f)goto done;
 if(i==11)bank=f;else s[i]=f;
 }
 memset(buffer,0,848);if(write(out,buffer,848))goto done;
 for(unsigned i=0;i<51;i++){
  if(fseek(bank,0,SEEK_SET))goto done;
 unsigned ns=0;if(pack(&source,h,size,i,far[i],bank,s,&ns)||fflush(bank))goto done;
 long bytes=ftell(bank);if(bytes<0||(unsigned long)bytes>UINT32_MAX||pad(out,64))goto done;
 long start=ftell(out);if(start<0||copy(bank,0,(unsigned)bytes,out)||pad(out,16))goto done;
 long map=ftell(out);if(map<0||copy(s[9],0,ns*4,out))goto done;
 long end=ftell(out);
  if(end<0||fseek(out,32+i*16,SEEK_SET)||put(out,(unsigned)start)||put(out,(unsigned)bytes)||put(out,(unsigned)map)||put(out,ns)||fseek(out,end,SEEK_SET))goto done;
 }
 long end=ftell(out);if(end<0||(unsigned long)end>UINT32_MAX||fseek(out,0,SEEK_SET)||write(out,"JCR1",4)||put(out,51)||put(out,(unsigned)end)||put(out,h[3])||put(out,h[0])||put(out,hash)||put(out,1)||put(out,0)||fseek(out,end,SEEK_SET))goto done;
 result=0;
 done:{int failure_errno=errno;
 if(jgr_scratch_reclaim_end(&reclaim)&&!result){result=-1;failure_errno=errno;}
 free(source.data);
 for(unsigned i=0;i<12;i++){FILE*f=i==11?bank:s[i];if(f){if(fclose(f)&&!result){result=-1;failure_errno=errno;}snprintf(path,sizeof path,"%s/roster-setup-%u.tmp",cache,i);remove(path);}}
 if(jgr_scratch_end()&&!result){result=-1;failure_errno=errno;}if(result)errno=failure_errno;return result;}
}
