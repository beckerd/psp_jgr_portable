/* Render-thread-owned keys; worker-owned descriptors/results are isolated
   on complete cache lines. Input asset banks live until the same-frame join. */
#ifdef __PSP__
#include <stdint.h>
#include "me.h"
#endif
#define POSE_REQUESTS 24
#define POSE_BONES 256
#define POSE_CHUNK_BONES 4

/* City tag preparation reuses stack-local bank/character descriptors for
   different meshes. Include the referenced asset storage, not just the
   descriptor addresses, so a cached cursor cannot retain another mesh. */
typedef struct {
    const Chars *bank;const JgcChar *ch;
    const JgcBone *bones;const JgcMotion *motion;
    float frame;
} PoseKey;
static PoseKey pose_key(const Chars *c,const JgcChar *ch,int mi,float frame)
{return (PoseKey){c,ch,c->bones+ch->bone0,mi<0 ? NULL:c->motions+mi,frame};}
typedef struct {CharSampleCursor cursor;unsigned first,count,local;} PoseRequest;
typedef struct {
    PoseRequest requests[POSE_REQUESTS];
    CharPose poses[POSE_BONES];
    float local[POSE_BONES][16] __attribute__((aligned(16)));
    unsigned count,bones,request,bone;
} __attribute__((aligned(64))) PoseJob;
static PoseJob pose_job;
static PoseKey pose_keys[POSE_REQUESTS],cursor_keys[4];
static CharSampleCursor pose_cursors[4];
static unsigned pose_count,cursor_count,cursor_next;
static int pose_ready,pose_async,pose_last=-1;
static CharPoseProfile pose_profile;
#ifdef __PSP__
static int pose_verify_status;
#endif

static int pose_key_equal(const PoseKey *a,const PoseKey *b)
{return a->bank==b->bank && a->ch==b->ch && a->bones==b->bones && a->motion==b->motion && a->frame==b->frame;}
/* Bounded, pure scalar-FPU work. Audio is checked after at most four bones,
   including local TRS construction. World hierarchy composition stays VFPU. */
static int pose_job_step(PoseJob *job)
{
    for(unsigned step=0;step<POSE_CHUNK_BONES && job->request<job->count;step++) {
        PoseRequest *r=job->requests+job->request;unsigned at=r->first+job->bone;
        CharPose *p=job->poses+at;
        chars_sample_prepared(&r->cursor,job->bone,p);
        if(r->local)chars_local_matrix(job->local[at],p->ev,p->pos,p->ang,p->scl);
        if(++job->bone==r->count){job->request++;job->bone=0;}
    }
    return job->request==job->count;
}
#ifdef __PSP__
static int pose_job_worker(int arg){return pose_job_step((PoseJob *)(uintptr_t)(unsigned)arg);}
#endif
void chars_pose_join(void)
{
    if(pose_ready || !pose_count)return;
#ifdef __PSP__
    if(pose_async) {
        unsigned chunks=0,ticks=0,maximum=0,begin=JGR_PROFILE_CLOCK();int complete;
        /* A failed physical fence must never release borrowed asset memory.
           The kernel watcher continues servicing HOME/sleep while waiting. */
        while((complete=me_background_wait(&chunks,&ticks,&maximum))<0)sceKernelDelayThread(1000);
        pose_profile.wait_us+=JGR_PROFILE_CLOCK()-begin;
        sceKernelDcacheInvalidateRange(&pose_job,sizeof pose_job);
        pose_profile.chunks+=chunks;pose_profile.me_ticks+=ticks;
        if(maximum>pose_profile.max_chunk_ticks)pose_profile.max_chunk_ticks=maximum;
        if(complete && !pose_verify_status) {
            /* First real-device batch certifies shared-cache publication,
               sampling and local TRS against this CPU's same input state. */
            pose_verify_status=1;
            for(unsigned r=0;r<pose_job.count;r++) {
                const PoseRequest *request=pose_job.requests+r;
                for(unsigned b=0;b<request->count;b++) {
                    CharPose expected;float matrix[16];unsigned at=request->first+b;
                    chars_sample_prepared(&request->cursor,b,&expected);
                    if(memcmp(&expected,pose_job.poses+at,sizeof expected))pose_verify_status=-1;
                    if(request->local) {
                        chars_local_matrix(matrix,expected.ev,expected.pos,expected.ang,expected.scl);
                        if(memcmp(matrix,pose_job.local[at],sizeof matrix))pose_verify_status=-1;
                    }
                }
            }
        }
        if(complete && pose_verify_status>0){pose_profile.me_bones+=pose_job.bones;pose_ready=1;pose_async=0;return;}
        /* Only reached after a physical reset (sleep/exit/failure). Restart
           the immutable batch, never consume a partially written pose. */
        pose_profile.cancelled++;if(!complete)pose_verify_status=0;pose_job.request=pose_job.bone=0;pose_async=0;
    }
#endif
    while(!pose_job_step(&pose_job)){}
    pose_profile.cpu_bones+=pose_job.bones;pose_ready=1;
}
void chars_pose_forget(void)
{
    chars_pose_join();pose_count=cursor_count=cursor_next=0;pose_last=-1;
    pose_job.count=pose_job.bones=pose_job.request=pose_job.bone=0;pose_ready=pose_async=0;
}
void chars_pose_frame_begin(void){chars_pose_forget();}
static void pose_prefetch(const Chars *c,int ci,int motion,float frame,int local)
{
    if(!c || !c->h || (unsigned)ci>=c->h->n_chars || pose_ready || pose_async)return;
    const JgcChar *ch=c->chars+ci;int nm=(int)ch->nmotions;
    int mi=nm>0 ? ch->motion0+((motion%nm)+nm)%nm:-1;
    PoseKey key=pose_key(c,ch,mi,frame);
    for(unsigned i=0;i<pose_count;i++)if(pose_key_equal(&pose_keys[i],&key)){pose_job.requests[i].local|=local;return;}
    if(!ch->nbones || pose_count==POSE_REQUESTS || ch->nbones>POSE_BONES-pose_job.bones)return;
    PoseRequest *r=pose_job.requests+pose_count;
    chars_sample_prepare(c,ch,mi,frame,&r->cursor);r->first=pose_job.bones;r->count=ch->nbones;r->local=local;
    pose_keys[pose_count++]=key;pose_job.count=pose_count;pose_job.bones+=ch->nbones;
    pose_profile.queued_bones+=ch->nbones;
}
void chars_pose_prefetch(const Chars *c,int ci,int motion,float frame){pose_prefetch(c,ci,motion,frame,1);}
void chars_pose_prefetch_channels(const Chars *c,int ci,int motion,float frame){pose_prefetch(c,ci,motion,frame,0);}
void chars_pose_submit(void)
{
    if(!pose_count || pose_ready || pose_async)return;
#ifdef __PSP__
    /* Outputs must not retain a dirty main-CPU cache line while ME owns
       them. Later audio writeback-all calls cannot overwrite ME results. */
    sceKernelDcacheWritebackInvalidateRange(&pose_job,sizeof pose_job);
    if(pose_verify_status>=0)pose_async=me_background_begin(pose_job_worker,(int)(uintptr_t)&pose_job);
#endif
    pose_profile.batches++;
    /* Emulators and a deliberately disabled ME use exactly the same kernel
       when an actor first consumes its pose. No extra preparatory work. */
}
static int pose_cache_find(const PoseKey *key)
{
    if(pose_last>=0 && pose_key_equal(pose_keys+pose_last,key))return pose_last;
    for(unsigned i=0;i<pose_count;i++)if(pose_key_equal(pose_keys+i,key))return pose_last=(int)i;
    return -1;
}
const float *chars_pose_local(const Chars *c,const JgcChar *ch,int mi,float frame,unsigned bone)
{
    PoseKey key=pose_key(c,ch,mi,frame);int i=pose_cache_find(&key);
    if(i<0 || bone>=ch->nbones)return NULL;
    chars_pose_join();return pose_job.requests[i].local ? pose_job.local[pose_job.requests[i].first+bone]:NULL;
}
const float *chars_pose_locals(const Chars *c,const JgcChar *ch,int mi,float frame)
{
    PoseKey key=pose_key(c,ch,mi,frame);int i=pose_cache_find(&key);
    if(i<0)return NULL;
    /* Request descriptors share the worker-owned cache lines. Join before
       reading them, just as the per-bone accessor does. */
    chars_pose_join();const PoseRequest *r=pose_job.requests+i;
    if(!r->local || r->count!=ch->nbones)return NULL;
    pose_profile.reused_bones+=r->count;
    return pose_job.local[r->first];
}
void chars_sample_pose(const Chars *c,const JgcChar *ch,int mi,float frame,unsigned bone,CharPose *pose)
{
    PoseKey key=pose_key(c,ch,mi,frame);int cached=pose_cache_find(&key);
    if(cached>=0 && bone<ch->nbones) {
        chars_pose_join();*pose=pose_job.poses[pose_job.requests[cached].first+bone];
        pose_profile.reused_bones++;return;
    }
    for(unsigned i=0;i<cursor_count;i++)if(pose_key_equal(cursor_keys+i,&key)) {
        chars_sample_prepared(pose_cursors+i,bone,pose);pose_profile.cursor_reused++;return;
    }
    unsigned slot=cursor_count<4 ? cursor_count++:(cursor_next++&3);
    cursor_keys[slot]=key;chars_sample_prepare(c,ch,mi,frame,pose_cursors+slot);
    chars_sample_prepared(pose_cursors+slot,bone,pose);
}
void chars_pose_profile(CharPoseProfile *out)
{
    *out=pose_profile;
#ifdef __PSP__
    out->verify_status=pose_verify_status;
#endif
    out->storage_bytes=sizeof pose_job+sizeof pose_keys+sizeof cursor_keys+sizeof pose_cursors;pose_profile=(CharPoseProfile){0};}
