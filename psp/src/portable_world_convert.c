/* Setup-only world serializers. Original payload is streamed from the user's
 * verified disc. Limits bound malformed source work as well as allocations. */
#include "portable_world_convert.h"
#include "portable_mission_convert.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define EXE_BASE 0x8c010000u
#define STAGE_BASE 0x8cb00000u
#define PACK_BASE 0x10000000u
#define IMAGE_LIMIT (2u*1024u*1024u)
typedef struct {
    unsigned stage,neighbors,zones,zone_count,counts;
    unsigned types,paths,near,far,texlists,motions,collider_a,collider_b,slip,sound,special;
    const char *audio[3],*peds;
} WorldSpec;
typedef struct {
    unsigned stage,grid,grid_pointer,sectors,items,stride,prototypes,prototype_count,collider_offset,prototypes_only;
    unsigned static_nodes,static_count,dynamic,dynamic_prototypes,dynamic_types,dynamic_stride,legacy,cip,skip_boxes;
} WorldCollisionSpec;
#include "portable_world_data.inc"
typedef struct {JgrDisc *disc;JgrDiscFile exe,stage;FILE *out;int error;unsigned native_solid;} World;
static uint32_t u32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned u16(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
static void s32(unsigned char *p,uint32_t v)
{p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static const JgrDiscFile *source(World *w,uint32_t address,uint32_t bytes,uint32_t *offset)
{
    if(address>=EXE_BASE && address-EXE_BASE<=w->exe.size && bytes<=w->exe.size-(address-EXE_BASE)) {
        *offset=address-EXE_BASE;return &w->exe;
    }
    if(address>=STAGE_BASE && address-STAGE_BASE<=w->stage.size && bytes<=w->stage.size-(address-STAGE_BASE)) {
        *offset=address-STAGE_BASE;return &w->stage;
    }
    return NULL;
}
static void read_at(World *w,uint32_t address,void *data,uint32_t bytes)
{
    uint32_t offset;const JgrDiscFile *f=source(w,address,bytes,&offset);
    if(!f || jgr_disc_read(w->disc,f,offset,data,bytes)){memset(data,0,bytes);w->error=1;}
}
static uint32_t word(World *w,uint32_t address)
{unsigned char data[4];read_at(w,address,data,4);return u32(data);}
static void write_bytes(World *w,const void *data,size_t size)
{if(!w->error && fwrite(data,1,size,w->out)!=size)w->error=1;}
static void put(World *w,uint32_t value){unsigned char data[4];s32(data,value);write_bytes(w,data,4);}
static void copy_file(World *w,const JgrDiscFile *file,uint32_t offset,uint32_t size)
{
    unsigned char block[4096];
    if(offset>file->size || size>file->size-offset){w->error=1;return;}
    while(size && !w->error) {
        unsigned n=size<sizeof block ? size:sizeof block;
        if(jgr_disc_read(w->disc,file,offset,block,n)){w->error=1;break;}
        write_bytes(w,block,n);offset+=n;size-=n;
    }
}
static void copy(World *w,uint32_t address,uint32_t size)
{
    uint32_t offset;const JgrDiscFile *f=source(w,address,size,&offset);
    if(!f){w->error=1;return;}copy_file(w,f,offset,size);
}
static void zones(World *w,const WorldSpec *s)
{
    unsigned char record[56];uint32_t descriptor=word(w,s->zones),records=word(w,descriptor),n=word(w,descriptor+4);
    if(n>4096 || s->zone_count>4096){w->error=1;return;}
    write_bytes(w,"ZON1",4);put(w,s->zone_count);put(w,n);copy(w,s->counts,24);
    for(unsigned i=0;i<n && !w->error;i++) {
        read_at(w,records+i*56,record,sizeof record);
        uint32_t na=u32(record+28),nb=u32(record+36);
        if(na>4096 || nb>4096){w->error=1;return;}
        write_bytes(w,record+40,8);write_bytes(w,record,24);put(w,na);put(w,nb);
        if(na)copy(w,u32(record+24),na*144);
        if(nb)copy(w,u32(record+32),nb*144);
    }
    for(unsigned i=0;i<s->zone_count && !w->error;i++) {
        uint32_t address=word(w,s->neighbors+i*8+4),list[65],count=0,offset;
        while(address && count<65 && source(w,address+count*4,4,&offset)) {
            uint32_t value=word(w,address+count*4);if((int32_t)value<0)break;
            list[count++]=value;
        }
        put(w,count);for(unsigned j=0;j<count;j++)put(w,list[j]);
    }
}
static void audio_bank(World *w,const WorldSpec *s)
{
    JgrDiscFile files[3];uint32_t offset=32;
    for(unsigned i=0;i<3;i++)if(jgr_disc_source(w->disc,s->audio[i],&files[i]) || files[i].size>16u*1024u*1024u){w->error=1;return;}
    write_bytes(w,"WSA1",4);put(w,s->stage);
    for(unsigned i=0;i<3;i++){put(w,offset);put(w,files[i].size);offset+=files[i].size;}
    for(unsigned i=0;i<3;i++)copy_file(w,&files[i],0,files[i].size);
}
static void grips(World *w,const WorldSpec *s)
{
    unsigned char zero[156]={0};write_bytes(w,"GRP1",4);put(w,s->stage);put(w,s->types);
    for(unsigned i=0;i<s->types && !w->error;i++) {
        uint32_t address=s->slip ? word(w,s->slip+i*4):0;put(w,address);
        if(address)copy(w,address,156);else write_bytes(w,zero,sizeof zero);
    }
}

/* Motion/collision images deduplicate complete serialized byte strings, as
 * the desktop serializer does. No full original stage image is allocated. */
typedef struct {uint32_t offset,size,hash;} ImageEntry;
typedef struct {unsigned char *data;uint32_t size,capacity,count;ImageEntry *entries;World *world;} Image;
static uint32_t append(Image *im,const void *data,uint32_t size)
{
    World *w=im->world;const unsigned char *bytes=data;uint32_t hash=2166136261u;
    if(w->error)return 0;
    for(uint32_t i=0;i<size;i++)hash=(hash^bytes[i])*16777619u;
    for(uint32_t i=0;i<im->count;i++) {
        ImageEntry *e=im->entries+i;
        if(e->hash==hash && e->size==size && !memcmp(im->data+e->offset,data,size))return PACK_BASE+e->offset;
    }
    uint32_t start=(im->size+3u)&~3u;
    if(size>IMAGE_LIMIT || start>IMAGE_LIMIT-size || im->count==8192){w->error=1;return 0;}
    uint32_t end=start+size;
    if(end>im->capacity) {
        uint32_t cap=im->capacity;while(cap<end)cap*=2;
        unsigned char *p=realloc(im->data,cap);if(!p){w->error=1;return 0;}im->data=p;im->capacity=cap;
    }
    memset(im->data+im->size,0,start-im->size);memcpy(im->data+start,data,size);im->size=end;
    im->entries[im->count++]=(ImageEntry){start,size,hash};return PACK_BASE+start;
}
static unsigned char *blob(World *w,uint32_t address,uint32_t bytes)
{
    if(bytes>IMAGE_LIMIT){w->error=1;return NULL;}
    unsigned char *p=malloc(bytes ? bytes:1);if(!p){w->error=1;return NULL;}
    read_at(w,address,p,bytes);return p;
}
static uint32_t copy_append(Image *im,uint32_t address,uint32_t bytes)
{
    unsigned char *p=blob(im->world,address,bytes);if(!p)return 0;
    uint32_t result=append(im,p,bytes);free(p);return result;
}
static uint32_t collider(Image *im,uint32_t address)
{
    World *w=im->world;unsigned char node[68],mesh[12];if(!address)return 0;
    read_at(w,address,node,sizeof node);
    if(u32(node+8) || u32(node+60) || u32(node+64)){w->error=1;return 0;}
    uint32_t target=0,pointer=u32(node+4);
    if(u16(node)==4)target=copy_append(im,pointer,96);
    else if(u16(node)==6) {
        read_at(w,pointer,mesh,sizeof mesh);unsigned nv=u16(mesh+8),np=u16(mesh+10);
        if(!nv || np>4096){w->error=1;return 0;}
        uint32_t vertices=copy_append(im,u32(mesh),nv*12);
        unsigned char *patches=blob(w,u32(mesh+4),np*36);if(!patches)return 0;
        for(unsigned i=0;i<np && !w->error;i++) {
            unsigned char *patch=patches+i*36;unsigned ni=(u32(patch+28)&65535)+2;
            unsigned char *indices=blob(w,u32(patch+24),ni*2);if(!indices)break;
            for(unsigned j=0;j<ni;j++)if((int16_t)u16(indices+j*2)<0 || u16(indices+j*2)>=nv){w->error=1;break;}
            s32(patch+24,append(im,indices,ni*2));free(indices);
        }
        s32(mesh,vertices);s32(mesh+4,append(im,patches,np*36));free(patches);target=append(im,mesh,12);
    } else w->error=1;
    s32(node+4,target);return append(im,node,sizeof node);
}
static unsigned tree(World *w,uint32_t root,uint32_t nodes[64])
{
    uint32_t stack[128];unsigned top=0,count=0;if(root)stack[top++]=root;
    while(top && !w->error) {
        uint32_t a=stack[--top];if(count==64){w->error=1;break;}
        for(unsigned i=0;i<count;i++)if(nodes[i]==a){w->error=1;return count;}
        nodes[count++]=a;uint32_t child=word(w,a+44),sibling=word(w,a+48);
        if(top+2>128){w->error=1;break;}
        if(sibling)stack[top++]=sibling;
        if(child)stack[top++]=child;
    }
    return count;
}
static void traffic(World *w,const WorldSpec *s)
{
    uint32_t types[16][12]={{0}},paths[32][4]={{0}},maxnodes=0,model=0;
    if(s->types>16 || s->paths>32){w->error=1;return;}
    for(unsigned i=0;i<s->types && !w->error;i++) {
        uint32_t *t=types[i],nodes[2][64];unsigned counts[2];
        t[0]=word(w,s->near+i*4);t[1]=word(w,s->far+i*4);t[2]=word(w,s->texlists+i*4);
        t[3]=s->collider_a ? word(w,s->collider_a+i*4):0;t[4]=s->collider_b ? word(w,s->collider_b+i*4):0;
        t[5]=s->slip ? word(w,s->slip+i*4):0;t[7]=t[8]=0xffffffffu;
        if(s->stage==2)t[6]=1;
        else if(s->stage==1)t[6]=word(w,s->sound+i*4);
        else {signed char sound;read_at(w,s->sound+i,&sound,1);t[6]=(uint32_t)(int32_t)sound;}
        for(unsigned lod=0;lod<2;lod++) {
            counts[lod]=tree(w,t[lod],nodes[lod]);if(counts[lod]>maxnodes)maxnodes=counts[lod];
            t[9+lod]=t[lod] ? model++:0xffffffffu;
        }
        if(s->special && t[0]) {
            uint32_t children[64],child=word(w,t[0]+44);unsigned count=0;
            while(child && !w->error) {
                if(count==64){w->error=1;break;}children[count++]=child;child=word(w,child+48);
            }
            if(count>5)for(unsigned j=0;j<2;j++) {
                unsigned k=0;while(k<counts[0] && nodes[0][k]!=children[j+4])k++;
                if(k==counts[0])w->error=1;else t[7+j]=k;
            }
        }
    }
    Image im={0};im.world=w;im.capacity=32768;im.size=16;
    im.data=calloc(1,im.capacity);im.entries=calloc(8192,sizeof *im.entries);
    if(!im.data || !im.entries){w->error=1;goto done;}
    for(unsigned i=0;i<s->types && !w->error;i++) {
        unsigned char directory[8];s32(directory,collider(&im,types[i][3]));s32(directory+4,collider(&im,types[i][4]));
        types[i][11]=append(&im,directory,8);
    }
    for(unsigned i=0;i<s->paths && !w->error;i++) {
        uint32_t address=word(w,s->motions+i*4);if(!address)continue;
        uint32_t md=word(w,address),frames=word(w,address+4),kind=word(w,address+8),channels=kind>>16;
        if((kind!=0x20003 && kind!=0x30007) || !frames || frames>=10000){w->error=1;break;}
        unsigned char rows[64*24]={0},header[12];
        for(unsigned bone=0;bone<maxnodes && !w->error;bone++)for(unsigned ch=0;ch<channels && !w->error;ch++) {
            uint32_t at=md+bone*channels*8,ptr=word(w,at+ch*4),n=word(w,at+channels*4+ch*4),packed=0;
            if(ptr) {
                if(!n || n>frames){w->error=1;break;}
                unsigned char *keys=blob(w,ptr,n*16);if(!keys)break;
                uint32_t previous=0;
                for(unsigned k=0;k<n;k++) {
                    uint32_t time=u32(keys+k*16);
                    if((!k && time) || (k && time<=previous) || time>=frames){w->error=1;break;}previous=time;
                }
                packed=append(&im,keys,n*16);free(keys);
            }
            s32(rows+bone*channels*8+ch*4,packed);s32(rows+bone*channels*8+channels*4+ch*4,n);
        }
        s32(header,append(&im,rows,maxnodes*channels*8));s32(header+4,frames);s32(header+8,kind);
        paths[i][0]=address;paths[i][1]=append(&im,header,12);paths[i][2]=maxnodes;paths[i][3]=frames;
    }
    write_bytes(w,"PTH1",4);put(w,s->stage);put(w,s->types);put(w,s->paths);
    put(w,32);put(w,32+s->types*48);put(w,32+s->types*48+s->paths*16);put(w,im.size);
    for(unsigned i=0;i<s->types;i++)for(unsigned j=0;j<12;j++)put(w,types[i][j]);
    for(unsigned i=0;i<s->paths;i++)for(unsigned j=0;j<4;j++)put(w,paths[i][j]);
    write_bytes(w,im.data,im.size);
 done:free(im.data);free(im.entries);
}

static void train(World *w,unsigned stage)
{
    const uint32_t *addresses=stage==3 ? world_train_3:world_train_5;
    unsigned count=stage==3 ? 4:2;uint32_t motions[5]={0};Image im={0};
    im.world=w;im.capacity=32768;im.size=16;im.data=calloc(1,im.capacity);im.entries=calloc(8192,sizeof *im.entries);
    if(!im.data || !im.entries){w->error=1;goto done;}
    for(unsigned i=0;i<count && !w->error;i++) {
        uint32_t a=addresses[i],md=word(w,a),frames=word(w,a+4),kind=word(w,a+8);
        unsigned char entries[64]={0},header[12];uint32_t declared=0;
        if(kind!=0x20003 || !frames || frames>10000){w->error=1;break;}
        for(unsigned bone=0;bone<4 && !w->error;bone++)for(unsigned channel=0;channel<2;channel++) {
            uint32_t pointer=word(w,md+bone*16+channel*4),n=word(w,md+bone*16+8+channel*4);
            if(n>frames || (pointer && !n)){w->error=1;break;}
            /* Preserve the original route's adjacent final rotation key and
             * its declared-count wrap as separate relocated descriptors. */
            if(stage==3 && i==2 && bone==2 && channel==1) {
                declared=n;if(!pointer || word(w,pointer+n*16)!=frames-1){w->error=1;break;}n++;
            }
            s32(entries+bone*16+channel*4,pointer ? copy_append(&im,pointer,n*16):0);
            s32(entries+bone*16+8+channel*4,n);
        }
        s32(header,append(&im,entries,sizeof entries));s32(header+4,frames);s32(header+8,kind);motions[i]=append(&im,header,12);
        if(stage==3 && i==2) {
            s32(entries+44,declared);s32(header,append(&im,entries,sizeof entries));motions[4]=append(&im,header,12);
        }
    }
    write_bytes(w,"BTR1",4);put(w,PACK_BASE);put(w,im.size);for(unsigned i=0;i<5;i++)put(w,motions[i]);write_bytes(w,im.data,im.size);
 done:free(im.data);free(im.entries);
}
static int range(World *w,uint32_t address,uint32_t bytes,uint32_t *low,uint32_t *high)
{
    uint32_t offset;if(!bytes || !source(w,address,bytes,&offset) || address>UINT32_MAX-bytes){w->error=1;return -1;}
    if(address<*low)*low=address;
    if(address+bytes>*high)*high=address+bytes;
    return 0;
}
static void doors(World *w)
{
    uint32_t low=UINT32_MAX,high=0;
    for(unsigned i=0;i<8 && !w->error;i++) {
        uint32_t motion=word(w,world_door_motions[i]),md=word(w,motion),frames=word(w,motion+4),kind=word(w,motion+8);
        if(frames!=20 || kind!=0x20003){w->error=1;break;}
        range(w,motion,12,&low,&high);range(w,md,16,&low,&high);
        for(unsigned channel=0;channel<2;channel++) {
            uint32_t pointer=word(w,md+channel*4),n=word(w,md+8+channel*4);
            if(pointer){if(!n || n>frames){w->error=1;break;}range(w,pointer,n*16,&low,&high);}
        }
    }
    if(w->error || high<low || high-low>IMAGE_LIMIT){w->error=1;return;}
    write_bytes(w,"SDM1",4);put(w,low);put(w,high-low);copy(w,low,high-low);
}

/* Mission image contents remain opaque except for the documented JMDc chain
 * fields. Use two passes over original data, never allocate the raw bank. */
typedef struct {JgrDiscFile file;char name[16];unsigned records;uint32_t blob_offset;} Mission;
static int mission_chain(World *w,const JgrDiscFile *f,uint32_t offsets[128])
{
    uint32_t at=0;unsigned count=0;unsigned char row[0xf0];
    while(!w->error) {
        if(count==128 || (at&3) || at>f->size || sizeof row>f->size-at || jgr_disc_read(w->disc,f,at,row,sizeof row) || memcmp(row,"JMDc",4)){w->error=1;break;}
        for(unsigned i=0;i<count;i++)if(offsets[i]==at){w->error=1;return -1;}
        offsets[count++]=at;uint32_t next=u32(row+0xec);if(!next)return (int)count;
        if(next<0x8cde0000u){w->error=1;break;}at=next-0x8cde0000u;
    }
    return -1;
}
static void missions(World *w)
{
    JgrDiscFile archive,usa;unsigned char header[8];Mission *files=NULL;unsigned file_count=0,records=0;uint32_t bytes=0;
    if(jgr_disc_find(w->disc,"JETRADIO/E_MISSIONDATA.AFS",&archive) || jgr_disc_find(w->disc,"JETRADIO/MISSIONDATA.AFS",&usa) ||
       jgr_disc_read(w->disc,&archive,0,header,8) || memcmp(header,"AFS",3)){w->error=1;return;}
    unsigned count=u32(header+4);if(count>1024){w->error=1;return;}
    files=calloc(count,sizeof *files);if(!files){w->error=1;return;}
    for(unsigned i=0;i<count && !w->error;i++) {
        JgrDiscFile file;char name[33],selected[33],original[33];unsigned char magic[4];uint32_t offsets[128];
        if(jgr_disc_afs_named(w->disc,&archive,i,&file,name)){w->error=1;break;}
        memcpy(original,name,sizeof original);char *stem=strrchr(name,'_');stem=stem ? stem+1:name;char *dot=strrchr(stem,'.');if(dot)*dot=0;
        for(char *p=stem;*p;p++)*p=(char)toupper((unsigned char)*p);
        if(!strcmp(stem,"S1M07") || !strcmp(stem,"S2M03") || !strcmp(stem,"S2M07") || !strcmp(stem,"S3M07")) {
            if(jgr_disc_afs_named(w->disc,&usa,i,&file,selected)){w->error=1;break;}
            if(strcmp(selected,original)){w->error=1;break;}
        }
        if(file.size<4 || jgr_disc_read(w->disc,&file,0,magic,4)){w->error=1;break;}
        if(memcmp(magic,"JMDc",4))continue;
        if(file.size>8u*1024u*1024u || bytes>32u*1024u*1024u-file.size-15){w->error=1;break;}
        int n=mission_chain(w,&file,offsets);if(n<0)break;
        Mission *m=files+file_count++;m->file=file;size_t length=strlen(stem);if(length>16)length=16;memcpy(m->name,stem,length);m->records=(unsigned)n;m->blob_offset=bytes;
        records+=(unsigned)n;bytes+=(file.size+15)&~15u;
        if(records>4096){w->error=1;break;}
    }
    write_bytes(w,"MIS3",4);put(w,records);unsigned first=0;
    for(unsigned i=0;i<file_count && !w->error;i++) {
        Mission *m=files+i;uint32_t offsets[128];int n=mission_chain(w,&m->file,offsets);
        if(n!=(int)m->records){w->error=1;break;}
        for(unsigned j=0;j<m->records && !w->error;j++) {
            unsigned char source_row[0xf0],row[88]={0};
            if(jgr_disc_read(w->disc,&m->file,offsets[j],source_row,sizeof source_row)){w->error=1;break;}
            memcpy(row,source_row+8,20);memcpy(row+20,source_row+0x70,12);memcpy(row+32,m->name,16);
            s32(row+48,u32(source_row+0xe4));s32(row+52,u32(source_row+0x24));s32(row+56,0x8cde0000u);
            s32(row+60,8+records*88+m->blob_offset);s32(row+64,m->file.size);s32(row+68,offsets[j]);
            s32(row+72,j+1<m->records ? first+j+1:0xffffffffu);s32(row+76,u32(source_row+0x28));
            s32(row+80,u32(source_row+0x80));s32(row+84,first);write_bytes(w,row,sizeof row);
        }
        first+=m->records;
    }
    for(unsigned i=0;i<file_count && !w->error;i++) {
        unsigned char zero[16]={0};copy_file(w,&files[i].file,0,files[i].file.size);
        write_bytes(w,zero,(0u-files[i].file.size)&15);
    }
    free(files);
}

#include "portable_world_collision.inc"

static const WorldSpec *identify(const char *name,unsigned *kind)
{
    static const char *formats[]={"zones%u.bin","worldaudio%u.bin","traffic%u_grips.bin","traffic%u.bin","peds%u.bin","stage%u.sob","stage%u.col"};
    char expected[64];
    for(unsigned i=0;i<sizeof world_specs/sizeof world_specs[0];i++)for(unsigned k=0;k<sizeof formats/sizeof formats[0];k++) {
        snprintf(expected,sizeof expected,formats[k],world_specs[i].stage);
        if(!strcmp(expected,name)){*kind=k;return world_specs+i;}
    }
    return NULL;
}
int jgr_world_convert_known(const char *name)
{unsigned kind;return name && (!strcmp(name,"missions.bin") || !strcmp(name,"bantam_train.bin") || !strcmp(name,"benten_train.bin") || !strcmp(name,"shibuya_doors.bin") || identify(name,&kind)!=NULL);}
int jgr_world_convert(JgrDisc *disc,const char *name,FILE *output)
{
    if(!disc || !name || !output || !jgr_world_convert_known(name))return -1;
    World w={0};w.disc=disc;w.out=output;
    if(!strcmp(name,"missions.bin")){missions(&w);return w.error ? -1:0;}
    unsigned kind;const WorldSpec *s=identify(name,&kind);
    if(!s) {
        if(!strcmp(name,"bantam_train.bin")){s=world_specs+3;kind=7;}
        else if(!strcmp(name,"benten_train.bin")){s=world_specs+2;kind=7;}
        else if(!strcmp(name,"shibuya_doors.bin")){s=world_specs;kind=8;}
        else return -1;
    }
    if(kind==1){audio_bank(&w,s);return w.error ? -1:0;}
    if(kind==4) {
        JgrDiscFile file;if(jgr_disc_source(disc,s->peds,&file))return -1;
        return jgr_mission_convert(disc,&file,JGR_MISSION_PEDS,0,output);
    }
    char path[96];snprintf(path,sizeof path,"decompile/afs/STAGE%u/000_st%udat.bin",s->stage,s->stage);
    if(jgr_disc_source(disc,"decompile/extracted/1ST_READ.BIN",&w.exe) || jgr_disc_source(disc,path,&w.stage))return -1;
    if(kind==0)zones(&w,s);else if(kind==2)grips(&w,s);else if(kind==3)traffic(&w,s);else if(kind==5)set_colliders(&w,s);else if(kind==6)world_collision(&w,s);else if(kind==7)train(&w,s->stage);else doors(&w);
    return w.error ? -1:0;
}
int jgr_world_native_collision(JgrDisc *disc,unsigned stage,FILE *output)
{
    if(!disc || !output || (stage<1 || stage>3))return -1;
    const WorldSpec *spec=NULL;
    for(unsigned i=0;i<sizeof world_specs/sizeof world_specs[0];i++)
        if(world_specs[i].stage==stage)spec=world_specs+i;
    if(!spec)return -1;
    World w={0};w.disc=disc;w.out=output;w.native_solid=1;
    char path[96];snprintf(path,sizeof path,"decompile/afs/STAGE%u/000_st%udat.bin",stage,stage);
    if(jgr_disc_source(disc,"decompile/extracted/1ST_READ.BIN",&w.exe) ||
       jgr_disc_source(disc,path,&w.stage))return -1;
    world_collision(&w,spec);return w.error?-1:0;
}
