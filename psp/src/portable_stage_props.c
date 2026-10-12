/* Source-owned prop LODs and shared local geometry for first-launch setup. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#include "portable_stage_props.h"
#include "portable_stage.h"
#include "portable_models.h"
#include "portable_scratch.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#define EB 0x8c010000u
#define SB 0x8cb00000u
#define MAX_OBJECTS 8192u
#define MAX_BATCHES 65536u
#define MAX_VERTICES 1048576u
#define MAX_INDICES 2097152u
#define MAX_PLACEMENTS 4096u
#define TEMP_COUNT 8u

typedef struct {FILE *file;unsigned size,count[5],offset[6];unsigned char header[80],*meta[3];} Bank;
typedef struct {uint32_t address,model,texlist,far_model,far_texlist;unsigned pid,sector,record;double position[3],matrix[16];} Prop;
typedef struct {uint32_t task,model,texlist,far_model,far_texlist;unsigned sectors,stride,types,bank;} Spec;
typedef struct {uint32_t mesh,list;double matrix[16];unsigned local,enabled;} Instance;
typedef struct {unsigned start,end,output;} Range;
typedef struct {unsigned offset,present;} LocalBatch;
typedef struct {unsigned char hash[32];unsigned vo,io,vn,ix,tex,flags;} Shared;
typedef struct {
 JgrDisc *disc;JgrDiscFile code,data;unsigned stage;const char *directory;
 Prop *props;unsigned nprops;FILE *temp[TEMP_COUNT];unsigned char *ownership;unsigned own_bytes,own_capacity;
 JgrScratchPool *scratch;
 Bank bank;Instance *instances;JgrStageProp *locals;unsigned nlocals;
} Context;
static unsigned u16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t u32(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float f32(const unsigned char *p){uint32_t n=u32(p);float f;memcpy(&f,&n,4);return f;}
static void w16(unsigned char *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void w32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
static void wf(unsigned char *p,double d){float f=(float)d;uint32_t n;memcpy(&n,&f,4);w32(p,n);}
static unsigned align16(unsigned n){return (n+15)&~15u;}
static int read_at(FILE *f,unsigned at,void *p,unsigned n){return fseek(f,at,SEEK_SET)||fread(p,1,n,f)!=n?-1:0;}
static int write(FILE *f,const void *p,unsigned n){return fwrite(p,1,n,f)==n?0:-1;}
static int pad(FILE *f,unsigned at){long pos=ftell(f);if(pos<0||(unsigned long)pos>at)return -1;while((unsigned long)pos++<at)if(fputc(0,f)==EOF)return -1;return 0;}
static int length(FILE *f,unsigned *n){long pos;if(fseek(f,0,SEEK_END)||(pos=ftell(f))<0||(unsigned long)pos>32*1024*1024)return -1;*n=(unsigned)pos;return 0;}
static int copy(FILE *from,unsigned at,unsigned n,FILE *to){unsigned char bytes[4096];if(fseek(from,at,SEEK_SET))return -1;while(n){unsigned k=n<sizeof bytes?n:sizeof bytes;if(fread(bytes,1,k,from)!=k||write(to,bytes,k))return -1;n-=k;}return 0;}
static int equal(FILE *a,unsigned aa,FILE *b,unsigned ba,unsigned n){unsigned char x[2048],y[2048];while(n){unsigned k=n<sizeof x?n:sizeof x;if(read_at(a,aa,x,k)||read_at(b,ba,y,k)||memcmp(x,y,k))return 0;n-=k;aa+=k;ba+=k;}return 1;}
static uint32_t fingerprint(FILE *f,unsigned n){unsigned char b[4096];uint32_t h=2166136261u;if(fseek(f,0,SEEK_SET))return 0;while(n){unsigned k=n<sizeof b?n:sizeof b;if(fread(b,1,k,f)!=k)return 0;for(unsigned i=0;i<k;i++)h=(h^b[i])*16777619u;n-=k;}return h;}
static int hash_range(FILE *f,unsigned at,unsigned n,JgrSha256 *h){unsigned char b[4096];while(n){unsigned k=n<sizeof b?n:sizeof b;if(read_at(f,at,b,k))return -1;jgr_sha256_update(h,b,k);at+=k;n-=k;}return 0;}
static int source(Context *c,uint32_t address,void *p,unsigned n){const JgrDiscFile *f;unsigned base;if(address>=SB){f=&c->data;base=SB;}else{f=&c->code;base=EB;}if(address<base||address-base>f->size||n>f->size-(address-base))return -1;return jgr_disc_read(c->disc,f,address-base,p,n);}
static int word_at(Context *c,uint32_t a,uint32_t *p){unsigned char b[4];if(source(c,a,b,4))return -1;*p=u32(b);return 0;}
static int data_pointer(Context *c,uint32_t a){return !(a&3)&&a>=SB&&a-SB<c->data.size;}
static void free_bank(Bank *b){for(unsigned i=0;i<3;i++)free(b->meta[i]);memset(b,0,sizeof *b);}
static int load_bank(Bank *b,FILE *f){
 memset(b,0,sizeof *b);b->file=f;
 if(length(f,&b->size)||read_at(f,0,b->header,80)||memcmp(b->header,"JGS1",4))goto bad;
 for(unsigned i=0;i<5;i++)b->count[i]=u32(b->header+4+i*4);
 for(unsigned i=0;i<6;i++)b->offset[i]=u32(b->header+24+i*4);
 if(b->count[0]>8192||b->count[1]>MAX_OBJECTS||b->count[2]>MAX_BATCHES||b->count[3]>MAX_VERTICES||b->count[4]>MAX_INDICES||b->offset[0]<80)goto bad;
 const unsigned units[5]={16,32,16,24,2};
 for(unsigned i=0;i<5;i++)if((b->offset[i]&15)||b->offset[i]>b->offset[i+1]||b->count[i]*units[i]>b->offset[i+1]-b->offset[i])goto bad;
 if((b->offset[5]&15)||b->offset[5]>b->size)goto bad;
 for(unsigned i=0;i<3;i++){unsigned n=b->count[i]*units[i];b->meta[i]=malloc(n?n:1);if(!b->meta[i]||read_at(f,b->offset[i],b->meta[i],n))goto bad;}
 for(unsigned i=0;i<b->count[0];i++){const unsigned char *t=b->meta[0]+i*16;unsigned w=u16(t),h=u16(t+2),cl=u32(t+8),px=u32(t+12);if(!w||!h||w>1024||h>1024||cl<b->offset[5]||cl>b->size||1024>b->size-cl||px<b->offset[5]||px>b->size||w*h>b->size-px)goto bad;}
 for(unsigned i=0;i<b->count[1];i++){const unsigned char *o=b->meta[1]+i*32;unsigned first=u32(o+16),n=u32(o+20);if(first>b->count[2]||n>b->count[2]-first||u32(o+24)&4)goto bad;for(unsigned k=0;k<4;k++)if(!isfinite(f32(o+4*k)))goto bad;}
 for(unsigned i=0;i<b->count[2];i++){const unsigned char *r=b->meta[2]+i*16;unsigned vo=u32(r),io=u32(r+4),vn=u16(r+8),in=u16(r+10);if(vo>b->count[3]||vn>b->count[3]-vo||io>b->count[4]||in>b->count[4]-io||(!(u16(r+14)&4)&&u16(r+12)>=b->count[0]))goto bad;}
 return 0;
 bad:free_bank(b);return -1;
}
static FILE *temporary(Context *c,unsigned slot){
 if(slot>=TEMP_COUNT||c->temp[slot])return NULL;
 char p[1024];int n=snprintf(p,sizeof p,"%s/props-setup-%u.tmp",c->directory,slot);if(n<0||(size_t)n>=sizeof p)return NULL;struct stat st;
#ifdef __PSP__
 int exists=stat(p,&st);
#else
 int exists=lstat(p,&st);
#endif
 if((!exists&&!S_ISREG(st.st_mode))||(exists&&errno!=ENOENT))return NULL;
 return c->temp[slot]=jgr_scratch_open(c->scratch,p);
}
/* These switches reproduce native task dispatch categories; placements and all
 * model/texture pointers are read from the original executable/stage banks. */
static int selected(unsigned stage,unsigned bank,unsigned type){
 if(stage==1)switch(type){case 4:case 5:case 10:case 12:case 13:case 14:case 15:case 16:case 17:case 18:case 19:case 20:case 21:case 22:case 27:return 1;default:return 0;}
 if(stage==2)switch(type){case 2:case 3:case 6:case 10:case 11:case 12:case 13:case 14:case 15:case 16:case 20:case 21:case 22:case 23:case 24:case 25:case 26:case 27:case 28:case 29:case 30:case 34:case 35:case 36:case 37:case 38:case 39:case 40:case 41:case 43:case 59:case 60:case 61:case 62:case 63:case 64:case 65:case 66:case 67:case 70:case 88:case 89:case 90:return 1;default:return 0;}
 if(stage==3){if(bank)return type<=2;switch(type){case 1:case 2:case 3:case 4:case 5:case 9:case 10:case 12:case 13:case 14:case 15:case 16:case 17:case 18:case 19:case 20:case 21:case 24:case 25:case 26:case 27:case 28:case 29:case 30:case 31:case 35:case 36:case 37:case 38:case 39:case 40:case 41:case 42:case 43:case 45:case 49:case 51:case 52:case 53:case 54:case 55:case 56:case 67:case 68:case 69:return 1;default:return 0;}}
 if(stage==5)switch(type){case 0:case 2:case 3:case 4:case 6:case 7:case 8:case 9:case 10:case 11:case 14:case 15:case 17:case 18:case 19:return 1;default:return 0;}
 return 0;
}
static void rotation(double m[16],unsigned axis,double a){jgr_model_identity(m);double cs=cos(a),sn=sin(a);if(axis==0){m[5]=cs;m[6]=-sn;m[9]=sn;m[10]=cs;}else if(axis==1){m[0]=cs;m[2]=sn;m[8]=-sn;m[10]=cs;}else{m[0]=cs;m[1]=-sn;m[4]=sn;m[5]=cs;}}
static int read_placements(Context *c){
 static const Spec specs[]={
 {0x8c10618c,0x8c107564,0x8c1075d4,0,0,63,40,28,0},
 {0x8c108284,0x8c109d2c,0x8c109e98,0x8c10a004,0x8c10a170,114,40,91,0},
 {0x8c1ba918,0x8c1bc064,0x8c1bc17c,0,0,46,52,70,0},
 {0x8c1baa88,0x8c1bc64c,0x8c1bc668,0,0,46,52,7,1},
 {0x8c203298,0x8c203f44,0x8c203f94,0,0,71,52,20,0}};
 unsigned begin=c->stage==1?0:c->stage==2?1:c->stage==3?2:4,end=begin+(c->stage==3?2:1);
 c->props=calloc(MAX_PLACEMENTS,sizeof *c->props);if(!c->props)return -1;
 for(unsigned bank=begin;bank<end;bank++){const Spec *s=specs+bank;
  for(unsigned sector=0;sector<s->sectors;sector++){
   uint32_t first,check;if(word_at(c,s->task+sector*4,&first))return -1;if(!data_pointer(c,first))continue;
   if(word_at(c,first,&check))return -1;
   int pointers=data_pointer(c,check);unsigned guard=0;
   for(uint32_t at=first;;at+=pointers?4:s->stride){uint32_t address=at;unsigned char raw[52];
    if(pointers){if(word_at(c,at,&address))return -1;if(!data_pointer(c,address))break;}
    if(source(c,address,raw,s->stride))return -1;
    int32_t type=(int32_t)u32(raw);if(!pointers&&type< -1)break;if(++guard>10000)return -1;
    if(type<0||(unsigned)type>=s->types||!selected(c->stage,s->bank,(unsigned)type))continue;
    if(c->nprops>=MAX_PLACEMENTS)return -1;
    for(unsigned i=0;i<c->nprops;i++)if(c->props[i].address==address)return -1;
    Prop *p=c->props+c->nprops++;p->address=address;p->pid=type;p->sector=sector;p->record=~0u;
    if(word_at(c,s->model+type*4,&p->model)||word_at(c,s->texlist+type*4,&p->texlist)||!data_pointer(c,p->model))return -1;
    if(s->far_model&&(word_at(c,s->far_model+type*4,&p->far_model)||word_at(c,s->far_texlist+type*4,&p->far_texlist)))return -1;
    double t[16],r[3][16],a[16],b[16],scale[16];jgr_model_identity(t);jgr_model_identity(scale);
    for(unsigned k=0;k<3;k++){double pos=f32(raw+4+k*4),sc=s->stride==52?f32(raw+28+k*4):1.;if(!isfinite(pos)||!isfinite(sc)||sc==0)return -1;p->position[k]=pos;t[k*4+3]=pos;scale[k*5]=sc;rotation(r[k],k,(int32_t)u32(raw+16+k*4)*(3.141592653589793*2/65536));}
    jgr_model_multiply(a,r[1],r[0]);jgr_model_multiply(b,r[2],a);jgr_model_multiply(a,t,b);jgr_model_multiply(p->matrix,a,scale);
   }
  }
 }
 return 0;
}
static unsigned char *record(Context *c,unsigned index){return c->ownership+32+16*u32(c->ownership+8)+32*index;}
static int ownership(Context *c,FILE *file){
 unsigned n;if(length(file,&n)||n<32||n>1024*1024)return -1;c->own_capacity=n+MAX_PLACEMENTS*32;c->ownership=malloc(c->own_capacity);if(!c->ownership||read_at(file,0,c->ownership,n))return -1;c->own_bytes=n;unsigned char *h=c->ownership;
 unsigned sectors=u32(h+8),rows=u32(h+12);if(memcmp(h,"JRS1",4)||u32(h+4)!=c->stage||sectors>256||rows>8192||n!=32+sectors*16+rows*32||u32(h+20)!=c->bank.count[1]||u32(h+24)!=c->bank.count[2]||u32(h+28)!=fingerprint(c->bank.file,c->bank.offset[3]))return -1;
 for(unsigned i=0;i<rows;i++){unsigned char *r=record(c,i);for(unsigned k=0;k<2;k++){unsigned first=u16(r+k*4),count=u16(r+k*4+2);if(first>c->bank.count[1]||count>c->bank.count[1]-first)return -1;}if(u16(r+20)>=sectors)return -1;for(unsigned j=0;j<i;j++)if(u32(record(c,j)+28)==u32(r+28))return -1;}
 for(unsigned p=0;p<c->nprops;p++)for(unsigned i=0;i<rows;i++)if(u32(record(c,i)+28)==c->props[p].address){
  unsigned char *r=record(c,i);if(!u16(r+2)||u16(r+6)||f32(r+24)!=0||u16(r+20)!=c->props[p].sector||(u16(r+22)!=0&&u16(r+22)!=9))return -1;
  for(unsigned k=0;k<3;k++)if(f32(r+8+4*k)!=c->props[p].position[k])return -1;
  c->props[p].record=i;w16(r+22,9);break;
 }
 for(unsigned i=0;i<rows;i++)if(u16(record(c,i)+22)==9){unsigned found=0;for(unsigned p=0;p<c->nprops;p++)found|=c->props[p].record==i;if(!found)return -1;}
 return 0;
}
static int finish_bank(Bank *b,FILE *vertices,unsigned nv,FILE *indices,unsigned ni,const unsigned char *extension,unsigned ext_bytes,FILE *out){
 unsigned offsets[6],at=80,sizes[6]={b->count[0]*16,b->count[1]*32,b->count[2]*16,nv*24,ni*2,0};
 for(unsigned i=0;i<6;i++){if(i==3)at+=ext_bytes;offsets[i]=at;at=align16(at+sizes[i]);}
 unsigned char header[80];memcpy(header,b->header,80);for(unsigned i=0;i<5;i++)w32(header+4+i*4,i==3?nv:i==4?ni:b->count[i]);for(unsigned i=0;i<6;i++)w32(header+24+i*4,offsets[i]);
 if(fseek(out,0,SEEK_SET)||write(out,header,80))return -1;
 for(unsigned i=0;i<b->count[0];i++){unsigned char r[16];memcpy(r,b->meta[0]+16*i,16);w32(r+8,u32(r+8)-b->offset[5]+offsets[5]);w32(r+12,u32(r+12)-b->offset[5]+offsets[5]);if(write(out,r,16))return -1;}
 if(pad(out,offsets[1])||write(out,b->meta[1],sizes[1])||pad(out,offsets[2])||write(out,b->meta[2],sizes[2])||pad(out,offsets[3]-ext_bytes)||(ext_bytes&&write(out,extension,ext_bytes))||pad(out,offsets[3])||copy(vertices,0,sizes[3],out)||pad(out,offsets[4])||copy(indices,0,sizes[4],out)||pad(out,offsets[5])||copy(b->file,b->offset[5],b->size-b->offset[5],out)||fflush(out))return -1;
 return 0;
}
typedef struct {unsigned char hash[32];FILE *file;unsigned clut,pixels;} TextureKey;
static int texture_key(Bank *b,unsigned index,TextureKey *key){unsigned char *t=b->meta[0]+index*16;key->file=b->file;key->clut=u32(t+8);key->pixels=u32(t+12);JgrSha256 sha;jgr_sha256_init(&sha);jgr_sha256_update(&sha,t,8);if(hash_range(b->file,key->clut,1024,&sha)||hash_range(b->file,key->pixels,u16(t)*u16(t+2),&sha))return -1;jgr_sha256_final(&sha,key->hash);return 0;}
static int append_bank(Context *c,Bank *fresh,const unsigned *owners,unsigned flags,int grow_bounds,FILE *out){
 Bank *b=&c->bank;unsigned nt=b->count[0],no=b->count[1],nb=b->count[2],nv=b->count[3],ni=b->count[4];
 if(fresh->count[0]>8192-nt||fresh->count[1]>MAX_OBJECTS-no||fresh->count[2]>MAX_BATCHES-nb||fresh->count[3]>MAX_VERTICES-nv||fresh->count[4]>MAX_INDICES-ni)return -1;
 unsigned capacity=nt+fresh->count[0],added=0,pixel_bytes=b->size-b->offset[5];
 TextureKey *keys=calloc(capacity?capacity:1,sizeof *keys);unsigned *remap=malloc((fresh->count[0]?fresh->count[0]:1)*sizeof *remap);int result=-1;
 if(!keys||!remap)goto done;
 for(unsigned i=0;i<3;i++){unsigned unit=i==1?32:16,n=(b->count[i]+fresh->count[i])*unit;unsigned char *p=realloc(b->meta[i],n?n:1);if(!p)goto done;b->meta[i]=p;}
 for(unsigned i=0;i<nt;i++)if(texture_key(b,i,keys+i))goto done;
 for(unsigned i=0;i<fresh->count[0];i++){
  TextureKey key;if(texture_key(fresh,i,&key))goto done;unsigned char *t=fresh->meta[0]+16*i;unsigned size=u16(t)*u16(t+2),slot;
  for(slot=0;slot<nt+added;slot++)if(!memcmp(keys[slot].hash,key.hash,32)&&!memcmp(b->meta[0]+16*slot,t,8)&&equal(keys[slot].file,keys[slot].clut,key.file,key.clut,1024)&&equal(keys[slot].file,keys[slot].pixels,key.file,key.pixels,size))break;
  if(slot==nt+added){unsigned char *r=b->meta[0]+slot*16;memcpy(r,t,16);w32(r+8,b->offset[5]+pixel_bytes);w32(r+12,b->offset[5]+pixel_bytes+1024);pixel_bytes+=1024+size;keys[slot]=key;added++;}
  remap[i]=slot;
 }
 for(unsigned i=0;i<fresh->count[1];i++){unsigned char *r=b->meta[1]+(no+i)*32;memcpy(r,fresh->meta[1]+i*32,32);w32(r+16,u32(r+16)+nb);w32(r+24,flags);w32(r+28,owners[i]);}
 for(unsigned i=0;i<fresh->count[2];i++){unsigned char *r=b->meta[2]+(nb+i)*16;memcpy(r,fresh->meta[2]+i*16,16);w32(r,u32(r)+nv);w32(r+4,u32(r+4)+ni);w16(r+12,u16(r+14)&4?0:remap[u16(r+12)]);}
 b->count[0]+=added;for(unsigned i=1;i<5;i++)b->count[i]+=fresh->count[i];
 unsigned sizes[6]={b->count[0]*16,b->count[1]*32,b->count[2]*16,b->count[3]*24,b->count[4]*2,0},offsets[6],at=80;unsigned char header[80];memcpy(header,b->header,80);
 for(unsigned i=0;i<6;i++){offsets[i]=at;at=align16(at+sizes[i]);w32(header+24+i*4,offsets[i]);if(i<5)w32(header+4+i*4,b->count[i]);}
 if(grow_bounds)for(unsigned i=0;i<6;i++){float a=f32(header+48+i*4),d=f32(fresh->header+48+i*4);if((i<3&&d<a)||(i>=3&&d>a))wf(header+48+i*4,d);}
 if(fseek(out,0,SEEK_SET)||write(out,header,80))goto done;
 for(unsigned i=0;i<b->count[0];i++){unsigned char t[16];memcpy(t,b->meta[0]+i*16,16);w32(t+8,u32(t+8)-b->offset[5]+offsets[5]);w32(t+12,u32(t+12)-b->offset[5]+offsets[5]);if(write(out,t,16))goto done;}
 if(pad(out,offsets[1])||write(out,b->meta[1],sizes[1])||pad(out,offsets[2])||write(out,b->meta[2],sizes[2])||pad(out,offsets[3])||copy(b->file,b->offset[3],nv*24,out)||copy(fresh->file,fresh->offset[3],fresh->count[3]*24,out)||pad(out,offsets[4])||copy(b->file,b->offset[4],ni*2,out)||copy(fresh->file,fresh->offset[4],fresh->count[4]*2,out)||pad(out,offsets[5])||copy(b->file,b->offset[5],b->size-b->offset[5],out))goto done;
 for(unsigned i=nt;i<nt+added;i++){unsigned char *t=b->meta[0]+i*16;if(copy(keys[i].file,keys[i].clut,1024,out)||copy(keys[i].file,keys[i].pixels,u16(t)*u16(t+2),out))goto done;}
 if(fflush(out))goto done;
 free_bank(b);if(load_bank(b,out))goto done;
 w32(c->ownership+20,b->count[1]);w32(c->ownership+24,b->count[2]);w32(c->ownership+28,fingerprint(out,b->offset[3]));result=0;
 done:free(keys);free(remap);return result;
}
static int near_far(Context *c){
 JgrStageProp *jobs=calloc(c->nprops?c->nprops:1,sizeof *jobs);unsigned *map=malloc((c->nprops?c->nprops:1)*sizeof *map),*owners=NULL;Bank fresh;memset(&fresh,0,sizeof fresh);int result=-1;unsigned jobs_n=0;
 if(!jobs||!map)goto done;
 for(unsigned i=0;i<c->nprops;i++)if(c->props[i].record==~0u){Prop *p=c->props+i;JgrStageProp *j=jobs+jobs_n;map[jobs_n++]=i;j->root=p->model;j->texlist=p->texlist;memcpy(j->matrix,p->matrix,sizeof j->matrix);}
 if(jobs_n){FILE *bank=temporary(c,0),*joined=temporary(c,1);if(!bank||!joined||jgr_stage_prop_bank(c->disc,c->stage,c->directory,jobs,jobs_n,bank)||fflush(bank)||load_bank(&fresh,bank))goto done;
  owners=calloc(fresh.count[1]?fresh.count[1]:1,sizeof *owners);if(!owners)goto done;unsigned before=c->bank.count[1],after=before+fresh.count[1],nr=u32(c->ownership+12);if(nr+jobs_n>8192||32*jobs_n>c->own_capacity-c->own_bytes)goto done;
  for(unsigned i=0;i<jobs_n;i++){Prop *p=c->props+map[i];JgrStageProp *j=jobs+i;if(!j->object_count||j->first_object>fresh.count[1]||j->object_count>fresh.count[1]-j->first_object)goto done;p->record=nr+i;unsigned char *r=record(c,nr+i);memset(r,0,32);w16(r,before+j->first_object);w16(r+2,j->object_count);w16(r+4,after);for(unsigned k=0;k<3;k++)wf(r+8+4*k,p->position[k]);w16(r+20,p->sector);w16(r+22,9);w32(r+28,p->address);for(unsigned oi=j->first_object;oi<j->first_object+j->object_count;oi++)owners[oi]=p->record+1;}
  w32(c->ownership+12,nr+jobs_n);c->own_bytes+=jobs_n*32;if(append_bank(c,&fresh,owners,0,1,joined))goto done;free(owners);owners=NULL;free_bank(&fresh);
 }
 if(c->stage==2){jobs_n=0;memset(jobs,0,c->nprops*sizeof *jobs);
  for(unsigned i=0;i<c->nprops;i++){Prop *p=c->props+i;if(!p->far_texlist)continue;JgrStageProp *j=jobs+jobs_n;map[jobs_n++]=i;j->root=p->far_model;j->texlist=p->far_texlist;memcpy(j->matrix,p->matrix,sizeof j->matrix);}
  FILE *bank=temporary(c,2),*joined=temporary(c,3);if(!bank||!joined||jgr_stage_prop_bank(c->disc,c->stage,c->directory,jobs,jobs_n,bank)||fflush(bank)||load_bank(&fresh,bank))goto done;
  owners=calloc(fresh.count[1]?fresh.count[1]:1,sizeof *owners);if(!owners)goto done;unsigned before=c->bank.count[1],after=before+fresh.count[1];
  for(unsigned i=0;i<c->nprops;i++){unsigned char *r=record(c,c->props[i].record);if(u16(r+6))goto done;w16(r+4,after);wf(r+24,160.*160.);}
  for(unsigned i=0;i<jobs_n;i++){Prop *p=c->props+map[i];JgrStageProp *j=jobs+i;if(j->first_object>fresh.count[1]||j->object_count>fresh.count[1]-j->first_object)goto done;if(j->object_count){unsigned char *r=record(c,p->record);w16(r+4,before+j->first_object);w16(r+6,j->object_count);}for(unsigned oi=j->first_object;oi<j->first_object+j->object_count;oi++)owners[oi]=p->record+1;}
  if(append_bank(c,&fresh,owners,2,0,joined))goto done;
 }
 result=0;
 done:free(jobs);free(map);free(owners);free_bank(&fresh);return result;
}
/* Keep traversal outside draw submission, including source eval-break. */
static int instances_for(Context *c,const Prop *p,unsigned first,unsigned count,uint32_t root,uint32_t texlist){
 typedef struct {uint32_t address;double matrix[16];} Pending;
 Pending *stack=malloc(4096*sizeof *stack);uint32_t *seen=malloc(4096*4);if(!stack||!seen){free(stack);free(seen);return -1;}unsigned pending=1,nseen=0,drawn=0;stack[0].address=root;memcpy(stack[0].matrix,p->matrix,sizeof p->matrix);int result=-1;
 while(pending){Pending item=stack[--pending];if(!item.address)continue;if(nseen>=4096)goto done;for(unsigned i=0;i<nseen;i++)if(seen[i]==item.address)goto done;seen[nseen++]=item.address;
  JgrModelNode node;if(jgr_model_node(c->disc,&c->data,SB,item.address,&node))goto done;double local[16],matrix[16];jgr_model_node_matrix(local,&node,0);jgr_model_multiply(matrix,item.matrix,local);
  if(node.model&&!(node.eval&8)){if(drawn>=count||first+drawn>=c->bank.count[1])goto done;Instance *instance=c->instances+first+drawn++;if(instance->enabled)goto done;instance->enabled=1;instance->mesh=node.model;instance->list=texlist;memcpy(instance->matrix,matrix,sizeof matrix);unsigned i;
   for(i=0;i<c->nlocals;i++)if(c->locals[i].mesh==node.model&&c->locals[i].texlist==texlist)break;
   if(i==c->nlocals){if(i>=MAX_OBJECTS)goto done;JgrStageProp *j=c->locals+c->nlocals++;j->mesh=node.model;j->texlist=texlist;jgr_model_identity(j->matrix);}instance->local=i;
  }
  if(node.sibling){if(pending>=4096)goto done;stack[pending].address=node.sibling;memcpy(stack[pending++].matrix,item.matrix,sizeof item.matrix);}
  if(node.child&&!(node.eval&16)){if(pending>=4096)goto done;stack[pending].address=node.child;memcpy(stack[pending++].matrix,matrix,sizeof matrix);}
 }
 result=drawn==count?0:-1;
 done:free(stack);free(seen);return result;
}
static int compare_topology(Bank *b,const unsigned char *object,Bank *local,const unsigned char *local_object){
 unsigned first=u32(object+16),n=u32(object+20),lf=u32(local_object+16);if(n!=u32(local_object+20))return 0;
 for(unsigned i=0;i<n;i++){unsigned char *a=b->meta[2]+(first+i)*16,*d=local->meta[2]+(lf+i)*16;unsigned vn=u16(a+8),ni=u16(a+10);if(vn!=u16(d+8)||ni!=u16(d+10)||!equal(b->file,b->offset[4]+u32(a+4)*2,local->file,local->offset[4]+u32(d+4)*2,ni*2))return 0;
  /* Only UVs decide whether original local positions can be reused. Read
   * bounded batches of complete records instead of seeking both scratch
   * streams for every eight-byte UV pair; position/color differences remain
   * deliberately irrelevant to this topology comparison. */
  unsigned char world[64*24],original[64*24];
  for(unsigned at=0;at<vn;){unsigned count=vn-at;if(count>64)count=64;
   if(read_at(b->file,b->offset[3]+(u32(a)+at)*24,world,count*24)||read_at(local->file,local->offset[3]+(u32(d)+at)*24,original,count*24))return 0;
   for(unsigned v=0;v<count;v++)if(memcmp(world+v*24,original+v*24,8))return 0;
   at+=count;
  }
 }
 return 1;
}
static void transformed(const double m[16],const double p[3],double out[3]){for(unsigned k=0;k<3;k++)out[k]=m[k*4]*p[0]+m[k*4+1]*p[1]+m[k*4+2]*p[2]+m[k*4+3];}
static int localize(Context *c,Bank *local,FILE *storage,LocalBatch *map,unsigned char *instance_records){
 Bank *b=&c->bank;unsigned char *vertices=NULL,*local_vertices=NULL;double *points=NULL;unsigned capacity=0,local_capacity=0,point_capacity=0;int result=-1;
 for(unsigned oi=0;oi<b->count[1];oi++){Instance *instance=c->instances+oi;if(!instance->enabled)continue;JgrStageProp *j=c->locals+instance->local;if(j->object_count!=1||j->first_object>=local->count[1])goto done;
  unsigned char *object=b->meta[1]+oi*32,*lo=local->meta[1]+j->first_object*32;int direct=compare_topology(b,object,local,lo);double inverse[16],stored[16];jgr_model_identity(stored);if(!direct&&jgr_model_inverse(inverse,instance->matrix))goto done;
  unsigned char *ir=instance_records+oi*64;for(unsigned k=0;k<12;k++){wf(ir+k*4,instance->matrix[k]);stored[k]=f32(ir+k*4);}unsigned npoints=0,first=u32(object+16),n=u32(object+20),lf=u32(lo+16);
  for(unsigned k=0;k<n;k++){unsigned bi=first+k;unsigned char *row=b->meta[2]+bi*16;unsigned vn=u16(row+8);if(map[bi].present||!vn)goto done;
   if(vn>capacity){unsigned char *p=realloc(vertices,vn*24);if(!p)goto done;vertices=p;capacity=vn;}
   if(read_at(b->file,b->offset[3]+u32(row)*24,vertices,vn*24))goto done;
   if(direct){if(vn>local_capacity){unsigned char *p=realloc(local_vertices,vn*24);if(!p)goto done;local_vertices=p;local_capacity=vn;}unsigned char *lb=local->meta[2]+(lf+k)*16;if(read_at(local->file,local->offset[3]+u32(lb)*24,local_vertices,vn*24))goto done;}
   if(vn>MAX_VERTICES-npoints)goto done;
   if(npoints+vn>point_capacity){unsigned next=point_capacity?point_capacity:1024;while(next<npoints+vn)next*=2;if(next>MAX_VERTICES)goto done;double *p=realloc(points,next*3*sizeof(double));if(!p)goto done;points=p;point_capacity=next;}
   for(unsigned v=0;v<vn;v++){double world[3],point[3],reconstructed[3];for(unsigned axis=0;axis<3;axis++){world[axis]=f32(vertices+v*24+12+axis*4);if(!isfinite(world[axis]))goto done;}
    if(direct)for(unsigned axis=0;axis<3;axis++)point[axis]=f32(local_vertices+v*24+12+axis*4);else transformed(inverse,world,point);
    for(unsigned axis=0;axis<3;axis++){wf(vertices+v*24+12+axis*4,point[axis]);point[axis]=f32(vertices+v*24+12+axis*4);if(!isfinite(point[axis]))goto done;points[(npoints+v)*3+axis]=point[axis];}transformed(stored,point,reconstructed);for(unsigned axis=0;axis<3;axis++)if(fabs(reconstructed[axis]-world[axis])>=.01)goto done;
   }
   long at=ftell(storage);if(at<0||(unsigned long)at>MAX_VERTICES*24||write(storage,vertices,vn*24))goto done;map[bi].present=1;map[bi].offset=at;npoints+=vn;
  }
  if(!npoints)goto done;
  double center[3]={0},radius=0;for(unsigned v=0;v<npoints;v++)for(unsigned k=0;k<3;k++)center[k]+=points[v*3+k];for(unsigned k=0;k<3;k++)center[k]/=npoints;
  for(unsigned v=0;v<npoints;v++){double squared=0;for(unsigned k=0;k<3;k++){double d=points[v*3+k]-center[k];squared+=d*d;}if(squared>radius)radius=squared;}
  for(unsigned k=0;k<3;k++)wf(ir+48+k*4,center[k]);
  wf(ir+60,sqrt(radius)+.001);w32(object+24,u32(object+24)|4);
 }
 result=fflush(storage)?-1:0;
 done:free(vertices);free(local_vertices);free(points);return result;
}
static int order_range(const void *a,const void *b){const Range *x=a,*y=b;return x->start<y->start?-1:x->start>y->start?1:x->end<y->end?-1:x->end>y->end;}
static int retained(Bank *b,const LocalBatch *locals,unsigned field,FILE *out,Range **ranges,unsigned *count,unsigned *size){
 Range *r=malloc((b->count[2]?b->count[2]:1)*sizeof *r);if(!r)return -1;unsigned n=0;
 for(unsigned i=0;i<b->count[2];i++)if(!locals[i].present){const unsigned char *row=b->meta[2]+i*16;unsigned start=u32(row+field*4),num=u16(row+8+field*2);if(num)r[n++]=(Range){start,start+num,0};}
 qsort(r,n,sizeof *r,order_range);unsigned merged=0;
 for(unsigned i=0;i<n;i++){if(merged&&r[i].start<=r[merged-1].end){if(r[i].end>r[merged-1].end)r[merged-1].end=r[i].end;}else r[merged++]=r[i];}
 unsigned total=0,unit=field?2:24;
 for(unsigned i=0;i<merged;i++){if(field&&((total-r[i].start)&1)){if(fputc(0,out)==EOF||fputc(0,out)==EOF){free(r);return -1;}total++;}r[i].output=total;unsigned length=r[i].end-r[i].start;if(copy(b->file,b->offset[3+field]+r[i].start*unit,length*unit,out)){free(r);return -1;}total+=length;}
 *ranges=r;*count=merged;*size=total;return 0;
}
static int remap_range(const Range *r,unsigned n,unsigned start,unsigned count,unsigned *out){if(!count){*out=0;return 0;}unsigned low=0,high=n;while(low<high){unsigned mid=low+(high-low)/2;if(r[mid].start<=start)low=mid+1;else high=mid;}if(!low||start+count>r[low-1].end)return -1;*out=r[low-1].output+start-r[low-1].start;return 0;}
static int share_geometry(Context *c,FILE *out){
 Bank *b=&c->bank,local;memset(&local,0,sizeof local);Range *ranges[2]={NULL,NULL};unsigned range_count[2]={0},sizes[2]={0};int result=-1;
 LocalBatch *locals=calloc(b->count[2]?b->count[2]:1,sizeof *locals);unsigned char *instances=calloc(b->count[1]?b->count[1]:1,64),*extension=NULL;Shared *shared=calloc(b->count[2]?b->count[2]:1,sizeof *shared);unsigned nshared=0;
 if(!locals||!instances||!shared)goto done;
 c->instances=calloc(b->count[1]?b->count[1]:1,sizeof *c->instances);c->locals=calloc(MAX_OBJECTS,sizeof *c->locals);if(!c->instances||!c->locals)goto done;
 for(unsigned i=0;i<c->nprops;i++){Prop *p=c->props+i;unsigned char *r=record(c,p->record);if(instances_for(c,p,u16(r),u16(r+2),p->model,p->texlist))goto done;if(u16(r+6)&&instances_for(c,p,u16(r+4),u16(r+6),p->far_model,p->far_texlist))goto done;}
 FILE *local_file=temporary(c,4),*vertex_file=temporary(c,5),*index_file=temporary(c,6),*local_storage=temporary(c,7);if(!local_file||!vertex_file||!index_file||!local_storage||jgr_stage_prop_bank(c->disc,c->stage,c->directory,c->locals,c->nlocals,local_file)||fflush(local_file)||load_bank(&local,local_file)||localize(c,&local,local_storage,locals,instances))goto done;
 for(unsigned field=0;field<2;field++)if(retained(b,locals,field,field?index_file:vertex_file,ranges+field,range_count+field,sizes+field))goto done;
 if(sizes[1]&1){if(fputc(0,index_file)==EOF||fputc(0,index_file)==EOF)goto done;sizes[1]++;}
 for(unsigned bi=0;bi<b->count[2];bi++){unsigned char *row=b->meta[2]+bi*16;unsigned vo=u32(row),io=u32(row+4),vn=u16(row+8),ix=u16(row+10);
  if(!locals[bi].present){unsigned newvo,newio;if(remap_range(ranges[0],range_count[0],vo,vn,&newvo)||remap_range(ranges[1],range_count[1],io,ix,&newio))goto done;w32(row,newvo);w32(row+4,newio);continue;}
  Shared key;memset(&key,0,sizeof key);key.vn=vn;key.ix=ix;key.tex=u16(row+12);key.flags=u16(row+14);JgrSha256 sha;jgr_sha256_init(&sha);if(hash_range(local_storage,locals[bi].offset,vn*24,&sha)||hash_range(b->file,b->offset[4]+io*2,ix*2,&sha))goto done;jgr_sha256_final(&sha,key.hash);unsigned at;
  for(at=0;at<nshared;at++){Shared *s=shared+at;if(s->vn==vn&&s->ix==ix&&s->tex==key.tex&&s->flags==key.flags&&!memcmp(s->hash,key.hash,32)&&equal(local_storage,locals[bi].offset,vertex_file,s->vo*24,vn*24)&&equal(b->file,b->offset[4]+io*2,index_file,s->io*2,ix*2))break;}
  if(at==nshared){if(vn>MAX_VERTICES-sizes[0]||ix>MAX_INDICES-sizes[1])goto done;key.vo=sizes[0];key.io=sizes[1];if(fseek(vertex_file,0,SEEK_END)||fseek(index_file,0,SEEK_END)||copy(local_storage,locals[bi].offset,vn*24,vertex_file)||copy(b->file,b->offset[4]+io*2,ix*2,index_file))goto done;sizes[0]+=vn;sizes[1]+=ix;if(sizes[1]&1){if(fputc(0,index_file)==EOF||fputc(0,index_file)==EOF)goto done;sizes[1]++;}shared[nshared++]=key;}
  w32(row,shared[at].vo);w32(row+4,shared[at].io);
 }
 unsigned prefix=align16(16+b->count[1]*2),unique=0;extension=calloc(1,prefix+b->count[1]*64);if(!extension)goto done;
 for(unsigned oi=0;oi<b->count[1];oi++)if(c->instances[oi].enabled){unsigned i;for(i=0;i<unique;i++)if(!memcmp(extension+prefix+i*64,instances+oi*64,64))break;if(i==unique){memcpy(extension+prefix+unique*64,instances+oi*64,64);unique++;}w16(extension+16+oi*2,i+1);}
 if(unique){memcpy(extension,"JGI1",4);w32(extension+4,b->count[1]);w32(extension+8,unique);}
 if(fflush(vertex_file)||fflush(index_file)||finish_bank(b,vertex_file,sizes[0],index_file,sizes[1],extension,unique?prefix+unique*64:0,out))goto done;
 unsigned char header[40];if(read_at(out,0,header,40))goto done;w32(c->ownership+28,fingerprint(out,u32(header+36)));result=0;
 done:free_bank(&local);free(ranges[0]);free(ranges[1]);free(locals);free(instances);free(extension);free(shared);return result;
}
int jgr_stage_props(JgrDisc *disc,unsigned stage,const char *directory,FILE *input,FILE *meta,FILE *out,FILE *out_meta){
 if(!disc||!directory||!input||!meta||!out||!out_meta||(stage!=1&&stage!=2&&stage!=3&&stage!=5))return -1;
 FILE *streams[4]={input,meta,out,out_meta};for(unsigned i=0;i<4;i++)for(unsigned j=i+1;j<4;j++)if(streams[i]==streams[j])return -1;
 Context c;memset(&c,0,sizeof c);c.disc=disc;c.stage=stage;c.directory=directory;int result=-1;char source_name[80];snprintf(source_name,sizeof source_name,"decompile/afs/STAGE%u/000_st%udat.bin",stage,stage);
 c.scratch=jgr_scratch_begin();if(!c.scratch)return -1;
 JgrStageStatsScope stats_scope;jgr_stage_stats_begin(&stats_scope,stage,JGR_STAGE_JOB_PROPS);
 if(jgr_disc_find(disc,"1ST_READ.BIN",&c.code)||jgr_disc_source(disc,source_name,&c.data)||load_bank(&c.bank,input)||read_placements(&c)||ownership(&c,meta))goto done;
 jgr_stage_stats_phase(JGR_STAGE_PHASE_POSTPROCESS);
 if(near_far(&c)||share_geometry(&c,out)||write(out_meta,c.ownership,c.own_bytes)||fflush(out_meta))goto done;
 result=0;
 done:{int failure_errno=errno;for(unsigned i=0;i<TEMP_COUNT;i++)if(c.temp[i]){if(fclose(c.temp[i])&&!result){result=-1;failure_errno=errno;}char p[1024];snprintf(p,sizeof p,"%s/props-setup-%u.tmp",directory,i);remove(p);}free_bank(&c.bank);free(c.props);free(c.ownership);free(c.instances);free(c.locals);if(jgr_scratch_end()&&!result){result=-1;failure_errno=errno;}jgr_stage_stats_end(&stats_scope,result);if(result)errno=failure_errno;return result;}
}
/* Complete city pair; intermediate files are private setup scratch only. */
int jgr_stage_city(JgrDisc *disc,unsigned stage,const char *directory,FILE *out,FILE *owners){
 if(!disc||!directory||!out||!owners||out==owners)return -1;
 if(stage==6)return jgr_stage_city_geometry(disc,stage,directory,out,owners);
 if(stage!=1&&stage!=2&&stage!=3&&stage!=5)return -1;
 char names[2][1024];FILE *files[2]={NULL,NULL};int result=-1;
 JgrScratchPool *scratch=jgr_scratch_begin();if(!scratch)return -1;
 JgrStageStatsScope stats_scope;jgr_stage_stats_begin(&stats_scope,stage,JGR_STAGE_JOB_CITY);
 for(unsigned i=0;i<2;i++){
  int n=snprintf(names[i],sizeof names[i],"%s/city-props-input-%u.tmp",directory,i);if(n<0||(size_t)n>=sizeof names[i])goto done;struct stat st;
#ifdef __PSP__
  int exists=stat(names[i],&st);
#else
  int exists=lstat(names[i],&st);
#endif
  if((!exists&&!S_ISREG(st.st_mode))||(exists&&errno!=ENOENT))goto done;
  files[i]=jgr_scratch_open(scratch,names[i]);if(!files[i])goto done;
 }
 if(jgr_stage_city_geometry(disc,stage,directory,files[0],files[1])||fflush(files[0])||fflush(files[1])||jgr_stage_props(disc,stage,directory,files[0],files[1],out,owners))goto done;
 result=0;
 done:{int failure_errno=errno;for(unsigned i=0;i<2;i++)if(files[i]){if(fclose(files[i])&&!result){result=-1;failure_errno=errno;}remove(names[i]);}if(jgr_scratch_end()&&!result){result=-1;failure_errno=errno;}jgr_stage_stats_end(&stats_scope,result);if(result)errno=failure_errno;return result;}
}
