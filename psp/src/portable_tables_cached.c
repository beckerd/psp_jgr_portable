/* Setup-only bindings into catalog-verified generated geometry. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#include "portable_tables.h"
#include "portable_stage.h"
#include "portable_stage_derived.h"
#include "portable_mission_cast.h"
#include "portable_scratch.h"
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    const char *directory;JgrTableDependency dependency;void *user;JgrScratchPool *scratch;
    FILE *city;const uint16_t *tunnel;unsigned tunnel_count;
    unsigned char *captured;unsigned captured_size;
} Cached;
static const char *const city_table_names[2]={"source_data_stage_ped_proxy_data.jsd","source_data_ptr_kogane_tunnel_materials.jsd"};
static struct {
    JgrDisc *disc;char directory[1024];JgrTableDependency dependency;void *user;
    unsigned char *data[2];unsigned size[2],busy;
} city_tables;
static unsigned cw(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static unsigned ch(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static void cp(unsigned char *p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static int cr(FILE *f,unsigned at,void *out,unsigned count){return fseek(f,at,SEEK_SET)||fread(out,1,count,f)!=count?-1:0;}
static int csize(FILE *f,unsigned *size){if(fseek(f,0,SEEK_END))return -1;long n=ftell(f);if(n<0||(unsigned long)n>0x7fffffffu)return -1;*size=(unsigned)n;return 0;}
static int cspan(unsigned at,unsigned count,unsigned unit,unsigned size){return at<=size && count<=(size-at)/unit;}
static int same_city_session(JgrDisc *disc,const Cached *c)
{return disc==city_tables.disc&&c->dependency==city_tables.dependency&&c->user==city_tables.user&&!strcmp(c->directory,city_tables.directory);}
static int cbinding(Cached *c,const JgrTableSection *sections,unsigned count,unsigned char *data,int doors)
{
    FILE *jgs=c->dependency(c->user,"stage1.jgs"),*jrs=c->dependency(c->user,"stage1.jrs");int result=-1;
    unsigned char geometry[80],owners[32];unsigned jgs_size,jrs_size;
    if(!jgs||!jrs||count!=(doors?6u:2u)||csize(jgs,&jgs_size)||csize(jrs,&jrs_size)||cr(jgs,0,geometry,80)||cr(jrs,0,owners,32)||memcmp(geometry,"JGS1",4)||memcmp(owners,"JRS1",4)||cw(owners+4)!=1)goto done;
    unsigned no=cw(geometry+8),oo=cw(geometry+28),sectors=cw(owners+8),records=cw(owners+12);
    if(no>65535||sectors>65535||records>65535||!cspan(oo,no,32,jgs_size)||!cspan(32,sectors,16,jrs_size)||!cspan(32+sectors*16,records,32,jrs_size))goto done;
    const JgrTableSection *s=sections+(doors?4:0);unsigned stride=doors?36:64,id_offset=doors?0:4,obj_offset=doors?4:32;
    if(s->size%stride)goto done;
    for(unsigned i=0;i<s->size/stride;i++){
        unsigned char *out=data+s->offset+i*stride;unsigned key=cw(out+id_offset),found=0;
        for(unsigned j=0;j<records;j++){
            unsigned char row[32];if(cr(jrs,32+sectors*16+j*32,row,32))goto done;
            if(cw(row+28)!=key)continue;
            unsigned object=ch(row),objects=ch(row+2);if(found++||objects!=1||object>=no||cr(jgs,oo+object*32,out+obj_offset,32))goto done;cp(out+id_offset,object);
        }
        if(found!=1)goto done;
    }
    /* The source generator orders door geometry by source root, then placement;
       source placement arrays instead run by sector. Restore geometry order. */
    if(doors)for(unsigned i=1;i<s->size/stride;i++){
        unsigned char row[36];memcpy(row,data+s->offset+i*stride,stride);unsigned j=i;
        while(j&&cw(data+s->offset+(j-1)*stride)>cw(row)){memcpy(data+s->offset+j*stride,data+s->offset+(j-1)*stride,stride);j--;}
        memcpy(data+s->offset+j*stride,row,stride);
    }
    result=0;
 done:{int failure_errno=errno;if(jgs&&fclose(jgs)&&!result){result=-1;failure_errno=errno;}if(jrs&&fclose(jrs)&&!result){result=-1;failure_errno=errno;}if(result)errno=failure_errno;return result;}
}
static int cgrind(Cached *c,const JgrTableSection *sections,unsigned count,unsigned char *data)
{
    FILE *jgs=c->dependency(c->user,"grind_lights.jgs");int result=-1;unsigned char header[80];unsigned size;
    if(!jgs||count!=8||sections[7].size%64||csize(jgs,&size)||cr(jgs,0,header,80)||memcmp(header,"JGS1",4))goto done;
    unsigned no=cw(header+8),nb=cw(header+12),oo=cw(header+28),ob=cw(header+32);
    if(no>65535||nb>65535||!cspan(oo,no,32,size)||!cspan(ob,nb,16,size))goto done;
    unsigned total=0;
    for(unsigned i=0;i<sections[7].size/64;i++){
        unsigned char *row=data+sections[7].offset+i*64;unsigned first=cw(row),n=cw(row+4),texture=~0u;
        if(first!=total||first>no||!n||n>no-first)goto done;
        total+=n;
        for(unsigned j=0;j<n;j++){
            unsigned char object[32];if(cr(jgs,oo+(first+j)*32,object,32))goto done;unsigned begin=cw(object+16),num=cw(object+20);if(begin>nb||!num||num>nb-begin)goto done;
            for(unsigned k=0;k<num;k++){unsigned char batch[16];if(cr(jgs,ob+(begin+k)*16,batch,16))goto done;unsigned t=ch(batch+12);if(ch(batch+14)&4)goto done;if(!j){if(texture==~0u)texture=t;else if(texture!=t)goto done;}}
        }
        if(texture>=cw(header+4))goto done;
        cp(row+8,texture);
    }
    result=total==no?0:-1;
 done:{int failure_errno=errno;if(jgs&&fclose(jgs)&&!result){result=-1;failure_errno=errno;}if(result)errno=failure_errno;return result;}
}
static int cped(JgrDisc *disc,Cached *c,const JgrTableSection *sections,unsigned count,unsigned char *data)
{
    FILE *props=NULL,*placements=NULL,*city=NULL,*owners=NULL;
    char paths[2][1024];int result=-1;
    if(count!=3||sections[0].size>65536-28||sections[2].size!=56)goto done;
    if(c->city){
        /* City sorts before its object bank during a fresh installation.
         * Preserve only roofs and the city bank now; finish bodies later. */
        placements=c->dependency(c->user,"set1_obj.bin");
        if(!placements||jgr_stage_ped_city_proxy(c->city,placements,data+sections[0].offset,sections[0].size,data+sections[2].offset,28))goto done;
        c->captured_size=sections[0].size+28;c->captured=malloc(c->captured_size);if(!c->captured)goto done;
        memcpy(c->captured,data+sections[0].offset,sections[0].size);
        memcpy(c->captured+sections[0].size,data+sections[2].offset,28);
        result=0;
        goto done;
    }
    props=c->dependency(c->user,"stage1_obj.jgs");if(!props)goto done;
    if(same_city_session(disc,c)&&city_tables.data[0]&&city_tables.size[0]==sections[0].size+28){
        memcpy(data+sections[0].offset,city_tables.data[0],sections[0].size);
        memcpy(data+sections[2].offset,city_tables.data[0]+sections[0].size,28);
        result=jgr_stage_ped_object_proxy(props,data+sections[1].offset,sections[1].size,data+sections[2].offset+28,28);
        goto done;
    }
    placements=c->dependency(c->user,"set1_obj.bin");if(!placements)goto done;
    for(unsigned i=0;i<2;i++){
        int n=snprintf(paths[i],sizeof paths[i],"%s/ped-table-%u.tmp",c->directory,i);if(n<0||(size_t)n>=sizeof paths[i])goto done;struct stat st;
#ifdef __PSP__
        int exists=stat(paths[i],&st);
#else
        int exists=lstat(paths[i],&st);
#endif
        if((!exists&&!S_ISREG(st.st_mode))||(exists&&errno!=ENOENT))goto done;
        FILE *f=jgr_scratch_open(c->scratch,paths[i]);if(!f)goto done;if(i)owners=f;else city=f;
    }
    if(jgr_stage_city_geometry(disc,1,c->directory,city,owners)||fflush(city)||jgr_stage_ped_proxy(city,props,placements,
       data+sections[0].offset,sections[0].size,data+sections[1].offset,sections[1].size,data+sections[2].offset,sections[2].size))goto done;
    result=0;
 done:{int failure_errno=errno;if(props&&fclose(props)&&!result){result=-1;failure_errno=errno;}if(placements&&fclose(placements)&&!result){result=-1;failure_errno=errno;}
    if(city){if(fclose(city)&&!result){result=-1;failure_errno=errno;}remove(paths[0]);}if(owners){if(fclose(owners)&&!result){result=-1;failure_errno=errno;}remove(paths[1]);}if(result)errno=failure_errno;return result;}
}
static int ctunnel(JgrDisc *disc,Cached *c,const JgrTableSection *sections,unsigned count,unsigned char *data)
{
    if(c->tunnel){
        if(count!=1||c->tunnel_count>32768||sections[0].size!=c->tunnel_count*2)return -1;
        for(unsigned i=0;i<c->tunnel_count;i++){
            data[sections[0].offset+i*2]=(unsigned char)c->tunnel[i];
            data[sections[0].offset+i*2+1]=(unsigned char)(c->tunnel[i]>>8);
        }
        return 0;
    }
    FILE *geometry=c->dependency(c->user,"stage2.jgs"),*tmp=NULL;char path[1024];int result=-1;unsigned size;
    if(!geometry||count!=1||sections[0].size>65536)goto done;
    int n=snprintf(path,sizeof path,"%s/tunnel-table.tmp",c->directory);if(n<0||(size_t)n>=sizeof path)goto done;struct stat st;
#ifdef __PSP__
    int exists=stat(path,&st);
#else
    int exists=lstat(path,&st);
#endif
    if((!exists&&!S_ISREG(st.st_mode))||(exists&&errno!=ENOENT))goto done;
    tmp=jgr_scratch_open(c->scratch,path);if(!tmp||jgr_stage_tunnel_materials(disc,c->directory,geometry,tmp)||fflush(tmp)||csize(tmp,&size)||size!=sections[0].size||cr(tmp,0,data+sections[0].offset,size))goto done;
    result=0;
 done:{int failure_errno=errno;if(geometry&&fclose(geometry)&&!result){result=-1;failure_errno=errno;}if(tmp){if(fclose(tmp)&&!result){result=-1;failure_errno=errno;}remove(path);}if(result)errno=failure_errno;return result;}
}
static int cmission(JgrDisc *disc,Cached *c,const JgrTableSection *sections,unsigned count,unsigned char *data)
{
    FILE *tmp=NULL;char path[1024];int result=-1;unsigned size,total=0;
    if(count!=3)return -1;
    for(unsigned i=0;i<count;i++){if(sections[i].size>65536-total)return -1;total+=sections[i].size;}
    int n=snprintf(path,sizeof path,"%s/mission-cast-table.tmp",c->directory);if(n<0||(size_t)n>=sizeof path)return -1;struct stat st;
#ifdef __PSP__
    int exists=stat(path,&st);
#else
    int exists=lstat(path,&st);
#endif
    if((!exists&&!S_ISREG(st.st_mode))||(exists&&errno!=ENOENT))return -1;
    tmp=jgr_scratch_open(c->scratch,path);if(!tmp)return -1;
    if(jgr_mission_cast_checked(disc,c->dependency,c->user,tmp)||fflush(tmp)||csize(tmp,&size)||size!=total)goto done;
    unsigned offset=0;for(unsigned i=0;i<count;i++){if(cr(tmp,offset,data+sections[i].offset,sections[i].size))goto done;offset+=sections[i].size;}
    result=0;
 done:{int failure_errno=errno;if(fclose(tmp)&&!result){result=-1;failure_errno=errno;}remove(path);if(result)errno=failure_errno;return result;}
}
static int cached_fill(JgrDisc *disc,const char *name,const JgrTableSection *sections,unsigned count,unsigned char *data,unsigned size,void *user)
{
    Cached *c=user;(void)size;
    if(!strcmp(name,"source_data_ptr_mission_cast.jsd"))return cmission(disc,c,sections,count,data);
    if(!strcmp(name,"source_data_ptr_kogane_tunnel_materials.jsd"))return ctunnel(disc,c,sections,count,data);
    if(!strcmp(name,"source_data_stage_ped_proxy_data.jsd"))return cped(disc,c,sections,count,data);
    if(!strcmp(name,"source_data_ptr_grind_props_data.jsd"))return cgrind(c,sections,count,data);
    if(!strcmp(name,"source_data_shibuya_gate_data.jsd"))return cbinding(c,sections,count,data,0);
    if(!strcmp(name,"source_data_ptr_shibuya_door_data.jsd"))return cbinding(c,sections,count,data,1);
    return -1;
}
static void capture_city_table(JgrDisc *disc,unsigned stage,FILE *geometry,const uint16_t *indices,unsigned count,void *user)
{
    (void)user;
    if(city_tables.busy||disc!=city_tables.disc||stage<1||stage>2||city_tables.data[stage-1])return;
    unsigned slot=stage-1,size=0;unsigned char *data=NULL;char path[1024];FILE *out=NULL;
    int saved_errno=errno;
    if(snprintf(path,sizeof path,"%s/city-table-capture.tmp",city_tables.directory)>=(int)sizeof path)return;
    JgrScratchPool *scratch=jgr_scratch_begin();if(!scratch)return;
    Cached context={city_tables.directory,city_tables.dependency,city_tables.user,scratch,
                    stage==1?geometry:NULL,stage==2?indices:NULL,count,NULL,0};
    city_tables.busy++;
    out=jgr_scratch_open(scratch,path);
    if(!out||jgr_convert_table_fill(disc,city_table_names[slot],out,cached_fill,&context)||fflush(out)||csize(out,&size)||!size||size>65536)goto done;
    if(stage==1){data=context.captured;size=context.captured_size;context.captured=NULL;}
    else {data=malloc(size);if(!data||cr(out,0,data,size))goto done;}
    if(!data||!size)goto done;
    /* Only completed in-process output becomes reusable. A failed capture
     * is harmless: the deferred job uses its normal full regeneration. */
    if(jgr_scratch_discard(scratch,out)){out=NULL;goto done;}out=NULL;
    if(jgr_scratch_end()){scratch=NULL;goto done;}scratch=NULL;
    city_tables.data[slot]=data;city_tables.size[slot]=size;data=NULL;
 done:
    free(data);free(context.captured);if(out)jgr_scratch_discard(scratch,out);if(scratch)jgr_scratch_end();
    city_tables.busy--;errno=saved_errno;
}
void jgr_table_city_cache_end(void)
{
    jgr_stage_city_capture(NULL,NULL,NULL);
    for(unsigned i=0;i<2;i++)free(city_tables.data[i]);
    memset(&city_tables,0,sizeof city_tables);
}
void jgr_table_city_cache_begin(JgrDisc *disc,const char *directory,JgrTableDependency dependency,void *user)
{
    jgr_table_city_cache_end();
    if(!disc||!directory||!dependency||strlen(directory)>=sizeof city_tables.directory)return;
    city_tables.disc=disc;strcpy(city_tables.directory,directory);city_tables.dependency=dependency;city_tables.user=user;
    jgr_stage_city_capture(disc,capture_city_table,NULL);
}
int jgr_convert_table_cached(JgrDisc *disc,const char *name,const char *directory,JgrTableDependency dependency,void *user,FILE *out)
{
    if(!disc||!name||!directory||!dependency||!out||!jgr_convert_table_deferred(name))return -1;
    if(disc==city_tables.disc&&dependency==city_tables.dependency&&user==city_tables.user&&!strcmp(directory,city_tables.directory)&&city_tables.data[1]&&!strcmp(name,city_table_names[1]))
        return fwrite(city_tables.data[1],1,city_tables.size[1],out)==city_tables.size[1]?0:-1;
    JgrScratchPool *scratch=jgr_scratch_begin();if(!scratch)return -1;
    Cached context={directory,dependency,user,scratch,NULL,NULL,0,NULL,0};
    city_tables.busy++;
    int result=jgr_convert_table_fill(disc,name,out,cached_fill,&context);
    city_tables.busy--;
    int failure_errno=errno;
    if(jgr_scratch_end()&&!result){result=-1;failure_errno=errno;}
    if(result)errno=failure_errno;
    return result;
}
