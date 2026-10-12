#include "diagnostics.h"
#include "traffic_draw.h"
#include "traffic_bounds.h"
#include "charpose.h"
#include <string.h>
#ifdef __PSP__
#include <pspkernel.h>
#endif

static TrafficDrawProfile profile;
static int batch_enabled=1;
void traffic_draw_batch_mode(int enabled){batch_enabled=!!enabled;}
static unsigned draw_clock(void)
{
#if defined(__PSP__) && JGR_DIAGNOSTICS
    return sceKernelGetSystemTimeLow();
#else
    return 0;
#endif
}
void traffic_draw_profile(TrafficDrawProfile *out)
{*out=profile;memset(&profile,0,sizeof profile);}
void traffic_draw_totals(TrafficDrawProfile *out)
{*out=profile;}

void traffic_draw_world(Chars *models,PathWorld *w,const ObjectView *view,const float planes[6][4])
{
    if(!w || !models || !models->h)return;
    CHAR_WORK_BEGIN(work);
    unsigned started=draw_clock();profile.frames++;int batch_open=0;
    const float root[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    models->drawn_tris=0;
    /* Own this scope explicitly: traffic precedes the ordinary actor pass,
       whose frustum is cleared at the end of every frame. */
#ifdef JGR_TRAFFIC_DRAW_REFERENCE
    chars_world_frustum(NULL);
#else
    chars_world_frustum(planes);
#endif
    for(unsigned i=0;i<w->count;i++) {
        PathInstance *p=&w->slots[w->order[i]];unsigned lod=p->near_model?0:1,model=p->model[lod];
        if(!p->steps || model>=models->h->n_chars)continue;
        const JgcChar *ch=&models->chars[model];if(!ch->nbones || ch->nbones>64)continue;
        profile.candidates++;
#ifndef JGR_TRAFFIC_DRAW_REFERENCE
        int ordinary=traffic_view_visible(view,p->task.pos,0);
        if(!ordinary && !p->close_draw){profile.source_rejected++;continue;}
        if(!ordinary && p->close_draw)profile.close_kept++;
#endif
        /* Rendering uses the advanced frame and cel interpolation. The
           simulation's old-frame ordinary Ninja pose is not equivalent. */
#ifndef JGR_TRAFFIC_SAMPLE_REFERENCE
        SceneNjsCelSample sample;
        if(!scene_njs_cel_prepare(&sample,&w->source,p->motion,p->task.frame,ch->nbones))continue;
        profile.sample_prepares++;
#endif
        CharPose pose[64];int valid=1;
        for(unsigned j=0;j<ch->nbones;j++) {
            const JgcBone *bone=&models->bones[ch->bone0+j];pose[j]=(CharPose){.ev=bone->ev};
            memcpy(pose[j].pos,bone->pos,12);memcpy(pose[j].ang,bone->ang,12);memcpy(pose[j].scl,bone->scl,12);
#ifdef JGR_TRAFFIC_SAMPLE_REFERENCE
            if(!scene_sample_njs(&w->source,p->motion,p->task.frame,j,1,&pose[j])){valid=0;break;}
#else
            uint32_t *hint=j<PATHWORLD_RENDER_HINT_BONES ? p->render_key_hint[j]:NULL;
            if(!scene_njs_cel_sample(&sample,j,hint,&pose[j])){valid=0;break;}
#endif
            profile.posed_bones++;
            if(!j) {
                if(!(p->render_bounds_valid&(1u<<lod))) {
                    p->render_radius[lod]=traffic_motion_radius(models,model,&w->source,p->motion);
                    p->render_bounds_valid|=1u<<lod;
                    profile.bounds_prepares++;
                }
                float center[3]={0};if(!(pose[0].ev&1))memcpy(center,pose[0].pos,12);
#ifndef JGR_TRAFFIC_DRAW_REFERENCE
                if(!chars_sphere_visible(center,p->render_radius[lod])){profile.frustum_rejected++;valid=0;break;}
#endif
            }
        }
        if(valid) {
            if(batch_enabled && !batch_open){chars_pose_batch_begin(models);batch_open=1;profile.state_batches++;}
            chars_draw_one_pose(models,model,root,pose,-1,0,0);
            profile.drawn++;if(lod)profile.far_drawn++;else profile.near_drawn++;
        }
    }
    if(batch_open)chars_pose_batch_end();
    chars_world_frustum(NULL);
    profile.triangles+=models->drawn_tris;
    profile.us+=draw_clock()-started;
    CHAR_WORK_END(CHAR_WORK_TRAFFIC,work);
}
