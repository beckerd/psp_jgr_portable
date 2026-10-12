#include "object_graffiti.h"
#include "spray.h"
#include "source_transform.h"
#include <math.h>
#include <string.h>

GfoPress gfo_press(GfoProgress *p,unsigned kind,unsigned anchors,int *cans,
                   unsigned flags,int edge,int blocked)
{
    GfoPress e={0};
    if((flags&0x10000) || !edge)return e;
    if(p->complete || (kind && !anchors && p->started) || (!kind && p->stroke)) {
        e.clear_target=1;return e;
    }
    if(blocked || *cans<1){e.empty=1;return e;}
    if(!kind) {
        if(!p->started){p->stroke=0;p->started=1;}
        e.score=300;p->reveal+=29;
        if(p->reveal>=288){p->stroke++;p->reveal=0;e.task=1;e.sound=1;}
    } else {
        if(anchors){e.large=1;return e;}
        p->started=1;e.score=500;e.sound=1;e.task=(flags&0x8000)?3:2;e.trial=1;
    }
    e.accepted=1;(*cans)--;return e;
}
uint32_t gfo_colour(const GfoProgress *p,uint32_t word,int mode)
{
    unsigned stroke=word>>16,start=word&65535;
    if((mode && stroke==25) || stroke<p->stroke)return 0xffffffffu;
    if(stroke>p->stroke || p->reveal<start)return 0;
    unsigned delta=p->reveal-start;
    return delta>=32 ? 0xffffffffu:((delta*255>>5)<<24)|0xffffffu;
}
static uint32_t word(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static int bank_read(GfoWorld *w,const void *data,size_t size,int append)
{
    const unsigned char *b=data;const GfoTemplate *table[64];
    if(!b || size<8 || memcmp(b,"GFO2",4) || ((uintptr_t)b&3))return -1;
    unsigned count=word(b+4);if(count>64 || (append && count>64-w->count))return -1;
    size_t at=8;
    for(unsigned i=0;i<count;i++) {
        if(size-at<sizeof(GfoTemplate))return -1;
        const GfoTemplate *t=(const GfoTemplate *)(b+at);
        if(t->bank>=64 || t->chain>=8 || t->index>=64 || t->kind>1 || !t->nbones || t->nbones>64 ||
           !t->nvertex || t->nvertex%3 || t->nvertex>4096)return -1;
        for(unsigned j=0;j<i;j++)if(table[j]->bank==t->bank && table[j]->chain==t->chain && table[j]->index==t->index)return -1;
        if(append)for(unsigned j=0;j<w->count;j++)if(w->templates[j]->bank==t->bank && w->templates[j]->chain==t->chain && w->templates[j]->index==t->index)return -1;
        at+=sizeof *t;
        if(t->nbones>(size-at)/sizeof(JgcBone))return -1;
        for(unsigned j=0;j<t->nbones;j++) {
            const JgcBone *bone=gfo_bones(t)+j;
            if(bone->parent < -1 || bone->parent >= (int)j || bone->batch0 || bone->nbatch)return -1;
            for(int k=0;k<3;k++)if(!isfinite(bone->pos[k]) || !isfinite(bone->scl[k]))return -1;
        }
        at+=t->nbones*sizeof(JgcBone);
        if(t->nvertex>(size-at)/sizeof(GfoVertex))return -1;
        /* Reject nonfinite original transforms/volumes and vertices. BAM
           rotations at descriptor +10/+14/+18 are integers, not floats. */
        for(unsigned j=0;j<228;j+=4)if(j<12 || j>=24) {
            float f;memcpy(&f,t->descriptor+j,4);if(!isfinite(f))return -1;
        }
        for(unsigned j=0;j<t->nvertex;j++) {
            const GfoVertex *v=(const GfoVertex *)(b+at+j*sizeof *v);
            if(!isfinite(v->u) || !isfinite(v->v) || v->bone>=t->nbones)return -1;
            for(int k=0;k<3;k++)if(!isfinite(v->pos[k]))return -1;
        }
        table[i]=t;at+=t->nvertex*sizeof(GfoVertex);
    }
    if(at!=size)return -1;
    if(!append){memset(w,0,sizeof *w);w->data=b;w->size=size;}
    memcpy(w->templates+w->count,table,count*sizeof *table);w->count+=count;return 0;
}
int gfo_bank(GfoWorld *w,const void *data,size_t size){return bank_read(w,data,size,0);}
int gfo_add_bank(GfoWorld *w,const void *data,size_t size){return bank_read(w,data,size,1);}
void gfo_reset(GfoWorld *w){memset(w->objects,0,sizeof w->objects);w->target=0;}
GfoObject *gfo_find(GfoWorld *w,unsigned handle)
{
    if(!handle)return NULL;
    for(unsigned i=0;i<GFO_CAPACITY;i++)if(w->objects[i].handle==handle)return w->objects+i;
    return NULL;
}
unsigned gfo_create(GfoWorld *w,unsigned parent,unsigned bank,unsigned chain,unsigned index)
{
    const GfoTemplate *t=NULL;if(!parent)return 0;
    for(unsigned i=0;i<w->count;i++)if(w->templates[i]->bank==bank && w->templates[i]->chain==chain && w->templates[i]->index==index)t=w->templates[i];
    if(!t)return 0;
    for(unsigned i=0;i<GFO_CAPACITY;i++)if(!w->objects[i].handle) {
        GfoObject *o=w->objects+i;memset(o,0,sizeof *o);o->parent=parent;o->tpl=t;
        o->handle=0x20000000u|(++w->serial&0x0fffffffu);
        float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        gfo_transform(o,identity);return o->handle;
    }
    return 0;
}
void gfo_delete_parent(GfoWorld *w,unsigned parent)
{
    for(unsigned i=0;i<GFO_CAPACITY;i++)if(w->objects[i].handle && w->objects[i].parent==parent) {
        if(w->target==w->objects[i].handle)w->target=0;
        memset(w->objects+i,0,sizeof(GfoObject));
    }
}
unsigned gfo_word(GfoWorld *w,unsigned handle,unsigned offset)
{
    GfoObject *o=gfo_find(w,handle);if(!o)return 0;
    switch(offset){case 0x144:return o->progress.stroke;case 0x148:return o->progress.reveal;
    case 0x14c:return o->progress.complete;case 0x150:return o->progress.started;case 0x154:return o->progress.busy;default:return 0;}
}
void gfo_transform(GfoObject *o,const float m[16])
{
    memcpy(o->matrix,m,64);
    float points[14][3],out[14][3];
    memcpy(points[0],o->tpl->descriptor+0x24,12);
    memcpy(points[1],o->tpl->descriptor+0x30,12);
    for(int i=0;i<6;i++) {
        memcpy(points[2+i],o->tpl->descriptor+0x54+i*12,12);
        memcpy(points[8+i],o->tpl->descriptor+0x9c+i*12,12);
    }
    source_column_points(m,points,out,(0x3fu<<8)|2u,14);
    memcpy(o->center,out[0],12);memcpy(o->normal,out[1],12);
    memcpy(o->volume,out[2],72);memcpy(o->volume+18,out[8],72);
}
int gfo_contains(const GfoObject *o,const float p[3])
{
    for(int i=0;i<6;i++) {
        float x=o->volume[i*3]-p[0],y=o->volume[i*3+1]-p[1],z=o->volume[i*3+2]-p[2];
        double d=(double)x*o->volume[18+i*3];d+=(double)y*o->volume[19+i*3];d+=(double)z*o->volume[20+i*3];
        if((float)d<0.f)return 0;
    }
    return 1;
}
void gfo_select(GfoWorld *w,const float p[3])
{
    w->target=0;int last=0;
    for(unsigned i=0;i<GFO_CAPACITY;i++) {
        GfoObject *o=w->objects+i;
        if(o->handle && o->registered>last && !o->progress.complete && gfo_contains(o,p)) {
            w->target=o->handle;last=o->registered;
        }
    }
}
unsigned gfo_tick(GfoWorld *w)
{
    unsigned events=0;
    for(unsigned i=0;i<GFO_CAPACITY;i++) {
        GfoObject *o=w->objects+i;
        if(o->handle && o->task && spray_reveal_tick(&o->progress.stroke,&o->progress.reveal,&o->progress.complete)) {
            if(o->task==2)events|=1;
            if(o->task==3)events|=2;
            o->task=0;
        }
    }
    return events;
}
GfoPress gfo_spray(GfoWorld *w,int *cans,unsigned flags,int edge,int blocked)
{
    GfoObject *o=gfo_find(w,w->target);if(!o)return (GfoPress){0};
    GfoPress e=gfo_press(&o->progress,o->tpl->kind,0,cans,flags,edge,blocked);
    if(e.clear_target)w->target=0;
    if(e.task){o->task=e.task;if(e.task!=1)o->progress.stroke=0;} /* 03b814 */
    return e;
}
