#include "scene.h"
#include <math.h>
#include <string.h>
#include "charpose.h"
#include "source_fmac.h"
#include "source_trs.h"
#include "font_source.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_scene_tables.inc"
#else
#include "generated/source_data/scene_tables.inc"
#endif

int scene_range(const Scene *s,uint32_t a,size_t n)
{
    return (s->image || s->pages) && a>=s->base && a-s->base<=s->length && n<=s->length-(a-s->base);
}
const void *scene_bytes(const Scene *s,uint32_t a,size_t n)
{
    if(!scene_range(s,a,n))return NULL;
    unsigned offset=a-s->base;
    if(s->image)return s->image+offset;
    unsigned in_page=offset&(SCENE_PAGE_SIZE-1);
    if(offset==s->length || n>SCENE_PAGE_SIZE-in_page)return NULL;
    return s->pages[offset>>SCENE_PAGE_SHIFT]+in_page;
}
int scene_read(const Scene *s,uint32_t a,void *out,size_t n)
{
    if(!scene_range(s,a,n) || (n && !out))return 0;
    if(!n)return 1;
    unsigned offset=a-s->base;
    if(s->image){memcpy(out,s->image+offset,n);return 1;}
    unsigned char *dst=out;
    while(n) {
        unsigned in_page=offset&(SCENE_PAGE_SIZE-1),part=SCENE_PAGE_SIZE-in_page;
        if(part>n)part=n;
        memcpy(dst,s->pages[offset>>SCENE_PAGE_SHIFT]+in_page,part);
        dst+=part;offset+=part;n-=part;
    }
    return 1;
}
uint32_t scene_word(const Scene *s,uint32_t a)
{
    /* Keep ordinary traffic/collision reads and aligned EVENT words on the
       fixed-size load path; only an unaligned page crossing needs a copy. */
    uint32_t v=0;const void *p=scene_bytes(s,a,4);
    if(p)memcpy(&v,p,4);else scene_read(s,a,&v,4);
    return v;
}
static float sf(const Scene *s,uint32_t a)
{ uint32_t u=scene_word(s,a);float f;memcpy(&f,&u,4);return f; }
int scene_clip(int ref) { return (unsigned)ref<170 ? SCENE_CLIP[ref] : -1; }
int scene_frames(int c,int clip)
{ return (unsigned)c<16 && (unsigned)clip<176 ? SCENE_FRAMES[c][clip] : 1; }

/* EVENT CNK keys retain their original virtual addresses. Like converted
   PLAY keys, each present channel contains one dense key per source frame. */
int scene_sample_cnk(const Scene *s,uint32_t motion,float frame,unsigned bone,CharPose *pose)
{
    if(!pose || !scene_range(s,motion,8) || !isfinite(frame) || frame<0 || bone>=CHAR_RENDER_MAX_BONES)return 0;
    unsigned frames=scene_word(s,motion)>>16,md=scene_word(s,motion+4);
    if(!frames || frame>=frames || !scene_range(s,md,(bone+1)*16))return 0;
    unsigned k=(unsigned)frame,k1=k+1<frames?k+1:0;float t=frame-k;
    CharPose result=*pose;
    for(unsigned channel=0;channel<3;channel++) {
        unsigned address=scene_word(s,md+bone*16+4+channel*4),stride=channel==1?6:12;
        if(!address)continue;
        if(!scene_range(s,address,(size_t)frames*stride))return 0;
        unsigned char keys[2][12];
        if(!scene_read(s,address+k*stride,keys[0],stride) ||
           !scene_read(s,address+k1*stride,keys[1],stride))return 0;
        for(unsigned j=0;j<3;j++) {
            if(channel==1) {
                int16_t a,b;memcpy(&a,keys[0]+j*2,2);memcpy(&b,keys[1]+j*2,2);
                result.ang[j]=a+(int)((int16_t)(b-a)*t);
            } else {
                float a,b;memcpy(&a,keys[0]+j*4,4);memcpy(&b,keys[1]+j*4,4);
                if(channel==0)result.pos[j]=a+(b-a)*t;else result.scl[j]=a+(b-a)*t;
            }
        }
        result.keyed|=1u<<channel;result.ev&=~(1u<<channel);
    }
    *pose=result;return 1;
}

int scene_effect_init(SceneEffect *e,int type,const float pos[3],unsigned random15)
{
    if(!e || (unsigned)type>2 || random15>=32768)return 0;
    memset(e,0,sizeof *e);e->active=1;e->type=type;memcpy(e->pos,pos,sizeof e->pos);
    e->scale=type==2 ? 1.5f:1.f;
    /* 0ddbac: retain the original intermediate rounding through degrees. */
    e->yaw=(int32_t)((((float)random15/32768.f)*360.f*65536.f)/360.f);
    return 1;
}
void scene_effect_tick(SceneEffect *e)
{
    if(!e->active)return;
    /* 0ddd08: fade uses the OLD motion frame; increment then clamp each
       clip independently. The task lifetime is a third independent clock. */
    if(e->mode==0)e->mode=1;
    else if(e->mode==1){e->sound_serial++;e->mode=2;} /* 096294(-fff0), 0ce56c(17) */
    else if(e->mode==2) {
        float a=e->type==0 ? 16.f:50.f,b=e->type==0 ? 26.f:76.f;
        if(e->frame[0]>=a && e->frame[0]<=b)e->alpha_offset[0]=-(e->frame[0]-a)/(b-a);
        a=e->type==0 ? 4.f:5.f;b=e->type==0 ? 11.f:12.f;
        if(e->frame[1]>=a && e->frame[1]<=b)e->alpha_offset[1]=-(e->frame[1]-a)/(b-a);
        e->frame[0]=fminf(e->frame[0]+1.f,e->type==0 ? 28.f:78.f);
        e->frame[1]=fminf(e->frame[1]+1.f,e->type==0 ? 13.f:14.f);
        if(e->age>=(e->type==0 ? 28.f:77.f))e->mode=3;
    } else if(e->mode==3)e->active=0;
    e->age+=1.f;
}

static int sample_njs(const Scene *s,uint32_t motion,float frame,unsigned bone,int cel,CharPose *pose)
{
    if(!pose || !scene_range(s,motion,12) || !isfinite(frame) || frame<0)return 0;
    uint32_t frames=scene_word(s,motion+4),kind=scene_word(s,motion+8);
    unsigned channels=kind==0x30007 ? 3:kind==0x20003 ? 2:0;
    if(!channels || !frames || frame>=frames || bone>=CHAR_RENDER_MAX_BONES)return 0;
    uint32_t md=scene_word(s,motion);
    if(!scene_range(s,md,(bone+1)*channels*8))return 0;
    CharPose result=*pose;uint32_t entry=md+bone*channels*8;
    /* 0c01b0 always passes rotation order 0. Ninja's ordinary path retains
       the object's rotation-order flag. Missing cel channels retain eval bits. */
    if(cel)result.ev&=~0x20u;else result.ev&=~7u;
    for(unsigned ch=0;ch<channels;ch++) {
        uint32_t p=scene_word(s,entry+ch*4),n=scene_word(s,entry+channels*4+ch*4);
        if(!p)continue;
        if(!n || n>s->length/16 || !scene_range(s,p,n*16))return 0;
        uint32_t lo=0,hi=n;float t;
        if(cel) {
            uint32_t next=cel==2 ? (uint32_t)ceilf(frame):(uint32_t)frame+1;
            if(next>=frames || !next){lo=n-1;hi=0;t=frame-(uint32_t)frame;}
            else {
                /* First key >= the NEXT integer frame, not <= this frame. */
                while(lo<hi){uint32_t k=lo+(hi-lo)/2;if(scene_word(s,p+k*16)<next)lo=k+1;else hi=k;}
                if(!lo || lo>=n)return 0;
                hi=lo;lo--;
                uint32_t a=scene_word(s,p+lo*16),b=scene_word(s,p+hi*16);
                if(b<=a)return 0;
                t=(frame-(float)a)/(float)(b-a);
            }
        } else {
            /* 2386ae/23873e/2387ce binary search + 237b96/237c44. */
            while(hi-lo>1){uint32_t k=(lo+hi)/2;if(scene_word(s,p+k*16)<=(uint32_t)frame)lo=k;else hi=k;}
            hi=(lo+1)%n;
            uint32_t a=scene_word(s,p+lo*16),b=scene_word(s,p+hi*16);
            t=(frame-(float)a)/(b>a ? (float)(b-a):1.f);
        }
        for(unsigned j=0;j<3;j++) {
            uint32_t a=p+lo*16+4+j*4,b=p+hi*16+4+j*4;
            if(ch==1) {
                int32_t av=(int32_t)scene_word(s,a),bv=(int32_t)scene_word(s,b);
                /* Cel angles use separate FMUL/FADD and exact endpoint
                   copies. Ordinary Ninja uses FMAC of weighted endpoints. */
                if(cel)result.ang[j]=t==0 ? av:t==1 ? bv:(int32_t)((float)(int32_t)((uint32_t)bv-(uint32_t)av)*t+(float)av);
                else result.ang[j]=(int32_t)source_fmac(1.f-t,(float)av,(float)bv*t);
            } else {
                float av=sf(s,a),bv=sf(s,b);
                float v=cel ? source_fmac(t,bv-av,av):source_fmac(1.f-t,av,bv*t);
                if(!isfinite(v))return 0;
                if(ch==0)result.pos[j]=v;else result.scl[j]=v;
            }
        }
        result.ev&=~(1u<<ch);
    }
    *pose=result;return 1;
}
int scene_sample_njs(const Scene *s,uint32_t motion,float frame,unsigned bone,int cel,CharPose *pose)
{ return bone<128 && sample_njs(s,motion,frame,bone,cel ? 1:0,pose); }
#include "scene_njs_cel.inc"
int scene_sample_njs_nodes(const Scene *s,uint32_t motion,float frame,unsigned bone,int cel,unsigned nodes,CharPose *pose)
{
    if(!nodes || nodes>CHAR_RENDER_MAX_BONES || bone>=nodes || !scene_range(s,motion,12))return 0;
    unsigned kind=scene_word(s,motion+8),channels=kind==0x30007 ? 3:kind==0x20003 ? 2:0;
    if(!channels || !scene_range(s,scene_word(s,motion),nodes*channels*8))return 0;
    return sample_njs(s,motion,frame,bone,cel ? 1:0,pose);
}
int scene_sample_collision(const Scene *s,uint32_t motion,float frame,unsigned bone,CharPose *pose)
{
    /* 0bea0c/0bf0b4 use ceil(frame), unlike the cel renderer's floor+1.
       At frame zero they take the last/first key branch with fraction zero.
       Collision descriptors always evaluate all bind TRS, not NJS eval bits. */
    if(!sample_njs(s,motion,frame,bone,2,pose))return 0;
    pose->ev=0;return 1;
}

/* 0f7d9c/0f7e34/0f7ee0: dense NJS_MDATA3 streams with 16-byte keys.
   Last key alone does NOT overwrite the current transform. Angles are signed
   32-bit values interpolated linearly, not shortest-arc CNK s16 rotations. */
void scene_sample_actor(const Scene *s,uint32_t track,float frame,SceneActor *a)
{
    if (!isfinite(frame) || frame<0 || !scene_range(s,track,12)) return;
    uint32_t md=scene_word(s,track);int k=(int)frame;float f=frame-k,g=1.f-f;
    if (!scene_range(s,md,24)) return;
    for (int ch=0;ch<3;ch++) {
        uint32_t p=scene_word(s,md+ch*4),n=scene_word(s,md+12+ch*4);
        if (!p || n<2 || (unsigned)k>=n-1 || !scene_range(s,p+(uint32_t)k*16,32)) continue;
        for (int j=0;j<3;j++) {
            uint32_t q=p+k*16+4+j*4;
            if (ch==1) a->angle[j]=(int32_t)((float)(int32_t)scene_word(s,q)*g+(float)(int32_t)scene_word(s,q+16)*f);
            else { float v=sf(s,q)*g+sf(s,q+16)*f;if (ch==0)a->pos[j]=v;else a->scale[j]=v; }
        }
    }
}
/* 0cfdd4: the camera's second stream contains world targets, not direction
   vectors. Each sample starts from the authored 64-byte NJS_CAMERA defaults. */
void scene_sample_camera(const Scene *s,uint32_t initial,uint32_t track,float frame,SceneCamera *c)
{
    if (!scene_range(s,initial,64) || !scene_range(s,track,12) || !isfinite(frame) || frame<0) return;
    for(int j=0;j<3;j++){c->pos[j]=sf(s,initial+4*j);c->forward[j]=sf(s,initial+12+4*j);}
    c->roll=(int32_t)scene_word(s,initial+24);c->fov=(int32_t)scene_word(s,initial+28);
    c->near_clip=-sf(s,initial+32);c->far_clip=-sf(s,initial+36);
    uint32_t md=scene_word(s,track),type=scene_word(s,track+8)&0xffff;
    if(!scene_range(s,md,32))return;
    int k=(int)frame;float f=frame-k,g=1.f-f;
    float vectors[2][3];int valid[2]={0,0};
    for(int ch=0;ch<4;ch++) {
        uint32_t p=scene_word(s,md+4*ch),n=scene_word(s,md+16+4*ch),stride=ch<2 ? 16:8;
        unsigned mask=ch==0 ? 1:ch==1 ? 0x40:ch==2 ? 0x80:0x100;
        if(!(type&mask) || !p || frame>=n || !scene_range(s,p+k*stride,stride*2))continue;
        if(ch<2) {
            for(int j=0;j<3;j++)vectors[ch][j]=sf(s,p+k*16+4+j*4)*g+sf(s,p+k*16+20+j*4)*f;
            valid[ch]=1;
        } else {
            int32_t v=(int32_t)((float)(int32_t)scene_word(s,p+k*8+4)*g+(float)(int32_t)scene_word(s,p+k*8+12)*f);
            if(ch==2)c->roll=v;else c->fov=v;
        }
    }
    if(valid[0])memcpy(c->pos,vectors[0],12);
    if(valid[0]&&valid[1]) {
        float n=0;for(int j=0;j<3;j++){c->forward[j]=vectors[1][j]-vectors[0][j];n+=c->forward[j]*c->forward[j];}
        if(n>0){float inv=1.f/sqrtf(n);for(int j=0;j<3;j++)c->forward[j]*=inv;}
    }
}
static void init_actor(SceneActor *a,int type)
{
    memset(a,0,sizeof *a);a->active=1;a->type=type;a->scale[0]=a->scale[1]=a->scale[2]=1;
    a->motion=scene_clip(112);a->old_motion=-1;a->next_mode=-1;a->blend=15.f;
}
static void skater_motion(SceneActor *a,int ref,int mode,float blend)
{
    /* 0cdfbc: a second transition during a blend takes OLD frame zero. */
    a->old_motion=a->motion;a->old_raw_motion=a->raw_motion;a->old_frame=a->mode==3 ? 0.f:a->frame;
    a->motion=scene_clip(ref);a->raw_motion=0;a->frame=0;a->blend=blend;
    a->mode=3;a->next_mode=mode;
}
static void raw_skater_motion(SceneActor *a,uint32_t motion,int variant,int mode,float blend)
{
    a->old_motion=a->motion;a->old_raw_motion=a->raw_motion;a->old_frame=a->frame;
    a->raw_motion=motion;a->frame=0;a->blend=blend;a->variant=variant;
    a->mode=3;a->next_mode=mode;
}
static int command(Scene *s,const uint32_t *w)
{
    int code=w[0],a=(int32_t)w[1],b=(int32_t)w[2];
    SceneActor *actor=(unsigned)a<30 ? &s->actors[a]:0;
    SceneObject *object=(unsigned)a<30 ? &s->objects[a]:0;
    switch(code) {
    case 3: memset(&s->camera,0,sizeof s->camera);s->camera.active=1;break;
    case 7:case 8:
        if(!scene_range(s,w[1],64)||!scene_range(s,w[2],12))return 0;
        s->camera.initial=w[1];s->camera.track=w[2];s->camera.frame=0;s->camera.mode=code==8 ? 3:2;s->camera.reverse=0;break;
    case 9:for(int j=0;j<3;j++)s->camera.offset[j]=(int32_t)w[j+1];break;
    case 10:
        if(!s->sin_bam)return 0;
        for(int j=0;j<3;j++)s->camera.angle[j]=(int32_t)(((float)(int32_t)w[j+1]*65536.f)/360.f);break;
    case 0xb:s->handoff|=2;break;
    case 0xc:s->letterbox=0;s->letterbox_angle=0;s->letterbox_step=0;break;
    case 0xf:s->letterbox_step=16384;break; /* 026b4c(1/30): open in one tick */
    case 0x11:s->letterbox_step=a>0 ? -(int)(16384.f/a) : -16384;break;
    case 0x13:s->letterbox=0;s->letterbox_angle=0;s->letterbox_step=0;break;
    case 0x14:break;
    case 0x15:
        if((int)w[4]==s->language){
            s->text=w[3];s->text_x=a;s->text_y=b;s->dialog_cursor=s->dialog_wait=0;s->dialog_mode=2;
            memset(s->dialog,0,sizeof s->dialog);
            for(unsigned j=0;j<sizeof s->dialog-1;j++){const char *p=scene_bytes(s,w[3]+j,1);if(!p||!*p)break;s->dialog[j]=*p;}
        }break;
    case 0x16:case 0x17:s->text=0;s->dialog_mode=0;s->dialog_cursor=0;break;
    case 0x18:case 0x19:case 0x1a:case 0x1b:case 0x1c:case 0x1d:case 0x1e:
        if(s->language==(code<=0x19 ? 0:code-0x19)) {
            /* 0e4008 creates a child; it does not replace the previous
               caption. EVENT 12's finite caption overlaps the next one. */
            SceneCaption *c=&s->caption;
            if(c->mode){unsigned i=0;while(i<7 && s->older_captions[i].mode)i++;if(i==7)return 0;s->older_captions[i]=*c;}
            memset(c,0,sizeof *c);
            c->mode=2;c->x=a;c->y=b;c->infinite=(int32_t)w[4]<0;
            c->remaining=c->infinite ? 0.f:(float)(int32_t)w[4];
            for(unsigned j=0;j<sizeof c->text-1;j++){const char *p=scene_bytes(s,w[3]+j,1);if(!p)return 0;if(!*p)break;c->text[j]=*p;}
        }break;
    case 0x1f:s->caption.mode=0;break;
    case 0x20:break; /* preloaded by the host before scene_play */
    case 0x21: {
        if((unsigned)b>=30 || (unsigned)a>=26)return 0;
        init_actor(&s->actors[b],a);s->actors[b].motion=-1;
        SceneTypedSound *v=&s->typed_sound[b];unsigned serial=v->serial;
        memset(v,0,sizeof *v);v->serial=serial;v->character=-1;
        if(a>=18 && a<=20 && s->mission_context) {
            v->character=a==18?11:a==19?10:12;v->mouth_mode=2;
            if(!s->random15)return 0;
            v->mouth_frame=49.f*(s->random15(s->random_ctx)/32768.f);
        }
        break;
    }
    case 0x22:if(!actor)return 0;for(int j=0;j<3;j++)actor->pos[j]=(int32_t)w[j+2];break;
    case 0x24:if(!actor||!scene_range(s,w[2],12))return 0;actor->track=w[2];actor->track_frame=0;break;
    case 0x23:case 0x25:case 0x26:case 0x27:
        if(!actor || !actor->active || !scene_range(s,w[2],8))return 0;
        actor->raw_motion=w[2];actor->frame=0;actor->mode=code==0x26?3:code==0x27?4:2;break;
    case 0x28:if(!actor||!scene_range(s,w[2],8))return 0;actor->raw_motion=w[2];actor->frame=(int32_t)w[3];actor->mode=5;break;
    case 0x29:case 0x2a: {
        if(!actor || !actor->active)return 0;
        SceneTypedSound *v=&s->typed_sound[a];v->active=1;v->kind=code;
        v->bank=b;v->reference=code==0x29?b:(int32_t)w[3];
        int duration=(int32_t)w[code==0x29?3:4];v->remaining=duration>0?duration:0x7fffffff;v->serial++;break;
    }
    case 0x2b:
        if(!actor || !actor->active || (unsigned)b>=3)return 0;
        if(s->typed_sound[a].character>=0){s->typed_sound[a].mouth_mode=b;s->typed_sound[a].mouth_frame=b==1?130.f:0.f;}break;
    case 0x2c:if(!actor)return 0;actor->active=0;s->typed_sound[a].active=0;break;
    case 0x2d:
        if(!object || !scene_range(s,w[2],52) || (w[3]&&!scene_range(s,w[3],8)))return 0;
        memset(object,0,sizeof *object);init_actor(&object->transform,-2);
        object->root=w[2];object->texlist=w[3];object->flags=w[4];object->voice=object->voice_ref=-1;
        object->transform.motion=-1;object->transform.blend=0;break;
    case 0x2e:
        if(!object || !object->transform.active)return 0;
        for(int j=0;j<3;j++)object->transform.pos[j]=(int32_t)w[j+2];break;
    case 0x2f:
        if(!object || !object->transform.active || !scene_range(s,w[2],12))return 0;
        object->transform.track=w[2];object->transform.track_frame=(int32_t)w[3];break;
    case 0x30:case 0x31:case 0x33:case 0x34:
        if(!object || !object->transform.active || !scene_range(s,w[2],8))return 0;
        object->njs_motion=0;object->flags&=~0x20u;
        object->transform.raw_motion=w[2];object->transform.frame=code==0x34 ? (int32_t)w[3]:0;
        object->transform.mode=code==0x30 ? 2:code==0x31 ? 3:code==0x33 ? 4:5;break;
    case 0x32:
        if(!object || !object->transform.active || !scene_range(s,w[2],12))return 0;
        object->njs_motion=w[2];object->flags|=0x20;object->transform.frame=0;object->transform.mode=3;break;
    case 0x35:
        if(!object || !object->transform.active)return 0;
        object->voice_kind=code;object->voice_bank=w[2];object->voice_ref=object->voice=(int32_t)w[3];
        object->voice_remaining=(int32_t)w[4]>0?(int32_t)w[4]:0x7fffffff;object->voice_serial++;break;
    case 0x36:
        if(!object || !object->transform.active || (unsigned)b>=8)return 0;
        /* 1ce468 -> 0ccd34 -> 096294/0e6354 -> 0e7558. Keep the command
           reference separate from the resulting audio task's program. */
        {
#if JGR_PORTABLE_ASSETS
#include "source_data_scene_command_program.inc"
#else
        
#include "generated/source_data/scene_command_program.inc"

#endif
        object->voice_kind=code;object->voice_bank=b;object->voice_ref=b;object->voice=program[b];
        object->voice_remaining=(int32_t)w[3]>0 ? (int32_t)w[3]:0x7fffffff;object->voice_serial++;}break;
    case 0x38:
        if(!object)return 0;
        object->transform.active=0;object->voice=-1;break;
    case 0x39:init_actor(&s->player,-1);s->player_mouth_mode=2;s->player_mouth_frame=0;break;
    case 0x3a:
        if(!s->player.active)return 0;
        s->player.track=0;s->player.track_frame=0;s->player.frame=0;
        for(int j=0;j<3;j++)s->player.pos[j]=(int32_t)w[j+1];
        s->player.old_motion=s->player.motion;s->player.old_frame=0;
        s->player.old_raw_motion=s->player.raw_motion;s->player.raw_motion=0;s->player.variant=0;
        s->player.mode=1;s->player.motion=scene_clip(112);break; /* 0cd49c */
    case 0x3b:if(!scene_range(s,w[1],12))return 0;s->player.track=w[1];s->player.track_frame=0;break;
    case 0x3c:case 0x3e:if(scene_clip(a)<0)return 0;skater_motion(&s->player,a,code==0x3c ? 2:1,(float)b);break;
    case 0x3d:case 0x3f:
        if(!s->player.active || !scene_range(s,w[1],8))return 0;
        raw_skater_motion(&s->player,w[1],b!=0,code==0x3d?2:1,(int32_t)w[3]);break;
    case 0x40:s->handoff|=1;break;
    case 0x41:s->player.active=0;s->voice_ref=-1;break;
    case 0x43:
        if((unsigned)a>=31)return 0;
        if(s->player.active){s->voice_ref=a;s->voice_remaining=b>0 ? b:0x7fffffff;s->voice_serial++;}
        break; /* 0cd8c0: replace the actor's previous voice, not overlap it */
    case 0x44:
        if(!s->player.active || (unsigned)a>=3)return 0;
        /* 0cd944 -> 0ce3e8, independent from the body CNK. */
        s->player_mouth_mode=a;s->player_mouth_frame=a==1 ? 130.f:0.f;break;
    case 0x45: {
        SceneRecruit *r=&s->recruit;memset(r,0,sizeof *r);
        r->character=s->recruit_mode>=1 && s->recruit_mode<=13 ? s->recruit_mode-1:s->recruit_mode==14 ? 15:s->character;
        init_actor(&r->actor,r->character);r->forward=1;r->companion_mode=2;r->voice_ref=-1;break;
    }
    case 0x47:case 0x48:
        if(!s->recruit.actor.active || !scene_range(s,w[1],12))return 0;
        s->recruit.actor.track=w[1];s->recruit.actor.track_frame=0;s->recruit.pingpong=code==0x48;break;
    case 0x49:case 0x4b:
        if(!s->recruit.actor.active || scene_clip(a)<0)return 0;
        skater_motion(&s->recruit.actor,a,code==0x49 ? 2:1,(float)b);break;
    case 0x4a:case 0x4c:
        if(!s->recruit.actor.active || !scene_range(s,w[1],8))return 0;
        raw_skater_motion(&s->recruit.actor,w[1],b==0,code==0x4a?2:1,(int32_t)w[3]);break;
    case 0x4d:
        if((unsigned)a>=31)return 0;
        if(s->recruit.actor.active){s->recruit.voice_ref=a;s->recruit.voice_remaining=b>0 ? b:0x7fffffff;s->recruit.voice_serial++;}break;
    case 0x4e:
        if(!s->recruit.actor.active || (unsigned)a>=3)return 0;
        s->recruit.companion_mode=a;s->recruit.companion_frame=a==1 ? 130.f:0.f;break;
    case 0x4f:s->recruit.actor.active=0;s->recruit.voice_ref=-1;break;
    case 0x51:case 0x52:break; /* original event-manager flags, not music */
    case 0x53:break; /* original 0cf460 is empty */
    case 0x55:if(a!=0)return 0;break; /* 0cf488: retire the current radio task; host callback owns it */
    case 0x56:s->texture_loader=w[1];break; /* 0cf4ac: scene texture-loader table */
    case 0x57:break; /* source 0cf4d8 is empty */
    case 0x59:case 0x5a:break;
    case 0x5b:case 0x5d:case 0x5e:case 0x5f: {
        unsigned i=0;while(i<32 && s->aux[i].active)i++;if(i==32)return 0;
        struct SceneAuxEffect *e=s->aux+i;memset(e,0,sizeof *e);
        e->active=1;e->kind=code;e->serial=++s->aux_serial;e->slot=-1;
        if(code==0x5d) {
            if(!scene_range(s,w[1],12) || !scene_range(s,w[2],12))return 0;
            e->track[0]=w[1];e->track[1]=w[2];e->sound_serial=1;
        } else if(code==0x5f) {
            if((unsigned)a>=30)return 0;
            e->slot=a;e->remaining=b;
        } else {
            for(unsigned j=0;j<3;j++)e->pos[0][j]=(int32_t)w[j+1];
            e->remaining=(int32_t)w[4];
        }
        if(code==0x5e || code==0x5f){if(!s->random15)return 0;e->age=(s->random15(s->random_ctx)/32768.f)*30.f;}
        break;
    }
    case 0x5c: {
        if((unsigned)a>2 || !s->random15)return 0;
        unsigned slot=0;while(slot<16 && s->effects[slot].active)slot++;
        if(slot==16)return 0;
        float pos[3];for(int j=0;j<3;j++)pos[j]=(int32_t)w[j+2];
        if(!scene_effect_init(s->effects+slot,a,pos,s->random15(s->random_ctx)))return 0;
        s->effects[slot].serial=++s->effect_serial;break;
    }
    case 0x60:
        for(int j=0;j<4;j++){int shift=24-j*8;s->clamp_max[j]=((w[1]>>shift)&255)/255.f;s->clamp_min[j]=((w[2]>>shift)&255)/255.f;}
        s->colour_phase=2;break;
    case 0x61:
        if(a<=0)return 0;
        if(s->colour_phase){for(int j=0;j<4;j++){s->clamp_max_step[j]=(1.f-s->clamp_max[j])/a;s->clamp_min_step[j]=s->clamp_min[j]/a;}
            s->overlay_step=(s->overlay_argb>>24)/(float)a;s->colour_phase=3;}
        break;
    case 0x62:s->colour_overlay=1;s->overlay_argb=w[1];break;
    default:return 0; /* never silently report an unimplemented command done */
    }
    if(s->command)s->command(s->ctx,s,w);
    return 1;
}
static int commands(Scene *s)
{
    for(int i=0;i<4096;i++,s->pc+=20) {
        uint32_t w[5];if(!scene_read(s,s->pc,w,sizeof w))return -1;
        if(w[0]==1||w[0]==2||w[0]==100)return w[0];
        if(w[0]==99){if((int)w[1]==s->language)return 100;continue;}
        if(!command(s,w))return -1;
    }
    return -1;
}
static int start(Scene *s,const void *image,unsigned char *const *pages,size_t n,uint32_t base,int id,uint32_t flags,int character)
{
    memset(s,0,sizeof *s);s->image=image;s->pages=pages;s->length=n;s->base=base;s->id=id;s->flags=flags;s->character=character;s->voice_ref=-1;
    if(n<12 || n>0x1000000 || base>UINT32_MAX-(n-1) || (unsigned)character>=16)return 0;
    for(int i=0;i<3;i++){s->scripts[i]=scene_word(s,base+i*4);if(!scene_range(s,s->scripts[i],20))return 0;}
    s->pc=s->scripts[0];s->phase=0xb;return 1;
}
int scene_start(Scene *s,const void *image,size_t n,uint32_t base,int id,uint32_t flags,int character)
{ return start(s,image,NULL,n,base,id,flags,character); }
int scene_start_pages(Scene *s,unsigned char *const *pages,size_t n,uint32_t base,int id,uint32_t flags,int character)
{ return start(s,NULL,pages,n,base,id,flags,character); }
void scene_play(Scene *s)
{ if(s->phase==0xf){s->pc=s->scripts[1];s->phase=0x12;s->clock=1.f;} }
int scene_voice_usage(const Scene *s,uint32_t *primary,uint32_t *recruit)
{
    *primary=*recruit=0;
    for(unsigned phase=0;phase<3;phase++) {
        uint32_t pc=s->scripts[phase];int ended=0;
        for(size_t n=0;n<s->length/20;n++,pc+=20) {
            if(!scene_range(s,pc,20))return 0;
            unsigned op=scene_word(s,pc);
            if(op==100){ended=1;break;}
            if(op==0x43 || op==0x4d) {
                unsigned ref=scene_word(s,pc+4);if(ref>=31)return 0;
                *(op==0x43?primary:recruit)|=1u<<ref;
            }
        }
        if(!ended)return 0;
    }
    return 1;
}
int scene_busy(const Scene *s){return s->phase>=0xb && s->phase<0x14 && !s->error;}
static void dialog_tick(Scene *s)
{
    /* 02ce7c/02cfe4: $w removes itself and pauses fifteen ticks; $f changes
       page, $c/$m/$n do not consume a character tick. English advances each
       tick. Japanese's global modulo-three cadence is not used by this port. */
    if(s->dialog_mode==4){if(++s->dialog_wait>=15){s->dialog_wait=0;s->dialog_mode=2;}return;}
    if(s->dialog_mode!=2)return;
    for(int guard=0;guard<512;guard++) {
        int k=s->dialog_cursor;unsigned char ch=s->dialog[k];
        if(!ch){s->dialog_mode=3;return;}
        if(ch!='$'){s->dialog_cursor+=ch>=128 ? 2:1;return;}
        char code=s->dialog[k+1];
        if(code=='w'){memmove(s->dialog+k,s->dialog+k+2,strlen(s->dialog+k+2)+1);s->dialog_wait=0;s->dialog_mode=4;return;}
        if(code=='f'){memmove(s->dialog,s->dialog+k+2,strlen(s->dialog+k+2)+1);s->dialog_cursor=0;return;}
        if(code=='c'){s->dialog_cursor+=5;continue;}
        if(code=='m'||code=='n'){s->dialog_cursor+=2;continue;}
        if(code=='q'){s->dialog_mode=4;return;}
        return;
    }
}
static unsigned text_glyphs(const char *text,unsigned length,int start_x,int start_y,SceneGlyph *glyph,unsigned capacity)
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_scene_c__colours_0.inc"
#else

#include "generated/source_data/scene_c__colours_0.inc"

#endif
 /* 0c1aac */
    if(!glyph)return 0;
    int x=start_x,y=start_y;unsigned n=0;uint32_t colour=0xffffffffu;
    for(unsigned j=0;j<length && text[j];) {
        unsigned bytes,c=font_source_code((const unsigned char *)text+j,length-j,&bytes);
        if(!bytes)break;
        if(c=='$' && j+1<length) {
            int code=text[++j];
            if(code=='c' && j+3<length) {
                unsigned index=(unsigned char)text[j+2]-'0';
                colour=index<7 ? colours[index]:0xffffffffu;j+=3;
            } else if(code=='m' || code=='n'){x=start_x;y+=code=='m' ? 32:24;}
            j++;
            continue;
        }
        if(n==capacity)break;
        glyph[n].x=x;glyph[n].y=y;glyph[n].argb=colour;glyph[n].character=c;n++;x+=bytes==2 ? 24:12;j+=bytes;
    }
    return n;
}
unsigned scene_dialog_glyphs(const Scene *s,SceneGlyph *glyph,unsigned capacity)
{
    if(!s || !s->text || s->dialog_cursor<=0)return 0;
    unsigned n=(unsigned)s->dialog_cursor;if(n>511)n=511;
    return text_glyphs(s->dialog,n,s->text_x,s->text_y,glyph,capacity);
}
unsigned scene_caption_glyphs(const Scene *s,SceneGlyph *glyph,unsigned capacity)
{
    if(!s || !glyph)return 0;unsigned n=0;
    for(unsigned i=0;i<8;i++) {
        const SceneCaption *c=i<7 ? s->older_captions+i:&s->caption;
        if(c->mode==2)n+=text_glyphs(c->text,255,c->x,c->y,glyph+n,capacity-n);
    }
    return n;
}
static void mouth_tick(int character,int *mode,float *frame)
{
    *frame+=1.f;
    /* PLAY +68: Goji alone has three two-frame clips; the other source
       banks have 160/160/50. 0cdb64 and 0cebec return to mode 2. */
    if(*frame>=(character==13 ? 1.f:*mode==2 ? 49.f:159.f)){*mode=2;*frame=0;}
}
static void actor_tick(Scene *s,SceneActor *a,int player)
{
    if(!a->active)return;
    if(a->track){scene_sample_actor(s,a->track,a->track_frame,a);a->track_frame+=1.f;
        float last=(float)scene_word(s,a->track+4)-1.f;
        if(a->track_frame>last){a->track_frame=player?0:last;a->track=0;}}
    if(player) {
        mouth_tick(s->character,&s->player_mouth_mode,&s->player_mouth_frame);
        a->frame+=1.f;float last=(a->raw_motion ? scene_word(s,a->raw_motion)>>16:scene_frames(s->character,a->motion))-1.f;
        switch(a->mode){case 0:a->mode=1;break;
        case 1:if(a->frame>=last)a->frame=0;break;
        case 2:if(a->frame>=last)a->frame=last;break;
        case 3:if(a->frame>=a->blend){a->frame=0;a->mode=a->next_mode;}break;}
    } else {
        SceneTypedSound *v=&s->typed_sound[a-s->actors];
        if(v->active && --v->remaining<0)v->active=0;
        if(v->character>=0 && ++v->mouth_frame>(v->mouth_mode==2?49.f:159.f)){v->mouth_mode=2;v->mouth_frame=0;}
        if(a->mode==0)a->mode=1;
        else if(a->raw_motion && a->mode>=2 && a->mode<=5) {
            a->frame+=1.f;float last=(float)(scene_word(s,a->raw_motion)>>16)-1.f;
            if(a->frame>last) {
                a->frame=a->mode==3?last:0;
                if(a->mode==2 || a->mode==3)a->mode=1;
            }
        }
    }
}
static void recruit_tick(Scene *s)
{
    SceneRecruit *r=&s->recruit;SceneActor *a=&r->actor;if(!a->active)return;
    /* 0cebec: sample BEFORE advancing, reflecting at both authored track
       endpoints. Replacing a track does not reset the direction byte. */
    if(a->track) {
        scene_sample_actor(s,a->track,a->track_frame,a);
        float last=(float)scene_word(s,a->track+4)-1.f;
        if(!r->pingpong){a->track_frame+=1.f;if(a->track_frame>last){a->track_frame=0;a->track=0;}}
        else if(r->forward){a->track_frame+=1.f;if(a->track_frame>=last){a->track_frame=last;r->forward=0;}}
        else {a->track_frame-=1.f;if(a->track_frame<=0.f){a->track_frame=0;r->forward=1;}}
    }
    a->frame+=1.f;
    if(r->voice_ref>=0 && --r->voice_remaining<0)r->voice_ref=-1;
    mouth_tick(r->character,&r->companion_mode,&r->companion_frame);
    float last=(a->raw_motion ? scene_word(s,a->raw_motion)>>16:scene_frames(r->character,a->motion))-1.f;
    switch(a->mode) {
    case 0:a->mode=1;break;
    case 1:if(a->frame>=last)a->frame=0;break;
    case 2:if(a->frame>=last){a->frame=0;skater_motion(a,112,1,a->blend);}break;
    case 3:if(a->frame>=a->blend){a->frame=0;a->mode=a->next_mode;}break;
    }
}
static void camera_tick(Scene *s)
{
    SceneCamera *c=&s->camera;if(!c->active)return;
    if(c->mode==0)c->mode=1;
    else if(c->mode==2 || c->mode==3) {
        scene_sample_camera(s,c->initial,c->track,c->frame,c);
        float last=(float)scene_word(s,c->track+4)-1.f;
        if(c->mode==2){c->frame+=1.f;if(c->frame>=last){c->frame=0;c->mode=1;}}
        else if(!c->reverse){c->frame+=1.f;if(c->frame>=last){c->frame=last;c->reverse=1;}}
        else {c->frame-=1.f;if(c->frame<=0){c->frame=0;c->reverse=0;}}
    }
    /* 0cf96c -> 27a06c/22f3ec: source FSCA and rounded FTRV stages;
       offsets apply to position, not direction. No libm replacement. */
    if(c->angle[0] || c->angle[1] || c->angle[2]) {
        if(!s->sin_bam){s->error=1;return;}
        float m[16];const float zero[3]={0,0,0},one[3]={1,1,1};
        source_trs_fsca(zero,c->angle,one,m,s->sin_bam);
        for(int vector=0;vector<2;vector++) {
            float *v=vector ? c->forward:c->pos,out[3];
            for(int j=0;j<3;j++){double x=(double)m[j*4]*v[0];x+=(double)m[j*4+1]*v[1];x+=(double)m[j*4+2]*v[2];x+=m[j*4+3];out[j]=(float)x;}
            memcpy(v,out,sizeof out);
        }
    }
    for(int j=0;j<3;j++)c->pos[j]+=c->offset[j];
}
static void object_tick(Scene *s,SceneObject *o)
{
    SceneActor *a=&o->transform;if(!a->active)return;
    /* 0ccec8: the independent world track and voice update BEFORE the
       skeleton clock. This table is not a gameplay-skater animation graph. */
    if(a->track){scene_sample_actor(s,a->track,a->track_frame,a);a->track_frame+=1.f;
        if(a->track_frame>(float)scene_word(s,a->track+4)-1.f){a->track_frame=0;a->track=0;}}
    if(o->voice>=0 && --o->voice_remaining<0)o->voice=-1;
    if(a->mode==0){a->mode=1;return;}
    if(a->mode<2 || a->mode>5)return;
    a->frame+=1.f;
    float last=(a->mode==3 && (o->flags&0x20)) ?
        (float)scene_word(s,o->njs_motion+4)-1.f:(float)(scene_word(s,a->raw_motion)>>16)-1.f;
    if(a->frame>=last){a->frame=0;if(a->mode==2 || a->mode==3){a->mode=1;o->flags&=~0x20u;}}
}
static void colour_tick(Scene *s)
{
    if(!s->colour_phase)return;
    if(s->colour_phase==4){s->colour_active=0;s->colour_phase=0;return;}
    s->colour_active=1;
    if(s->colour_phase==3) {
        int done=1;
        for(int j=0;j<4;j++) {
            s->clamp_max[j]+=s->clamp_max_step[j];if(s->clamp_max[j]>1.f)s->clamp_max[j]=1.f;
            s->clamp_min[j]-=s->clamp_min_step[j];if(s->clamp_min[j]<0.f)s->clamp_min[j]=0.f;
            if(s->clamp_max[j]!=1.f || s->clamp_min[j]!=0.f)done=0;
        }
        if(done)s->colour_phase=4;
        if(s->colour_overlay){float a=(s->overlay_argb>>24)-s->overlay_step;
            s->overlay_argb=(s->overlay_argb&0xffffffu)|((uint32_t)(a<0.f ? 0.f:a)<<24);}
    }
    s->colour_max=s->colour_min=0;
    for(int j=0;j<4;j++){int shift=24-j*8;s->colour_max|=(uint32_t)(s->clamp_max[j]*255.f)<<shift;s->colour_min|=(uint32_t)(s->clamp_min[j]*255.f)<<shift;}
}
static void aux_tick(Scene *s,struct SceneAuxEffect *e)
{
    if(!e->active)return;
    if(e->kind==0x5d) {
        int done=0;
        for(unsigned j=0;j<2;j++) {
            uint32_t md=scene_word(s,e->track[j]),keys=scene_word(s,md);
            unsigned frame=(unsigned)e->age,last=scene_word(s,e->track[j]+4)-1;
            if(frame>last)frame=last;
            if(frame<scene_word(s,md+12)) {
                if(!scene_read(s,keys+frame*16+4,e->pos[j],12)){s->error=1;return;}
            }
            done+=e->age>=last;
        }
        if(done==2)e->active=0;
    } else if(e->kind==0x5b) {
        if(e->mode==0)e->mode=1;
        else if(e->mode==1){e->mode=2;e->sound_serial=1;}
        else if(e->mode==2){e->frame++;if(e->age>=119)e->mode=3;}
        else e->active=0;
    } else {
        if(e->mode<2)e->mode++;
        else {
            if((int)e->age%30==0)e->mesh^=1;
            e->frame=(e->frame+1)&7;
            if(e->remaining>=0 && --e->remaining<0)e->active=0;
        }
    }
    e->age+=1.f;
}
void scene_tick(Scene *s)
{
    if(!scene_busy(s)||s->phase==0xf)return;
    if(s->phase==0x11){s->wait+=1.f;if(s->limit<=s->wait){s->pc+=20;s->phase=s->return_phase;}}
    else {
        int code=commands(s);
        if(code<0){s->error=1;return;}
        if(code==1||code==2){s->return_phase=s->phase;s->limit=(float)(int32_t)scene_word(s,s->pc+4)-(code==2?s->clock:0.f)-3.f;s->wait=0;s->phase=0x11;}
        else if(s->phase==0xb){s->phase=0xf;s->pc=s->scripts[1];}
        else if(s->phase==0x12){s->phase=0x13;s->pc=s->scripts[2];}
        else if(s->phase==0x13){s->phase=0x14;s->result_player=s->player;s->result_camera=s->camera;}
    }
    s->clock+=1.f;
    /* Parent interpreter precedes its child tasks. New motion frame 0 is
       sampled this tick; a zero-length blend transitions before first draw. */
    actor_tick(s,&s->player,1);for(int i=0;i<30;i++)actor_tick(s,&s->actors[i],0);
    recruit_tick(s);
    for(int i=0;i<30;i++)object_tick(s,&s->objects[i]);
    for(int i=0;i<16;i++)scene_effect_tick(s->effects+i);
    for(int i=0;i<32;i++)aux_tick(s,s->aux+i);
    /* Child update: decrement after commands, delete strictly below zero.
       A zero command duration means INT_MAX, not an immediate stop. */
    if(s->player.active && s->voice_ref>=0 && --s->voice_remaining<0)s->voice_ref=-1;
    camera_tick(s);
    for(unsigned i=0;i<8;i++) {
        SceneCaption *c=i<7 ? s->older_captions+i:&s->caption;
        if(c->mode==6)c->mode=0;
        else if(c->mode==2 && !c->infinite && --c->remaining<=0)c->mode=6;
    }
    dialog_tick(s);
    colour_tick(s);
    s->letterbox_angle+=s->letterbox_step;
    if(s->letterbox_angle>=16384){s->letterbox_angle=16384;s->letterbox_step=0;}
    if(s->letterbox_angle<=0){s->letterbox_angle=0;s->letterbox_step=0;}
    s->letterbox=sinf(s->letterbox_angle*(6.283185307f/65536.f));
}
