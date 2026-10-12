#include "diagnostics.h"
#include "pathworld.h"
#include "source_probe.h"
#include "source_trs.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef __PSP__
#include <pspkernel.h>
#endif
static PathWorldProfile profile;
static unsigned path_clock(void)
{
#if defined(__PSP__) && JGR_DIAGNOSTICS
    return sceKernelGetSystemTimeLow();
#else
    return 0;
#endif
}
void pathworld_profile_read(PathWorldProfile *out)
{*out=profile;profile=(PathWorldProfile){0};}
void pathworld_profile_totals(PathWorldProfile *out)
{*out=profile;}

static unsigned word(const unsigned char *p){unsigned v;memcpy(&v,p,4);return v;}
static int range(size_t length,unsigned at,size_t n){return at<=length && n<=length-at;}
static unsigned identity(unsigned generation,unsigned slot){return 0x20000000u|(generation<<6)|slot;}
static PathInstance *find(PathWorld *w,unsigned h)
{
    if(!w || (h&0xe0000000u)!=0x20000000u)return NULL;
    PathInstance *p=&w->slots[h&63];
    return p->active && p->task.handle==h ? p:NULL;
}
const PathInstance *pathworld_find(const PathWorld *w,unsigned h){return find((PathWorld *)w,h);}
int pathworld_bind(PathWorld *w,const void *image,size_t length,Collision *col)
{
    if(!w || !image || length<32)return -1;
    const unsigned char *b=image;
    unsigned stage=word(b+4),nt=word(b+8),np=word(b+12),ot=word(b+16),op=word(b+20),oi=word(b+24),sz=word(b+28);
    if(memcmp(b,"PTH1",4) || !(stage==1||stage==2||stage==3||stage==5||stage==6) ||
       !nt || nt>16 || !np || np>16 || ot!=32 || op!=ot+nt*48 || oi!=op+np*16 ||
       !range(length,oi,sz) || (size_t)oi+sz!=length || sz>0x10000000u)return -2;
    /* Bind only an empty owner. Reset first, so borrowed contacts cannot be
       abandoned or redirected to a different level's geometry. */
    if(w->count)return -3;
    Scene s={0};s.image=b+oi;s.length=sz;s.base=0x10000000u;
    for(unsigned i=0;i<nt;i++) {
        unsigned dir=word(b+ot+i*48+44);if(!scene_bytes(&s,dir,8))return -4;
        for(unsigned j=0;j<2;j++){unsigned node=scene_word(&s,dir+j*4);if(node && !scene_bytes(&s,node,68))return -4;}
    }
    for(unsigned i=0;i<np;i++) {
        const unsigned char *r=b+op+i*16;unsigned motion=word(r+4),bones=word(r+8),frames=word(r+12);
        if(!motion){if(word(r)||bones||frames)return -5;continue;}
        if(!bones || bones>64 || !frames || frames>10000 || !scene_bytes(&s,motion,12) || scene_word(&s,motion+4)!=frames)return -5;
        unsigned kind=scene_word(&s,motion+8),channels=kind>>16;
        if((kind!=0x20003 && kind!=0x30007) || !scene_bytes(&s,scene_word(&s,motion),bones*channels*8u))return -5;
    }
    w->source=s;w->bank=b;w->length=length;w->stage=stage;w->ntypes=nt;w->npaths=np;w->types=ot;w->paths=op;
    w->collision=col;w->error=0;w->clock=0;return 0;
}
int pathworld_bind_grips(PathWorld *w,const void *image,size_t length)
{
    if(!w || !w->bank || !image || length!=12+w->ntypes*160)return -1;
    const unsigned char *b=image;
    if(memcmp(b,"GRP1",4)||word(b+4)!=w->stage||word(b+8)!=w->ntypes)return -2;
    for(unsigned i=0;i<w->ntypes;i++) {
        if(word(b+12+i*160)!=word(w->bank+w->types+i*48+20))return -3;
        for(unsigned j=0;j<39;j++){float f;memcpy(&f,b+16+i*160+j*4,4);if(!isfinite(f))return -4;}
    }
    w->grips=b;return 0;
}
static void grip_transform(const float m[12],const float v[3],float out[3],int vector)
{
    for(int r=0;r<3;r++)out[r]=(float)((double)m[r*4]*v[0]+(double)m[r*4+1]*v[1]+(double)m[r*4+2]*v[2]+(vector ? 0.:m[r*4+3]));
}
static void grip_integrate(PathWorld *w,PathInstance *p)
{
    if(!w->grips || !p->slip_source || !p->near_surface){p->grip_valid=0;return;}
    const float *shape=(const float *)(w->grips+16+p->task.type*160);
    float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};const float one[3]={1,1,1};
    source_trs_apply(p->task.pos,p->task.rotation,one,m,player_fsca_sin);
    /* The reference origin is the authored rear anchor, not the model root. */
    float center[3];grip_transform(m,shape+36,center,0);
    memcpy(p->grip_previous,p->grip_matrix,sizeof p->grip_matrix);
    memcpy(p->grip_matrix,m,sizeof p->grip_matrix);
    for(int r=0;r<3;r++)p->grip_matrix[r*4+3]=center[r];
    if(!p->grip_valid)memcpy(p->grip_previous,p->grip_matrix,sizeof p->grip_matrix);
    p->grip_valid=1;
}
/* 0c9080: express the query in the current basis, then subtract its prior
   world position. Keep Cramer's-rule products in source evaluation order. */
static int grip_velocity(const float m[12],const float old[12],const float q[3],float out[3])
{
    float a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    float det=(((i*a*e-f*a*h)-i*d*b)+c*d*h+f*g*b)-c*g*e;
    if(det==0.f)return 0;
    float x=q[0]-m[3],y=q[1]-m[7],z=q[2]-m[11];
    float u=((((i*x*e-f*x*h)-i*d*y)+z*d*h+f*g*y)-z*g*e)/det;
    float v=((((i*a*y-z*a*h)-i*x*b)+c*x*h+z*g*b)-c*g*y)/det;
    float t=((((z*a*e-f*a*y)-z*d*b)+c*d*y+f*x*b)-c*x*e)/det;
    for(int r=0;r<3;r++)out[r]=q[r]-(((old[r*4]*u+old[r*4+3])+old[r*4+1]*v)+old[r*4+2]*t);
    return 1;
}
int pathworld_grip_query(void *context,const float point[3],CarPullContact *out)
{
    PathWorld *w=context;memset(out,0,sizeof *out);if(!w || !w->grips)return 0;
    for(unsigned i=0;i<w->count;i++) {
        const PathInstance *p=w->slots+w->order[i];if(!p->grip_valid)continue;
        const float *shape=(const float *)(w->grips+16+p->task.type*160);
        /* Plane positions use the model root; the motion basis uses anchor. */
        float m[12];memcpy(m,p->grip_matrix,sizeof m);
        for(int r=0;r<3;r++)m[r*4+3]=p->task.pos[r];
        int inside=1;
        for(int plane=0;plane<6;plane++) {
            float a[3],normal[3],d[3];grip_transform(m,shape+plane*3,a,0);grip_transform(m,shape+18+plane*3,normal,1);
            for(int r=0;r<3;r++)d[r]=a[r]-point[r];
            if(probe_ninja_dot(d,normal)<0.f){inside=0;break;}
        }
        if(inside && grip_velocity(p->grip_matrix,p->grip_previous,point,out->velocity)) {
            for(int r=0;r<3;r++){out->anchor[r]=p->grip_matrix[r*4+3];out->translation[r]=out->anchor[r]-p->grip_previous[r*4+3];}
            out->handle=p->task.handle;
            /* Original loop publishes later overlapping objects last. */
        }
    }
    return !!out->handle;
}
static void release(PathWorld *w,PathInstance *p)
{
    if(!p->active)return;
    if(p->surface)col_surface_remove(w->collision,p->surface);
    if(p->task.sound && w->sound_output)w->sound_output(w->context,p,PATH_SOUND_DELETE,p->task.sound);
    if(w->released)w->released(w->context,p->task.event,p->task.handle);
    free(p->vertices);free(p->geometry[0]);free(p->geometry[1]);
    unsigned slot=(unsigned)(p-w->slots),generation=p->generation;
    for(unsigned i=0;i<w->count;i++)if(w->order[i]==slot){memmove(w->order+i,w->order+i+1,(w->count-i-1)*sizeof *w->order);break;}
    memset(p,0,sizeof *p);p->generation=generation;w->count--;
}
void pathworld_reset(PathWorld *w)
{
    if(!w)return;
    for(unsigned i=0;i<64;i++)release(w,&w->slots[i]);
    w->clock=0;w->error=0;
}
unsigned pathworld_spawn(PathWorld *w,unsigned event,const unsigned args[7])
{
    if(!w || !w->bank || !args || !event || args[6] || args[1]>=w->ntypes || args[2]>=w->npaths)return 0;
    /* The event owns at most one task even if a caller has not yet published
       its handle. Source mode is allowed outside 0..5, as the kernel is. */
    for(unsigned i=0;i<64;i++)if(w->slots[i].active && w->slots[i].task.event==event)return 0;
    unsigned slot=0;while(slot<64 && (w->slots[slot].active || w->slots[slot].generation==0x7fffffu))slot++;
    if(slot==64)return 0;
    const unsigned char *type=w->bank+w->types+args[1]*48,*path=w->bank+w->paths+args[2]*16;
    unsigned motion=word(path+4),frames=word(path+12);if(!motion || !frames)return 0;
    PathInstance p={0};p.generation=w->slots[slot].generation+1;
    pathobject_init(&p.task,frames,args[3],args[4],(int)args[0]);
    p.task.type=args[1];p.task.track=args[2];p.task.event=event;p.task.handle=identity(p.generation,slot);
    p.motion=motion;p.subject=args[5];p.sound_program=(int)word(type+24);p.slip_source=word(type+20);
    for(unsigned j=0;j<2;j++){p.node[j]=scene_word(&w->source,word(type+44)+j*4);p.model[j]=word(type+36+j*4);p.pulse_bone[j]=word(type+28+j*4);}
    p.task.special=p.pulse_bone[0]!=UINT_MAX && p.pulse_bone[1]!=UINT_MAX;
    /* 06b024 allocates cache A only for kind 6 and cache B only for a
       NON-kind-6 descriptor. Preserve null and intentionally unused banks. */
    if(p.node[0] && (scene_word(&w->source,p.node[0])&65535)==6) {
        unsigned mesh=scene_word(&w->source,p.node[0]+4);
        if(!scene_bytes(&w->source,mesh,12))goto fail;
        p.nvertices=scene_word(&w->source,mesh+8)&65535;
        if(!p.nvertices || p.nvertices>1024)goto fail;
        p.vertices=malloc(p.nvertices*12u);if(!p.vertices)goto fail;
        if(pathcoll_sample(&w->source,p.node[0],0,0,p.vertices,p.nvertices,NULL)!=(int)p.nvertices)goto fail;
        int n=pathcoll_surfaces(&w->source,p.node[0],p.vertices,p.nvertices,NULL,0);if(n<=0)goto fail;p.ntri=(unsigned)n;
        for(unsigned j=0;j<2;j++) {
            p.geometry[j]=malloc(p.ntri*sizeof(ColTri));if(!p.geometry[j])goto fail;
            if(pathcoll_surfaces(&w->source,p.node[0],p.vertices,p.nvertices,p.geometry[j],p.ntri)!=n)goto fail;
        }
    }
    if(p.node[1] && (scene_word(&w->source,p.node[1])&65535)!=6) {
        if((scene_word(&w->source,p.node[1])&65535)!=4 || pathcoll_body(&w->source,p.node[1],0,0,p.body,p.body_center,p.body_bounds))goto fail;
        memcpy(p.body_previous,p.body,sizeof p.body);p.body_flags=scene_word(&w->source,p.node[1])>>16;p.body_valid=1;
        memcpy(p.body_old_center,p.body_center,12);
    }
    p.active=1;w->slots[slot]=p;w->order[w->count++]=slot;return p.task.handle;
fail:
    free(p.vertices);free(p.geometry[0]);free(p.geometry[1]);return 0;
}
void pathworld_control(PathWorld *w,unsigned h,int opcode)
{
    PathInstance *p=find(w,h);if(!p)return;
    if(opcode==0xa)release(w,p);else if(opcode==0xb)pathobject_resume(&p->task);
}
static float distance2(const float *a,const float *b)
{/* 0c284c/0c2b7c use separately rounded FMUL/FADD, not FIPR. */
 float d[3];for(int j=0;j<3;j++)d[j]=a[j]-b[j];return (d[0]*d[0]+d[1]*d[1])+d[2]*d[2];}
int pathworld_body_prepare(PathWorld *w,unsigned handle)
{
    PathInstance *p=find(w,handle);if(!p || !p->body_valid)return -1;
    if(!p->body_dirty)return 0;
    unsigned started=path_clock();float ignored[4];
    int error=0;
    /* A nearby/towed vehicle is normally queried on successive ticks.
       Its previous exact box is already resident. Only reconstruct history
       after skipped queries; never substitute the last queried tick for
       the actual previous simulation tick. */
    if(p->body_cached_animated==p->body_old_animated &&
       (!p->body_old_animated || p->body_cached_frame==p->body_old_frame)) {
        memcpy(p->body_previous,p->body,sizeof p->body);
        memcpy(p->body_old_center,p->body_center,sizeof p->body_center);
    } else error=pathcoll_body(&w->source,p->node[1],p->body_old_animated ? p->motion:0,
        p->body_old_frame,p->body_previous,p->body_old_center,ignored);
    if(!error)error=pathcoll_body(&w->source,p->node[1],p->motion,p->body_frame,
        p->body,p->body_center,p->body_bounds);
    unsigned elapsed=path_clock()-started;
    profile.body_us+=elapsed;profile.total_us+=elapsed;profile.body_prepared++;
    if(error){w->error=-4;return w->error;}
    p->body_cached_frame=p->body_frame;p->body_cached_animated=1;
    p->body_dirty=0;return 0;
}
int pathworld_tick(PathWorld *w,const float player[3],const float camera[3])
{
    if(!w || !w->bank || !player || !camera)return -1;
    if(w->error)return w->error;
    for(int k=0;k<3;k++)if(!isfinite(player[k]) || !isfinite(camera[k]))return -2;
    unsigned started=path_clock();profile.ticks++;
    w->clock++;
    unsigned context_motion=0;float context_frame=0;
    unsigned handles[64],count=w->count;
    for(unsigned i=0;i<count;i++)handles[i]=w->slots[w->order[i]].task.handle;
    for(unsigned i=0;i<count;i++) {
        PathInstance *p=find(w,handles[i]);if(!p)continue;
        unsigned part=path_clock();profile.objects++;
        PathObjectInput in={0};memcpy(in.player,player,12);
        if(p->task.state==0 || p->task.state==1) {
            CharPose pose={.scl={1,1,1}};
            /* 235e74 returns without changing Ninja's global context when
               frame >= frames. Collision traversal has its own local keys
               (0bea0c/0bf0b4) and does not replace that context. The shipped
               start=10000 events therefore sample the preceding task's OLD
               root frame once, then their own clock wraps in 06b3bc. Do not
               guess a context from outside this batch if its first task is
               invalid: report the unbound source dependency. */
            if(p->task.frame<(float)p->task.frames){context_motion=p->motion;context_frame=p->task.frame;}
            if(!context_motion || !scene_sample_njs(&w->source,context_motion,context_frame,0,0,&pose)){w->error=-3;return w->error;}
            memcpy(in.position,pose.pos,12);memcpy(in.rotation,pose.ang,12);
        }
        if(w->sound_input)w->sound_input(w->context,p,&in);
        else in.sound_blocked=1; /* Explicit unbound consumer, not fake playback. */
        unsigned old_sound=p->task.sound;
        unsigned events=pathobject_tick(&p->task,&in);
        profile.root_us+=path_clock()-part;
        if(events&PATH_DELETE){
            if((events&PATH_SOUND_DELETE) && old_sound && w->sound_output)
                w->sound_output(w->context,p,PATH_SOUND_DELETE,old_sound);
            release(w,p);continue;
        }
        if(events&PATH_INTEGRATE) {
            part=path_clock();
            float body[24],center[3],bounds[4];unsigned next=p->buffer^1;
            int near=distance2(p->task.pos,player)<12100.f;
            if(p->body_valid) {
                p->body_old_frame=p->body_frame;p->body_frame=p->task.frame;p->body_old_animated=p->steps!=0;
                if(w->defer_bodies) {
                    if(pathcoll_body_bound(&w->source,p->node[1],p->motion,p->task.frame,p->body_coarse)){w->error=-4;return w->error;}
                    p->body_dirty=1;
                } else {
                    if(pathcoll_body(&w->source,p->node[1],p->motion,p->task.frame,body,center,bounds)){w->error=-4;return w->error;}
                    profile.body_prepared++;
                }
            }
            profile.body_us+=path_clock()-part;part=path_clock();
            if(near && p->ntri) {
                if(pathcoll_sample(&w->source,p->node[0],p->motion,p->task.frame,p->vertices,p->nvertices,NULL)!=(int)p->nvertices ||
                   pathcoll_surfaces(&w->source,p->node[0],p->vertices,p->nvertices,p->geometry[next],p->ntri)!=(int)p->ntri){w->error=-5;return w->error;}
                if(w->collision) {
                    if(p->surface) {
                        if(!col_surface_publish(w->collision,p->surface,p->geometry[next],p->geometry[p->buffer],p->ntri)){w->error=-6;return w->error;}
                    } else {
                        p->surface=col_surface_add(w->collision,p->geometry[next],p->geometry[p->buffer],p->ntri);
                        if(!p->surface){w->error=-6;return w->error;}
                    }
                }
                p->buffer=next;
            } else if(p->surface) {col_surface_remove(w->collision,p->surface);p->surface=0;}
            if(p->body_valid && !w->defer_bodies){memcpy(p->body_previous,p->body,sizeof p->body);memcpy(p->body,body,sizeof body);
                memcpy(p->body_old_center,p->body_center,12);memcpy(p->body_center,center,12);memcpy(p->body_bounds,bounds,16);
                p->body_cached_frame=p->body_frame;p->body_cached_animated=1;}
            p->near_surface=near;p->near_model=distance2(p->task.pos,camera)<90000.f;
            p->close_draw=distance2(p->task.pos,player)<2500.f;p->steps++;
            profile.surface_us+=path_clock()-part;part=path_clock();
            grip_integrate(w,p);
            profile.grip_us+=path_clock()-part;
            if(w->integrated)w->integrated(w->context,p);
        }
        if(w->sound_output)w->sound_output(w->context,p,events&(PATH_SOUND_CREATE|PATH_SOUND_DELETE|PATH_SOUND_POSITION),
                                         events&PATH_SOUND_DELETE?old_sound:(unsigned)p->task.sound);
    }
    profile.total_us+=path_clock()-started;return 0;
}

unsigned pathworld_body_candidates(const PathWorld *w,const float low[3],const float high[3],unsigned handles[PATHWORLD_CAPACITY])
{
    if(!w || !low || !high || !handles)return 0;
    unsigned n=0;
    for(unsigned i=0;i<w->count;i++) {
        const PathInstance *p=&w->slots[w->order[i]];
        if(!p->active || !p->body_valid || !p->steps)continue;
        const float *bound=w->defer_bodies ? p->body_coarse:p->body_bounds;
        float radius=bound[3]*(w->defer_bodies ? 2.f:1.f);int reject=0;
        for(int j=0;j<3;j++) {
            /* Outward slack covers the differently associated float sums.
               Unknown bounds fall through to the original narrow phase. */
            float slack=.001f+(fabsf(bound[j])+fabsf(radius)+fabsf(low[j])+fabsf(high[j]))*1e-5f;
            if(isfinite(slack) && radius>=0 && (bound[j]+radius+slack<low[j] || bound[j]-radius-slack>high[j]))reject=1;
        }
        if(!reject)handles[n++]=p->task.handle;
    }
    return n;
}

int pathworld_candidate_contacts(const PathWorld *w,const unsigned *handles,unsigned count,const float sphere[4],
    const float center[3],const float previous[3],unsigned flags,
    unsigned layer,unsigned accepts,PathBodyHit *hits,unsigned capacity)
{
    if(!w || !sphere || !center || !previous || !hits || !capacity)return 0;
    for(int j=0;j<4;j++)if(!isfinite(sphere[j]))return 0;
    if(sphere[3]<0)return 0;
    for(int j=0;j<3;j++)if(!isfinite(center[j]) || !isfinite(previous[j]))return 0;
    /* 06b024 -> 0b6d5c(8,1); 0b5f90 accepts either cross-direction mask. */
    if(!((layer&1)||(accepts&8)))return 0;
    unsigned n=0;
    for(unsigned i=0;i<(handles ? count:w->count);i++) {
        const PathInstance *p=handles ? pathworld_find(w,handles[i]):&w->slots[w->order[i]];
        if(!p || !p->active || !p->body_valid || !p->steps)continue;
        if(w->defer_bodies) {
            float reach=(p->body_coarse[3]+sphere[3])*2.f;
            if(isfinite(reach) && (fabsf(p->body_coarse[0]-sphere[0])>reach ||
               fabsf(p->body_coarse[1]-sphere[1])>reach || fabsf(p->body_coarse[2]-sphere[2])>reach))continue;
            if(pathworld_body_prepare((PathWorld *)w,p->task.handle))return 0;
        }
        float d[3];for(int j=0;j<3;j++)d[j]=p->body_bounds[j]-sphere[j];
        /* 2711a4's bounding-sphere rejection. */
        float radii=p->body_bounds[3]+sphere[3];
        if(!(radii>body_length(d)) || !source_body_box_sphere(p->body,sphere))continue;
        PathBodyHit hit;hit.handle=p->task.handle;
        hit.distance=source_actor_contact(&hit.contact,center,previous,flags,p->body_center,p->body_old_center,p->body_flags);
        if(n==capacity && !(hits[n-1].distance>hit.distance))continue;
        unsigned at=n<capacity ? n:capacity-1;
        while(at && hits[at-1].distance>hit.distance){hits[at]=hits[at-1];at--;}
        hits[at]=hit;if(n<capacity)n++;
    }
    return (int)n;
}

int pathworld_sphere_contacts(const PathWorld *w,const float sphere[4],
    const float center[3],const float previous[3],unsigned flags,
    unsigned layer,unsigned accepts,PathBodyHit *hits,unsigned capacity)
{return pathworld_candidate_contacts(w,NULL,0,sphere,center,previous,flags,layer,accepts,hits,capacity);}
