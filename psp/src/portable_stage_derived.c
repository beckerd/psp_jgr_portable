/* Derived first-launch sidecars. No original geometry, polygons or pixels are
 * compiled here; every result is rebuilt from generated assets/source media. */
#include "portable_stage_derived.h"
#include "portable_indexed.h"
#include "portable_convert.h"
#ifdef __PSP__
#include "portable_scratch.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <limits.h>
#include "stage_texture_usage.inc"
#define DERIVED_MAX_TEXTURES 4096u
#define DERIVED_MAX_OBJECTS 8191u
#define DERIVED_MAX_BATCHES 65536u
#define DERIVED_MAX_VERTICES 1048576u
#define DERIVED_MAX_INDICES 2097152u
#define MIP_BUDGET 32768u

typedef struct {FILE *file;uint32_t size,nt,no,nb,nv,ni,ot,oo,ob,ov,oi,op,key;int error;} Derived;
static uint32_t der_word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned der_half(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static void der_encode(unsigned char *p,uint32_t v)
{p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static void der_float(unsigned char *p,double value)
{float f=(float)value;uint32_t bits;memcpy(&bits,&f,4);der_encode(p,bits);}
static double der_real(const unsigned char *p)
{uint32_t bits=der_word(p);float f;memcpy(&f,&bits,4);return f;}
static uint32_t der_fnv(const unsigned char *p,size_t n,uint32_t h)
{while(n--)h=(h^*p++)*16777619u;return h;}
static int der_span(const Derived *d,uint32_t offset,uint32_t n)
{return offset<=d->size && n<=d->size-offset;}
static int der_read(Derived *d,uint32_t offset,void *data,uint32_t n)
{
    if(!der_span(d,offset,n) || fseek(d->file,(long)offset,SEEK_SET) || fread(data,1,n,d->file)!=n) {
        d->error=1;memset(data,0,n);return -1;
    }return 0;
}
static int der_hash(Derived *d,uint32_t offset,uint32_t n,JgrSha256 *sha,uint32_t *fnv)
{
    unsigned char buffer[4096];while(n) {
        unsigned bytes=n<sizeof buffer ? n:sizeof buffer;if(der_read(d,offset,buffer,bytes))return -1;
        if(sha)jgr_sha256_update(sha,buffer,bytes);
        if(fnv)*fnv=der_fnv(buffer,bytes,*fnv);
        offset+=bytes;n-=bytes;
    }return 0;
}
static int der_open(Derived *d,FILE *file)
{
    unsigned char h[80];memset(d,0,sizeof *d);d->file=file;
    if(!file || fseek(file,0,SEEK_END))return -1;
    long size=ftell(file);
    if(size<80 || (unsigned long)size>INT32_MAX)return -1;
    d->size=(uint32_t)size;
    if(der_read(d,0,h,sizeof h) || memcmp(h,"JGS1",4))return -1;
    d->nt=der_word(h+4);d->no=der_word(h+8);d->nb=der_word(h+12);d->nv=der_word(h+16);d->ni=der_word(h+20);
    d->ot=der_word(h+24);d->oo=der_word(h+28);d->ob=der_word(h+32);d->ov=der_word(h+36);d->oi=der_word(h+40);d->op=der_word(h+44);
    if(!d->nt || d->nt>DERIVED_MAX_TEXTURES || !d->no || d->no>DERIVED_MAX_OBJECTS || d->nb>DERIVED_MAX_BATCHES ||
       d->nv>DERIVED_MAX_VERTICES || d->ni>DERIVED_MAX_INDICES || d->ot<80 || d->ot>d->oo || d->oo>d->ob || d->ob>d->ov || d->ov>d->oi || d->oi>d->op ||
       d->nt*16>d->oo-d->ot || d->no*32>d->ob-d->oo || d->nb*16>d->ov-d->ob || d->nv*24>d->oi-d->ov || d->ni*2>d->op-d->oi || d->op>d->size)return -1;
    d->key=2166136261u;return der_hash(d,0,d->ov,NULL,&d->key);
}
static int der_write(FILE *out,const void *p,size_t n){return fwrite(p,1,n,out)==n ? 0:-1;}
static int der_put(FILE *out,uint32_t n){unsigned char p[4];der_encode(p,n);return der_write(out,p,4);}

/* Authored mip selection retains the measured port residency weights. These
 * scores are performance calibration, not source texture/pixel payload. */
typedef struct {
    unsigned width,height,flags,eligible,matched,ambiguous,quality;
    uint32_t clut,pixels,fnv,weight;unsigned char sha[32],*lower;unsigned cost,first,last;
} MipTexture;
typedef struct {JgrDiscFile file;char archive[64];unsigned member;} MipPack;
static int mip_packs(JgrDisc *disc,unsigned stage,MipPack packs[128],unsigned *count)
{
    JgrDiscFile exe;unsigned char *code=NULL;char (*names)[64]=NULL;unsigned names_count=0;int result=-1;*count=0;
    if(jgr_disc_source(disc,"decompile/extracted/1ST_READ.BIN",&exe) || exe.size>4u*1024u*1024u)return -1;
    code=malloc(exe.size);names=calloc(1024,64);if(!code || !names || jgr_disc_read(disc,&exe,0,code,exe.size))goto done;
    for(unsigned i=0;i<exe.size;) {
        unsigned begin=i;while(i<exe.size && ((code[i]>='A'&&code[i]<='Z')||(code[i]>='0'&&code[i]<='9')||code[i]=='_'))i++;
        unsigned n=i-begin;
        if(n && n<64 && i+4<=exe.size && !memcmp(code+i,".AFS",4)) {
            if(names_count==1024)goto done;
            memcpy(names[names_count++],code+begin,n);i+=4;
        }else if(i<exe.size)i++;
    }
    char prefix[24];snprintf(prefix,sizeof prefix,"STAGE%uTXP",stage);
    for(unsigned at=0;at+64<exe.size;at+=4) {
        unsigned id=der_word(code+at),member=der_word(code+at+4),base=der_word(code+at+8),list=der_word(code+at+12);
        if(der_word(code+at+20)!=1 || id>=names_count || member>=256 || base<0x8c800000u || base>=0x8d000000u ||
           list<0x8c010000u || list-0x8c010000u>=exe.size || strncmp(names[id],prefix,strlen(prefix)))continue;
        unsigned i;for(i=0;i<*count;i++)if(packs[i].member==member && !strcmp(packs[i].archive,names[id]))break;
        if(i<*count)continue;
        if(*count==128)goto done;
        MipPack *p=packs+(*count)++;memcpy(p->archive,names[id],64);p->member=member;
    }
    if(!*count && (stage==5 || stage==6)) {
        *count=1;snprintf(packs[0].archive,sizeof packs[0].archive,"STAGE%u",stage);packs[0].member=1;
    }
    for(unsigned i=0;i<*count;i++) {
        JgrDiscFile archive;char path[96];snprintf(path,sizeof path,"JETRADIO/%s.AFS",packs[i].archive);
        if(jgr_disc_find(disc,path,&archive) || jgr_disc_afs(disc,&archive,packs[i].member,&packs[i].file))goto done;
    }
    result=*count ? 0:-1;
 done:free(code);free(names);return result;
}
/* Capture only fingerprints, never encoded/source pixels. Small optional slabs
 * let a low-memory install retain a partial cache and fall back on every miss. */
#define MIP_IDENTITY_LIMIT 2048u
#define MIP_ALIAS_LIMIT 8192u
typedef struct {
    uint64_t offset;uint32_t size,gbix;uint16_t target,opaque;unsigned char sha[32];
} MipIdentity;
typedef struct MipIdentities {struct MipIdentities *next;unsigned count;MipIdentity rows[128];} MipIdentities;
static struct {JgrDisc *disc;MipIdentities *slabs;unsigned count;} mip_cache;
#ifdef JGR_STAGE_MIPS_TESTING
static JgrStageMipsStats mip_stats;
static unsigned mip_identity_limit=MIP_IDENTITY_LIMIT,mip_alias_limit=MIP_ALIAS_LIMIT;
#define MIP_COUNT(field) (mip_stats.field++)
void jgr_stage_mips_test_limits(unsigned identities,unsigned aliases)
{mip_identity_limit=identities<MIP_IDENTITY_LIMIT?identities:MIP_IDENTITY_LIMIT;mip_alias_limit=aliases<MIP_ALIAS_LIMIT?aliases:MIP_ALIAS_LIMIT;}
void jgr_stage_mips_stats(JgrStageMipsStats *stats){if(stats)*stats=mip_stats;}
void jgr_stage_mips_stats_reset(void){memset(&mip_stats,0,sizeof mip_stats);}
#else
#define mip_identity_limit MIP_IDENTITY_LIMIT
#define mip_alias_limit MIP_ALIAS_LIMIT
#define MIP_COUNT(field) ((void)0)
#endif
void jgr_stage_mips_cache_end(void)
{while(mip_cache.slabs){MipIdentities *next=mip_cache.slabs->next;free(mip_cache.slabs);mip_cache.slabs=next;}memset(&mip_cache,0,sizeof mip_cache);}
void jgr_stage_mips_cache_begin(JgrDisc *disc)
{jgr_stage_mips_cache_end();mip_cache.disc=disc;}
static MipIdentity *mip_identity(JgrDisc *disc,const JgrDiscFile *file,uint32_t gbix,unsigned target)
{
    if(!disc || mip_cache.disc!=disc)return NULL;
    for(MipIdentities *s=mip_cache.slabs;s;s=s->next)for(unsigned i=0;i<s->count;i++) {
        MipIdentity *r=s->rows+i;if(r->offset==file->offset && r->size==file->size && r->gbix==gbix && r->target==target)return r;
    }
    return NULL;
}
static void mip_remember(JgrDisc *disc,const JgrDiscFile *file,uint32_t gbix,unsigned target,unsigned opaque,const unsigned char sha[32])
{
    if(!disc || mip_cache.disc!=disc || mip_cache.count>=mip_identity_limit || mip_identity(disc,file,gbix,target))return;
    MipIdentities *s=mip_cache.slabs;
    if(!s || s->count==128) {
#ifdef __PSP__
        if(jgr_scratch_heap_available()<8u*1024u*1024u+sizeof *s)return;
#endif
        s=malloc(sizeof *s);if(!s)return;s->next=mip_cache.slabs;s->count=0;mip_cache.slabs=s;
    }
    MipIdentity *r=s->rows+s->count++;r->offset=file->offset;r->size=file->size;r->gbix=gbix;r->target=(uint16_t)target;r->opaque=(uint16_t)opaque;
    if(opaque)memcpy(r->sha,sha,32);else memset(r->sha,0,32);mip_cache.count++;MIP_COUNT(captured);
}
void jgr_stage_mips_capture(JgrDisc *disc,const JgrStageTextureSource *source,const unsigned char *rgba,FILE *encoded,void *user)
{
    (void)user;
    if(!source || !rgba || !encoded || mip_cache.disc!=disc || mip_cache.count>=mip_identity_limit ||
       source->width!=source->height || source->width<64 || source->width>256 || (source->width&(source->width-1)) ||
       source->source_width!=source->source_height || source->source_width<source->width || source->source_width>1024 ||
       source->gbix>=source->file.size || mip_identity(disc,&source->file,source->gbix,source->width))return;
    unsigned opaque=1;for(unsigned i=0;i<source->source_width*source->source_height;i++)if(rgba[i*4+3]!=255){opaque=0;break;}
    unsigned char digest[32];JgrSha256 sha;jgr_sha256_init(&sha);
    if(opaque) {
        unsigned char buffer[4096];uint32_t offsets[2]={source->clut,source->pixels},sizes[2]={1024,source->width*source->height};
        for(unsigned part=0;part<2;part++) {
            if(fseek(encoded,(long)offsets[part],SEEK_SET))return;
            unsigned left=sizes[part];while(left){unsigned n=left<sizeof buffer?left:sizeof buffer;if(fread(buffer,1,n,encoded)!=n)return;jgr_sha256_update(&sha,buffer,n);left-=n;}
        }
        jgr_sha256_final(&sha,digest);
    }
    mip_remember(disc,&source->file,source->gbix,source->width,opaque,digest);
}
static unsigned mip_spread10(unsigned n)
{n&=0x3ffu;n=(n|(n<<8))&0x00ff00ffu;n=(n|(n<<4))&0x0f0f0f0fu;n=(n|(n<<2))&0x33333333u;return (n|(n<<1))&0x55555555u;}
static unsigned mip_twiddle(unsigned x,unsigned y)
{return mip_spread10(y)|(mip_spread10(x)<<1);}
static unsigned char *mip_decode(const unsigned char *payload,unsigned bytes,unsigned pf,unsigned df,unsigned original,unsigned size)
{
    if(pf>2 || !size || original>1024 || (original&(original-1)) || size>original || (size&(size-1)) ||
       (df!=2 && df!=4 && df!=0x11 && df!=0x12))return NULL;
    unsigned entries=df==4 ? 256:original<=16 ? 16:original<=32 ? 64:256;
    unsigned offset=(df==2 || df==0x12) ? 2:entries*8;
    for(unsigned s=1;s<size;s*=2)offset+=(df==2 || df==0x12) ? 2*s*s:(s*s/4 ? s*s/4:1);
    unsigned char *pixels=malloc(size*size*4);if(!pixels)return NULL;
    for(unsigned y=0;y<size;y++)for(unsigned x=0;x<size;x++) {
        unsigned at;
        if(df==2 || df==0x12)at=offset+2*mip_twiddle(x,y);
        else {
            unsigned index=offset+mip_twiddle(x/2,y/2);if(size<2 || index>=bytes || payload[index]>=entries)goto fail;
            at=payload[index]*8+2*((x&1)*2+(y&1));
        }
        if(at>=bytes || bytes-at<2)goto fail;
        unsigned v=der_half(payload+at);unsigned char *p=pixels+(y*size+x)*4;
        if(pf==1){p[0]=((v>>11)&31)*255/31;p[1]=((v>>5)&63)*255/63;p[2]=(v&31)*255/31;p[3]=255;}
        else if(!pf){p[0]=((v>>10)&31)*255/31;p[1]=((v>>5)&31)*255/31;p[2]=(v&31)*255/31;p[3]=(v>>15)*255;}
        else{p[0]=((v>>8)&15)*17;p[1]=((v>>4)&15)*17;p[2]=(v&15)*17;p[3]=((v>>12)&15)*17;}
    }
    return pixels;
 fail:free(pixels);return NULL;
}
typedef struct {uint32_t key;unsigned index;} MipColor;
static int mip_quantize(const unsigned char *rgba,unsigned size,const unsigned char palette[1024],unsigned char *out,int *quality)
{
    MipColor *cache=calloc(32768,sizeof *cache);uint64_t error=0;if(!cache)return -1;
    for(unsigned y=0;y<size;y++)for(unsigned x=0;x<size;x++) {
        const unsigned char *p=rgba+(y*size+x)*4;if(p[3]!=255){free(cache);return 1;}
        uint32_t key=1u<<24|p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16;unsigned slot=(key*2654435761u)&32767;
        while(cache[slot].key && cache[slot].key!=key)slot=(slot+1)&32767;
        unsigned index;
        if(cache[slot].key)index=cache[slot].index;
        else {
            unsigned best=UINT_MAX;index=0;
            for(unsigned i=0;i<256;i++)if(palette[i*4+3]==255) {
                unsigned distance=0;for(unsigned c=0;c<3;c++){int d=(int)p[c]-palette[i*4+c];distance+=(unsigned)(d*d);}
                if(distance<best){best=distance;index=i;}
            }
            if(best==UINT_MAX){free(cache);return 1;}cache[slot]=(MipColor){key,index};
        }
        for(unsigned c=0;c<3;c++){int d=(int)p[c]-palette[index*4+c];error+=(uint64_t)(d*d);}
        out[((y/8)*(size/16)+x/16)*128+(y%8)*16+x%16]=(unsigned char)index;
    }
    *quality=error<=144ull*size*size*3;free(cache);return 0;
}
typedef struct {unsigned pf,df,width,bytes,offset;} MipSource;
typedef struct {uint32_t gbix;unsigned pack,next;} MipAlias;
typedef struct {MipAlias *rows;unsigned count,capacity;} MipAliases;
static int mip_header(JgrDisc *disc,const JgrDiscFile *file,uint32_t gbix,MipSource *source)
{
    if(gbix>file->size)return -1;
    unsigned char header[80];unsigned n=file->size-gbix;if(n>sizeof header)n=sizeof header;
    if(jgr_disc_read(disc,file,gbix,header,n))return -1;
    unsigned pv=8;while(pv+16<=n && pv<64 && memcmp(header+pv,"PVRT",4))pv++;
    if(pv+16>n || pv>=64)return -1;
    unsigned df=header[pv+9],pf=header[pv+8],width=der_half(header+pv+12),height=der_half(header+pv+14),length=der_word(header+pv+4);
    if(df!=2 && df!=4 && df!=0x11 && df!=0x12)return 0;
    if(pf>2 || !width || width!=height || width>1024 || (width&(width-1)) || length<8)return -1;
    unsigned bytes=length-8;if(bytes>file->size-gbix-pv-16)bytes=file->size-gbix-pv-16;
    if(bytes>3u*1024u*1024u)return -1;
    *source=(MipSource){pf,df,width,bytes,gbix+pv+16};return 1;
}
static unsigned char *mip_raw(JgrDisc *disc,const JgrDiscFile *file,const MipSource *s)
{unsigned char *raw=malloc(s->bytes?s->bytes:1);if(raw && jgr_disc_read(disc,file,s->offset,raw,s->bytes)){free(raw);raw=NULL;}return raw;}
static int mip_alias(MipAliases *aliases,MipTexture *texture,unsigned pack,uint32_t gbix)
{
    if(aliases->count==aliases->capacity) {
        unsigned n=aliases->capacity?aliases->capacity*2:64;if(n>mip_alias_limit)n=mip_alias_limit;
        if(n<=aliases->capacity)return -1;
        MipAlias *rows=realloc(aliases->rows,n*sizeof *rows);if(!rows)return -1;aliases->rows=rows;aliases->capacity=n;
    }
    unsigned id=++aliases->count;aliases->rows[id-1]=(MipAlias){gbix,pack,0};
    if(texture->last)aliases->rows[texture->last-1].next=id;else texture->first=id;texture->last=id;MIP_COUNT(aliases);return 0;
}
/* Match every source before ranking. Even a second identical base image can
 * carry conflicting authored lower levels, so all its aliases remain visible. */
static int mip_lower(const unsigned char *raw,const MipSource *s,const unsigned char palette[1024],MipTexture *t)
{
    unsigned char *lower=malloc(t->cost);int quality=1,opaque=1;if(!lower)return -1;unsigned offset=0;MIP_COUNT(lower_candidates);
    for(unsigned level=1;level<=2;level++) {
        unsigned extent=t->width>>level;unsigned char *pixels=mip_decode(raw,s->bytes,s->pf,s->df,s->width,extent);int good=0;
        if(!pixels){free(lower);return -1;}
        int status=mip_quantize(pixels,extent,palette,lower+offset,&good);free(pixels);
        if(status<0){free(lower);return -1;}if(status>0)opaque=0;if(!good)quality=0;offset+=extent*extent;
    }
    if(opaque) {
        if(t->matched){if(memcmp(lower,t->lower,t->cost))t->ambiguous=1;free(lower);}
        else {t->matched=1;t->lower=lower;t->quality=(unsigned)quality;}
    }else free(lower);
    return 0;
}
static int mip_source(JgrDisc *disc,const JgrDiscFile *file,uint32_t gbix,Derived *jgs,MipTexture *textures,unsigned pack,MipAliases *aliases)
{
    MipSource s;int status=mip_header(disc,file,gbix,&s);if(status<=0)return status;
    unsigned char *raw=NULL,*image=NULL,*encoded=NULL;int result=-1;MIP_COUNT(sources);
    for(unsigned target=64;target<=256 && target<=s.width;target*=2) {
        unsigned needed=0;for(unsigned i=0;i<jgs->nt;i++)if(textures[i].eligible && textures[i].width==target)needed=1;
        if(!needed)continue;
        unsigned char digest[32];MipIdentity *identity=mip_identity(disc,file,gbix,target);
        if(identity) {MIP_COUNT(identity_hits);if(!identity->opaque){result=0;goto done;}memcpy(digest,identity->sha,32);}
        else {
            if(!raw){raw=mip_raw(disc,file,&s);if(!raw)goto done;}
            if(!image) {
                image=mip_decode(raw,s.bytes,s.pf,s.df,s.width,s.width);if(!image)goto done;
                for(unsigned i=0;i<s.width*s.width;i++)if(image[i*4+3]!=255){mip_remember(disc,file,gbix,target,0,NULL);result=0;goto done;}
            }
            unsigned size=1024+target*target,flags;encoded=malloc(size);if(!encoded)goto done;MIP_COUNT(base_encodes);
            if(jgr_indexed_memory(image,s.width,s.width,target,target,encoded,size,&flags))goto done;
            JgrSha256 sha;jgr_sha256_init(&sha);jgr_sha256_update(&sha,encoded,size);jgr_sha256_final(&sha,digest);
            mip_remember(disc,file,gbix,target,1,digest);
        }
        for(unsigned i=0;i<jgs->nt;i++)if(textures[i].eligible && textures[i].width==target && !memcmp(textures[i].sha,digest,32)) {
            if(aliases){if(mip_alias(aliases,textures+i,pack,gbix)){result=1;goto done;}}
            else {
                unsigned char palette[1024];if(der_read(jgs,textures[i].clut,palette,1024))goto done;
                if(!raw){raw=mip_raw(disc,file,&s);if(!raw)goto done;}
                if(mip_lower(raw,&s,palette,textures+i))goto done;
            }
        }
        free(encoded);encoded=NULL;
    }
    result=0;
 done:free(raw);free(image);free(encoded);return result;
}
static int mip_scan(JgrDisc *disc,MipPack *packs,unsigned npacks,Derived *d,MipTexture *textures,MipAliases *aliases)
{
    for(unsigned pack=0;pack<npacks;pack++) {
        const JgrDiscFile *file=&packs[pack].file;unsigned char buffer[4096];uint32_t at=0;unsigned count=0;
        while(at+4<=file->size) {
            unsigned n=file->size-at;if(n>sizeof buffer)n=sizeof buffer;
            if(jgr_disc_read(disc,file,at,buffer,n))return -1;
            unsigned i=0;
            while(i+4<=n) {
                if(!memcmp(buffer+i,"GBIX",4)) {
                    if(++count>8192)return -1;
                    int status=mip_source(disc,file,at+i,d,textures,pack,aliases);if(status)return status;i+=8;
                }else i++;
            }
            at+=i;
        }
    }return 0;
}
static int mip_materialize(JgrDisc *disc,const MipPack *packs,const MipAliases *aliases,Derived *d,MipTexture *texture)
{
    unsigned char palette[1024];if(der_read(d,texture->clut,palette,1024))return -1;
    for(unsigned id=texture->first;id;id=aliases->rows[id-1].next) {
        const MipAlias *alias=aliases->rows+id-1;const JgrDiscFile *file=&packs[alias->pack].file;MipSource source;
        if(mip_header(disc,file,alias->gbix,&source)!=1)return -1;
        unsigned char *raw=mip_raw(disc,file,&source);if(!raw)return -1;
        int status=mip_lower(raw,&source,palette,texture);free(raw);if(status)return -1;
    }return 0;
}
int jgr_stage_mips(JgrDisc *disc,FILE *source,unsigned stage,FILE *output)
{
    Derived d;MipTexture *textures=NULL;MipPack *packs=NULL;MipAliases aliases={0};unsigned npacks=0;int result=-1,lazy=1;
    if(!disc || !output || (stage!=1 && stage!=2 && stage!=3 && stage!=5 && stage!=6) || der_open(&d,source))return -1;
    const unsigned *weights=NULL;for(unsigned i=0;i<sizeof stage_texture_usage/sizeof stage_texture_usage[0];i++)if(stage_texture_usage[i].key==d.key && stage_texture_usage[i].count==d.nt)weights=stage_texture_usage[i].weight;
    if(!weights)return -1;
    jgr_indexed_cache_begin();
    textures=calloc(d.nt,sizeof *textures);packs=calloc(128,sizeof *packs);if(!textures || !packs || mip_packs(disc,stage,packs,&npacks))goto done;
    for(unsigned i=0;i<d.nt;i++) {
        unsigned char row[16];if(der_read(&d,d.ot+i*16,row,16))goto done;MipTexture *t=textures+i;
        t->width=der_half(row);t->height=der_half(row+2);t->flags=der_half(row+4);t->clut=der_word(row+8);t->pixels=der_word(row+12);t->weight=weights[i];
        if(t->flags&1 || t->width<64 || t->height<32 || t->width!=t->height || t->width>256 || !t->weight || (stage==2 && i==60))continue;
        t->cost=(t->width*t->height/4)+(t->width*t->height/16);t->eligible=1;t->fnv=2166136261u;
        JgrSha256 sha;jgr_sha256_init(&sha);
        if(der_hash(&d,t->clut,1024,&sha,&t->fnv) || der_hash(&d,t->pixels,t->width*t->height,&sha,&t->fnv))goto done;
        jgr_sha256_final(&sha,t->sha);
    }
    int scan=mip_scan(disc,packs,npacks,&d,textures,&aliases);if(scan<0)goto done;
    if(scan>0) {
        /* Optional alias storage exhausted: discard incomplete discovery and
         * retain the original eager matching/ambiguity/selection behavior. */
        free(aliases.rows);memset(&aliases,0,sizeof aliases);lazy=0;MIP_COUNT(eager_fallbacks);
        for(unsigned i=0;i<d.nt;i++)textures[i].first=textures[i].last=0;
        if(mip_scan(disc,packs,npacks,&d,textures,NULL))goto done;
    }
    unsigned ids[DERIVED_MAX_TEXTURES],count=0;
    for(unsigned i=0;i<d.nt;i++)if(lazy ? textures[i].first!=0 : (textures[i].matched && !textures[i].ambiguous && textures[i].quality)) {
        unsigned at=count;while(at && (uint64_t)textures[i].weight*textures[ids[at-1]].cost>(uint64_t)textures[ids[at-1]].weight*textures[i].cost){ids[at]=ids[at-1];at--;}
        ids[at]=i;count++;
    }
    unsigned selected[16],nr=0,pixels=0;
    for(unsigned i=0;i<count && nr<16;i++) {
        MipTexture *t=textures+ids[i];if(t->cost>MIP_BUDGET-pixels)continue;
        if(lazy) {
            if(mip_materialize(disc,packs,&aliases,&d,t))goto done;
            if(!t->matched || t->ambiguous || !t->quality){free(t->lower);t->lower=NULL;continue;}
        }
        selected[nr++]=ids[i];pixels+=t->cost;
    }
    if(der_write(output,"JGM1",4) || der_put(output,d.key) || der_put(output,d.nt) || der_put(output,nr) || der_put(output,pixels) || der_put(output,32) || der_put(output,32+nr*32) || der_put(output,32+nr*32+pixels))goto done;
    unsigned offset=0;
    for(unsigned i=0;i<nr;i++) {
        unsigned char row[32]={0};unsigned id=selected[i];MipTexture *t=textures+id;
        row[0]=id;row[1]=id>>8;row[2]=t->width;row[3]=t->width>>8;row[4]=t->height;row[5]=t->height>>8;row[6]=2;
        der_encode(row+8,t->fnv);der_encode(row+12,offset);der_encode(row+16,offset+t->width*t->height/4);
        der_encode(row+20,t->width*t->height/4);der_encode(row+24,t->width*t->height/16);
        if(der_write(output,row,32))goto done;
        offset+=t->cost;
    }
    for(unsigned i=0;i<nr;i++)if(der_write(output,textures[selected[i]].lower,textures[selected[i]].cost))goto done;
    result=0;
 done:if(textures)for(unsigned i=0;i<d.nt;i++)free(textures[i].lower);free(textures);free(packs);free(aliases.rows);jgr_indexed_cache_end();return result;
}

#include "portable_stage_visibility.inc"
