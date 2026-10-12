#include "movie.h"
#include <string.h>
#include <math.h>
#include "source_fmac.h"
#include "movie_route.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_movie_tables.inc"
#else
#include "generated/source_data/movie_tables.inc"
#endif
#define MOVIE_MAX_ACTORS 256
int movie_next(int id){return (unsigned)id<35 ? MOVIE_NEXT[id]:-1;}
unsigned movie_flags(int id){return (unsigned)id<35 ? MOVIE_FLAGS[id]:0;}
int movie_exit_route(int id,int skipped)
{
    int next=movie_next(id);
    if((movie_flags(id)&2) || (skipped && next>=0))return MOVIE_ROUTE_SAVE;
    return skipped ? MOVIE_ROUTE_RETURN:next;
}
int movie_voice_reference(int movie,int segment)
{
    return (unsigned)movie<35 && (unsigned)segment<MOVIE_SEGMENTS[movie] ?
        MOVIE_VOICES[movie][segment]:-1;
}
int movie_script_voice_reference(int movie,int cue)
{
    if((unsigned)movie>=35 || cue<0)return -1;
    for(unsigned i=0;i<sizeof MOVIE_AUDIO_CUES/sizeof *MOVIE_AUDIO_CUES;i++)
        if(MOVIE_AUDIO_CUES[i].movie==movie && MOVIE_AUDIO_CUES[i].voice>=0 && !cue--)
            return MOVIE_AUDIO_CUES[i].voice;
    return -1;
}
static void script_audio(Movie *m)
{
    /* 051130 calls 0de0e0 before incrementing the caption clock. Its
       trigger window is clock-1 < tick <= clock; opcode 13 replaces and
       starts narration directly, independently of segment voice tables. */
    for(unsigned i=0;i<sizeof MOVIE_AUDIO_CUES/sizeof *MOVIE_AUDIO_CUES;i++) {
        if(MOVIE_AUDIO_CUES[i].movie!=m->movie || MOVIE_AUDIO_CUES[i].segment!=m->segment)continue;
        int tick=MOVIE_AUDIO_CUES[i].tick;
        if(m->caption_frame-1.f<tick && m->caption_frame>=tick) {
            m->audio_request=MOVIE_AUDIO_CUES[i].voice;
            m->audio_start=m->audio_request>=0;m->audio_stop=m->audio_request<0;
        }
    }
}
static uint32_t word(const Movie *m,uint32_t p){return scene_word(&m->source,p);}
uint32_t movie_actor(const Movie *m,unsigned index)
{return index<(unsigned)m->model_count ? word(m,word(m,m->descriptor+8)+index*4):0;}
static int select_segment(Movie *m,int index)
{
    uint32_t list=word(m,0x8c800004);m->descriptor=word(m,list+index*4);
    if(!scene_range(&m->source,m->descriptor,12))return 0;
    m->model0=0;
    for(int i=0;i<index;i++) {
        uint32_t p=word(m,word(m,list+i*4)+8);unsigned j=0;
        while(j<MOVIE_MAX_ACTORS && scene_range(&m->source,p+j*4,4) && word(m,p+j*4))j++;
        if(j==MOVIE_MAX_ACTORS || !scene_range(&m->source,p+j*4,4))return 0;
        m->model0+=j;
    }
    uint32_t p=word(m,m->descriptor+8);m->model_count=0;
    while(m->model_count<MOVIE_MAX_ACTORS && scene_range(&m->source,p+m->model_count*4,4) && word(m,p+m->model_count*4)) {
        uint32_t a=word(m,p+m->model_count*4);
        if(!scene_range(&m->source,a,16))return 0;
        m->model_count++;
    }
    if(m->model_count==MOVIE_MAX_ACTORS || !scene_range(&m->source,p+m->model_count*4,4))return 0;
    m->segment=index;m->frame=m->caption_frame=0;m->next_audio=0;return 1;
}
static int bind_source(Movie *m,const void *image,unsigned char *const *pages,unsigned length,int id)
{
    memset(m,0,sizeof *m);m->audio_request=-1;
    if((unsigned)id>=35||(!image && !pages)||length<12)return 0;
    m->source.image=image;m->source.pages=pages;m->source.length=length;m->source.base=0x8c800000;
    m->movie=id;m->count=MOVIE_SEGMENTS[id];m->fade=1;
    uint32_t list=word(m,0x8c800004);
    if(!scene_range(&m->source,list,(m->count+1)*4)||word(m,list+m->count*4))return 0;
    /* Validate every authored list before playback. An out-of-bounds word
       reads as zero in Scene, so zero alone is not a valid terminator. */
    for(int i=0;i<m->count;i++)if(!select_segment(m,i))return 0;
    if(!select_segment(m,0))return 0;
    return 1;
}
int movie_bind(Movie *m,const void *image,unsigned length,int id)
{return bind_source(m,image,NULL,length,id);}
int movie_bind_pages(Movie *m,unsigned char *const *pages,unsigned length,int id)
{return bind_source(m,NULL,pages,length,id);}
float movie_segment_end(const Movie *m)
{
    uint32_t camera=word(m,m->descriptor),motion=word(m,camera+4);
    float end=motion ? (float)word(m,motion+4)-1:0;
    for(int i=0;i<m->model_count;i++) {
        motion=word(m,movie_actor(m,i)+4);
        if(motion)end=fmaxf(end,(float)word(m,motion+4)-1);
    }
    return end;
}
void movie_step(Movie *m,int ready)
{
    m->audio_request=-1;m->audio_start=0;m->audio_stop=0;m->music_request=-1;if(m->done||m->error)return;
    /* The source child ticks independently of parent loading barriers. */
    if(m->phase==14 || m->phase==15)m->frame+=1;
    if(m->phase>=14) {
        m->letterbox=fminf(1,m->letterbox+1.f/30.f);
        if(!m->fade_out)m->fade=fmaxf(0,m->fade-.05f);
    }
    switch(m->phase) {
    case 0:m->phase=7;break;
    case 7:m->phase=9;break;
    case 9:m->phase=movie_voice_reference(m->movie,m->segment)<0 ? 13:10;break;
    case 10:m->audio_request=movie_voice_reference(m->movie,m->segment);m->phase=11;break;
    case 11:if(ready)m->phase=12;break;
    case 12:m->audio_start=1;m->phase=13;break;
    case 13:m->frame=m->caption_frame=0;m->phase=14;
        /* 02e238 only selects F_RADIO for movie 0 and SYS_RADIO for 31.
           Other DEMOs retain their incoming stream, including ENDING. */
        m->music_request=m->movie==0 ? 2:m->movie==31 ? 188:-1;break;
    case 14:
        if(m->segment+1==m->count) {
            if(movie_segment_end(m)-m->frame<31)m->fade_out=1;
            if(m->frame>=movie_segment_end(m))m->phase=15;
        } else if(!m->next_audio && m->frame+30>=movie_segment_end(m)) {
            int voice=movie_voice_reference(m->movie,m->segment+1);
            if(voice<0){if(!select_segment(m,m->segment+1))m->error=1;}
            else {m->audio_request=voice;m->next_audio=1;}
        } else if(m->next_audio && ready) {
            if(!select_segment(m,m->segment+1))m->error=1;
            m->audio_start=1;
        }
        script_audio(m);m->caption_frame+=1;break;
    case 15:m->caption_frame+=1;if(m->fade_out && m->fade>=1)m->phase=17;break;
    case 17:m->done=1;break;
    default:m->error=1;break;
    }
    if(m->fade_out)m->fade=fminf(1,m->fade+1.f/30.f);
}
void movie_skip(Movie *m)
{
    if(m->phase!=14 || m->fade>0 || !(movie_flags(m->movie)&1))return;
    m->skipped=1;m->audio_request=-1;m->audio_start=0;m->audio_stop=1;m->fade_out=1;m->phase=15;
}
float movie_actor_frame(const Movie *m,uint32_t actor)
{
    uint32_t motion=word(m,actor+4),frames=word(m,motion+4);
    if(!motion || !frames)return 0;
    if(m->frame<(float)frames-1)return m->frame;
    /* 09d978: floor + signed remainder 013e94 + fractional part.
       0x12 is cel drawing WITH looping, not a new material type. */
    if(word(m,actor+12)&0x10)return fmodf(m->frame,(float)frames);
    return (float)frames-1;
}
static float number(const Movie *m,uint32_t address)
{uint32_t bits=word(m,address);float v;memcpy(&v,&bits,4);return v;}
void movie_camera(const Movie *m,SceneCamera *c)
{
    memset(c,0,sizeof *c);uint32_t cam=word(m,m->descriptor),motion=word(m,cam+4),initial=word(m,cam);
    if(!scene_range(&m->source,initial,64) || !scene_range(&m->source,motion,12))return;
    unsigned frames=word(m,motion+4),md=word(m,motion),kind=word(m,motion+8);
    if(!frames || kind!=0x401c1 || !scene_range(&m->source,md,32) || !isfinite(m->frame) || m->frame<0)return;
    float frame=fminf(m->frame,(float)frames-1),target[3];int has_target=0;
    for(int j=0;j<3;j++){c->pos[j]=number(m,initial+4*j);c->forward[j]=number(m,initial+12+4*j);}
    c->roll=(int32_t)word(m,initial+24);c->fov=(int32_t)word(m,initial+28);
    c->near_clip=-number(m,initial+32);c->far_clip=-number(m,initial+36);
    /* DEMO 09dc4c calls Ninja 22fc6c, NOT EVENT's dense 0cfdd4 sampler.
       22f79c/22f916/22f9a0/22fa26 search sparse time keys; interpolators
       22f5a0 / 22f504 use weighted FMAC, including signed scalar angles. */
    for(unsigned ch=0;ch<4;ch++) {
        uint32_t p=word(m,md+4*ch),n=word(m,md+16+4*ch),stride=ch<2 ? 16:8;
        if(!p)continue;
        if(!n || n>m->source.length/stride || !scene_range(&m->source,p,n*stride))return;
        unsigned lo=0,hi=n;
        while(hi-lo>1){unsigned mid=(lo+hi)/2;if(word(m,p+mid*stride)<=(unsigned)frame)lo=mid;else hi=mid;}
        hi=(lo+1)%n;
        uint32_t a=p+lo*stride,b=p+hi*stride;int32_t delta=(int32_t)(word(m,b)-word(m,a));
        float t=(frame-(float)word(m,a))/(delta>0 ? (float)(uint32_t)delta:1.f);
        if(ch<2) {
            for(unsigned j=0;j<3;j++) {
                float v=source_fmac(1.f-t,number(m,a+4+4*j),number(m,b+4+4*j)*t);
                if(ch==0)c->pos[j]=v;else target[j]=v;
            }
            if(ch==1)has_target=1;
        } else {
            int32_t v=(int32_t)source_fmac(1.f-t,(float)(int32_t)word(m,a+4),(float)(int32_t)word(m,b+4)*t);
            if(ch==2)c->roll=v;else c->fov=v;
        }
    }
    if(has_target) {
        for(int j=0;j<3;j++)c->forward[j]=target[j]-c->pos[j];
        /* 278a88: Y*Y, FMAC X*X, FMAC Z*Z, then reciprocal square root.
           PSP sqrt approximation is not asserted SH4-bit-exact here. */
        float n=source_fmac(c->forward[0],c->forward[0],c->forward[1]*c->forward[1]);
        n=source_fmac(c->forward[2],c->forward[2],n);
        if(!(n>0) || !isfinite(n))return;
        float inv=1.f/sqrtf(n);for(int j=0;j<3;j++)c->forward[j]*=inv;
    }
    c->active=1;
}
void movie_caption(const Movie *m,char text[256],int *x,int *y)
{
    text[0]=0;*x=-1;*y=394;uint32_t table=word(m,0x8c800000),p=word(m,table);
    for(unsigned i=0;i<512 && scene_range(&m->source,p,28);i++,p+=28) {
        unsigned type=word(m,p);if(type==16)break;
        if(word(m,p+4)!=(unsigned)m->segment)continue;
        uint32_t ptr=0;int tick=word(m,p+8),hold=0;
        if(type==4 && word(m,p+20)==1){ptr=word(m,p+12);hold=word(m,p+16);*x=-1;}
        if(type==6){ptr=word(m,p+20);hold=word(m,p+24);*x=word(m,p+12);*y=word(m,p+16);}
        if(!ptr || m->caption_frame<=tick || m->caption_frame>tick+hold)continue;
        for(unsigned j=0;j<255;j++) {
            const char *c=scene_bytes(&m->source,ptr+j,1);if(!c){text[0]=0;return;}
            text[j]=*c;if(!*c)return;text[j+1]=0;
        }
        return;
    }
}
