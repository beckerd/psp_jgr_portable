/* Setup-only intact parked-object springs and moving collision surfaces.
 * Source parameters are read from the verified user disc. No recorded frame
 * data, executable image or stage image is retained by this module. */
#include "portable_native_objects.h"
#include "portable_native_player.h"
#include "source_trs.h"
#include "source_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    unsigned type,enabled,count,handle,buffer,contact,settled;
    float pos[3],velocity[2],force[2];int32_t base[3],angle[3],published[3];
    ColTri *local,*geometry[2];
} NativeParkedObject;
struct JgrNativeParked {
    Collision *world;Player *player;unsigned count;
    float maximum,divisor,damping,restoring,settle;
    NativeParkedObject objects[64];
};
static unsigned object_word(const void *p){unsigned v;memcpy(&v,p,4);return v;}
static int object_publish(JgrNativeParked *s,NativeParkedObject *o,int initial)
{
    if(!initial && o->handle && !memcmp(o->angle,o->published,sizeof o->angle)) {
        /* Source transform is unchanged. Clear the preceding movement once,
         * then retain identical geometry/contact order without republishing. */
        if(!o->settled) {
            if(!col_surface_publish(s->world,o->handle,o->geometry[o->buffer],o->geometry[o->buffer],o->count))return 0;
            o->settled=1;
        }
        return 1;
    }
    unsigned next=o->buffer^1u;float matrix[16];const float unit[3]={1,1,1};
    source_trs_fsca(o->pos,o->angle,unit,matrix,jgr_native_player_fsca_sin);
    for(unsigned j=0;j<o->count;j++) {
        ColTri *d=o->geometry[next]+j;*d=o->local[j];d->src=4;
        for(unsigned v=0;v<3;v++)source_point(matrix,o->local[j].v+v*3,d->v+v*3);
    }
    if(initial)memcpy(o->geometry[o->buffer],o->geometry[next],o->count*sizeof(ColTri));
    if(o->handle){if(!col_surface_publish(s->world,o->handle,o->geometry[next],o->geometry[o->buffer],o->count))return 0;}
    else {o->handle=col_surface_add(s->world,o->geometry[next],o->geometry[o->buffer],o->count);if(!o->handle)return 0;}
    o->buffer=next;o->settled=initial;memcpy(o->published,o->angle,sizeof o->angle);return 1;
}
static float object_clamp(float x,float bound){return x>bound ? bound:x< -bound ? -bound:x;}
static int object_tick(void *context)
{
    JgrNativeParked *s=context;if(!s || !s->player)return -1;Player *p=s->player;if(p->reset_query)return 0;
    for(unsigned i=0;i<s->count;i++) {
        NativeParkedObject *o=s->objects+i;
        if(o->enabled && o->contact) {
            /* 0916fa: previous completed actor position, inverse base yaw,
             * then Ninja 2809b8's FIPR/FSRRA normalization. */
            float delta[3]={p->x-o->pos[0],p->y-o->pos[1],p->z-o->pos[2]},local[3];
            const float origin[3]={0},unit[3]={1,1,1};int32_t angle[3]={0,-o->base[1],0};float matrix[16];
            source_trs_fsca(origin,angle,unit,matrix,jgr_native_player_fsca_sin);source_point(matrix,delta,local);
            float length=probe_fipr(local,local);
            if(length>0.f){float inverse=1.f/sqrtf(length);for(unsigned k=0;k<3;k++)local[k]*=inverse;
                o->force[0]=object_clamp(o->force[0]+(local[2]*s->maximum)/s->divisor,s->maximum);
                o->force[1]=object_clamp(o->force[1]+(local[0]*s->maximum)/s->divisor,s->maximum);}
        }
        o->contact=0;
        if(o->enabled) {
            float total=((fabsf(o->velocity[0])+fabsf(o->force[0]))+fabsf(o->velocity[1]))+fabsf(o->force[1]);
            if(total<s->settle){memset(o->velocity,0,sizeof o->velocity);memset(o->force,0,sizeof o->force);}
            else for(unsigned j=0;j<2;j++) {
                unsigned axis=j ? 2:0;
                o->velocity[j]=object_clamp((o->velocity[j]+o->force[j])*s->damping,s->maximum);
                o->angle[axis]=(int32_t)((float)o->angle[axis]+o->velocity[j]);
                o->force[j]=s->restoring*(float)(o->base[axis]-o->angle[axis]);
            }
        }
        if(!object_publish(s,o,0))return -1;

    }
    return 0;
}
int jgr_native_parked_tick(JgrNativeParked *objects){return object_tick(objects);}
static void object_contact(void *context,int triangle)
{
    JgrNativeParked *s=context;const ColTri *current,*old;
    if(col_contact(s->world,triangle,&current,&old)!=2)return;
    uintptr_t point=(uintptr_t)current;
    for(unsigned i=0;i<s->count;i++) {
        NativeParkedObject *o=s->objects+i;uintptr_t first=(uintptr_t)o->geometry[o->buffer];
        if(point>=first && point<first+o->count*sizeof(ColTri)){o->contact=1;break;}
    }
}
void jgr_native_parked_bind(JgrNativeParked *s,Player *p)
{
    if(!s || !p)return;
    s->player=p;p->before_tick=object_tick;p->before_tick_context=s;
    p->surface_contact=object_contact;p->surface_contact_context=s;
    for(unsigned i=0;i<s->count;i++)s->objects[i].contact=0;
}
void jgr_native_parked_free(JgrNativeParked **owner)
{
    if(!owner || !*owner)return;JgrNativeParked *s=*owner;
    for(unsigned i=0;i<s->count;i++){NativeParkedObject *o=s->objects+i;if(o->handle)col_surface_remove(s->world,o->handle);
        free(o->local);free(o->geometry[0]);free(o->geometry[1]);}
    free(s);*owner=NULL;
}
typedef struct {unsigned zone,type;float position[3];int32_t angle[3];} ParkedPlacement;
typedef struct {
    JgrDisc *disc;JgrDiscFile executable,stage,mission_set;unsigned error,nodes;
} ParkedSource;
static int parked_read(ParkedSource *s,uint32_t address,void *data,unsigned bytes)
{
    const JgrDiscFile *file;uint32_t base;
    if(address>=0x8cde6000u){file=&s->mission_set;base=0x8cde6000u;}
    else if(address>=0x8cb00000u){file=&s->stage;base=0x8cb00000u;}
    else {file=&s->executable;base=0x8c010000u;}
    if(address<base || address-base>file->size || bytes>file->size-(address-base) ||
       jgr_disc_read(s->disc,file,address-base,data,bytes)) {
        s->error=1;memset(data,0,bytes);return 0;
    }
    return 1;
}
static unsigned parked_word(ParkedSource *s,uint32_t address)
{unsigned char b[4];parked_read(s,address,b,4);return object_word(b);}
static unsigned parked_tree_count(ParkedSource *s,uint32_t address,unsigned depth)
{
    unsigned total=0;
    while(address && !s->error) {
        unsigned char node[68];
        if(depth>=16 || s->nodes++>=2048 || !parked_read(s,address,node,sizeof node)){s->error=1;break;}
        unsigned kind=node[0]|(unsigned)node[1]<<8,mesh=object_word(node+4);
        if(kind==6) {
            unsigned char header[12];if(!parked_read(s,mesh,header,sizeof header))break;
            unsigned patches=object_word(header+4),count=header[10]|(unsigned)header[11]<<8;
            if(count>COL_SURFACE_TRIANGLES){s->error=1;break;}
            for(unsigned i=0;i<count && !s->error;i++) {
                uint64_t at=(uint64_t)patches+i*36u+28u;
                if(at>UINT32_MAX){s->error=1;break;}
                total+=parked_word(s,(uint32_t)at)&65535u;
                if(total>COL_SURFACE_TRIANGLES){s->error=1;break;}
            }
        } else if(kind==4)total+=12;
        uint32_t child=object_word(node+60);if(child)total+=parked_tree_count(s,child,depth+1);
        if(total>COL_SURFACE_TRIANGLES){s->error=1;break;}
        address=object_word(node+64);
    }
    return total;
}
static int parked_placements(ParkedSource *s,unsigned subset,ParkedPlacement output[64],unsigned *count)
{
    uint32_t address=0x8cde6000u,seen[8];unsigned char header[84];*count=0;
    if(subset>=8)return 0;
    for(unsigned i=0;i<=subset;i++) {
        if(!address)return 0;
        for(unsigned j=0;j<i;j++)if(address==seen[j])return 0;
        seen[i]=address;
        if(!parked_read(s,address,header,sizeof header) || memcmp(header,"JMSc",4))return 0;
        if(i<subset)address=object_word(header+80);
    }
    uint32_t table=object_word(header+52);if(!table)return 1;
    for(unsigned zone=0;zone<63 && !s->error;zone++) {
        if(table>UINT32_MAX-zone*4u)return 0;
        uint32_t pointer=parked_word(s,table+zone*4u);if(!pointer)continue;
        unsigned terminated=0;
        for(unsigned i=0;i<512 && !s->error;i++) {
            if(pointer>UINT32_MAX-i*4u)return 0;
            uint32_t record=parked_word(s,pointer+i*4u);if(!record){terminated=1;break;}
            unsigned char row[28];if(!parked_read(s,record,row,sizeof row))return 0;
            unsigned type=object_word(row);if(type>=18)continue; /* 030af8's source type gate */
            if(*count==64)return 0;
            ParkedPlacement *p=output+(*count)++;p->zone=zone;p->type=type;
            memcpy(p->position,row+4,12);memcpy(p->angle,row+16,12);
            for(unsigned j=0;j<3;j++)if(!isfinite(p->position[j]))return 0;
        }
        if(!terminated)return 0;
    }
    return !s->error;
}
int jgr_native_parked_load(JgrNativeParked **owner,Collision *world,JgrDisc *disc,
    JgrTableDependency dependency,void *user,unsigned stage,unsigned set,unsigned subset)
{
    if(!owner || *owner || !world || !disc || !dependency || stage!=1 || set>999)return 0;
    ParkedSource source={0};source.disc=disc;char name[96];JgrNativeParked *s=NULL;FILE *bank=NULL;int ok=0;
    snprintf(name,sizeof name,"decompile/afs/MISSIONSET/%03u_mission_set_%02u.bin",set,set);
    if(jgr_disc_source(disc,"decompile/extracted/1ST_READ.BIN",&source.executable) ||
       jgr_disc_source(disc,"decompile/afs/STAGE1/000_st1dat.bin",&source.stage) ||
       jgr_disc_source(disc,name,&source.mission_set))goto done;
    ParkedPlacement placements[64];unsigned placement_count;
    if(!parked_placements(&source,subset,placements,&placement_count))goto done;
    s=calloc(1,sizeof *s);if(!s)goto done;s->world=world;
    /* Coordinates select the source's creation/spring parameters. No copied
     * coefficient payload or per-frame motion is compiled into the program. */
    static const uint32_t coefficients[5]={0x8c0917c0u,0x8c0917c8u,0x8c091970u,0x8c091624u,0x8c091968u};
    float values[5];for(unsigned i=0;i<5;i++) {
        if(!parked_read(&source,coefficients[i],values+i,4) || !isfinite(values[i]) || !(values[i]>0.f))goto done;
    }
    s->maximum=values[0];s->divisor=values[1];s->damping=values[2];s->restoring=values[3];s->settle=values[4];
    unsigned counts[18],flags[18],offsets[18];
    for(unsigned i=0;i<18;i++) {
        flags[i]=parked_word(&source,0x8c107c50u+i*4u);source.nodes=0;
        counts[i]=parked_tree_count(&source,parked_word(&source,0x8c107ae8u+i*4u),0);
        if(source.error || flags[i]>1)goto done;
    }
    bank=dependency(user,"stage1.sob");if(!bank)goto done;
    long bytes;unsigned char header[8];
    if(fseek(bank,0,SEEK_END) || (bytes=ftell(bank))<8 || bytes>8*1024*1024 || fseek(bank,0,SEEK_SET) ||
       fread(header,1,8,bank)!=8 || memcmp(header,"SOB1",4) || object_word(header+4)!=18)goto done;
    unsigned at=8;
    for(unsigned i=0;i<18;i++) {
        if(at>(unsigned)bytes || (unsigned)bytes-at<4 || fread(header,1,4,bank)!=4)goto done;
        unsigned n=object_word(header);at+=4;offsets[i]=at;
        if(counts[i]>n || n>((unsigned)bytes-at)/sizeof(ColTri))goto done;
        at+=n*sizeof(ColTri);if(fseek(bank,at,SEEK_SET))goto done;
    }
    if(at!=(unsigned)bytes)goto done;
    col_reset_extra(world);
    for(unsigned i=0;i<placement_count;i++) {
        const ParkedPlacement *r=placements+i;unsigned type=r->type;if(!counts[type])continue;
        NativeParkedObject *o=s->objects+s->count++;o->type=type;o->enabled=flags[type];o->count=counts[type];
        memcpy(o->pos,r->position,12);memcpy(o->base,r->angle,12);memcpy(o->angle,o->base,12);
        size_t n=o->count*sizeof(ColTri);o->local=malloc(n);o->geometry[0]=malloc(n);o->geometry[1]=malloc(n);
        if(!o->local || !o->geometry[0] || !o->geometry[1] || fseek(bank,offsets[type],SEEK_SET) || fread(o->local,1,n,bank)!=n)goto done;
        for(unsigned j=0;j<o->count;j++)for(unsigned k=0;k<9;k++)if(!isfinite(o->local[j].v[k]))goto done;
        /* 090ff0 +bc initially selects the first (+c0) collider tree. The
         * second (+c4) tree is an alternate damaged state, not an extra mesh. */
        if(!object_publish(s,o,1))goto done;
    }
    if(col_rebuild(world))goto done;*owner=s;s=NULL;ok=1;
done:if(bank)fclose(bank);jgr_native_parked_free(&s);return ok;
}
