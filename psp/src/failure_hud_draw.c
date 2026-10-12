#include "hud_layout.h"
#include "failure_hud_draw.h"
#include <pspgu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void matrix(int mode,const float v[16]);
void failure_models_free(FailureModels *h)
{chars_free(&h->models);free(h->image);memset(h,0,sizeof *h);}
static int pose_motion(const FailureModels *h,int model,int owner,int clip,float frame,CharPose poses[64])
{
    if(!h || !h->models.h || !h->image || (unsigned)model>=FAILURE_MODELS || (unsigned)owner>=FAILURE_MODELS || (unsigned)clip>=2)return -1;
    const JgcChar *c=h->models.chars+model;if(c->nbones>64)return -1;
    for(unsigned i=0;i<c->nbones;i++){
        chars_sample_pose(&h->models,c,-1,0,i,poses+i);
        if(frame>=0 && h->motion[owner][clip] && !scene_sample_njs(&h->source,h->motion[owner][clip],frame,i,0,poses+i))return -1;
    }
    return c->nbones;
}
int failure_models_pose(const FailureModels *h,int model,int clip,float frame,CharPose poses[64])
{return pose_motion(h,model,model,clip,frame,poses);}
int failure_models_load(FailureModels *h,const char *mesh,const char *motion)
{
    memset(h,0,sizeof *h);if(chars_load(&h->models,mesh))return -1;
    if(h->models.h->n_chars!=FAILURE_MODELS || h->models.h->n_motions)goto invalid;
    FILE *f=fopen(motion,"rb");if(!f)goto invalid;
    unsigned header[4],rec[FAILURE_MODELS][3];long size=-1;unsigned prefix=16+sizeof rec;
    if(!fseek(f,0,SEEK_END))size=ftell(f);rewind(f);
    int ok=size>=prefix && size<=0x80000 && fread(header,1,16,f)==16 &&
        !memcmp(header,"FHM1",4) && header[2]==(unsigned)size-prefix && header[3]==FAILURE_MODELS && header[1]<=0xffffffffu-header[2];
    if(ok)ok=fread(rec,1,sizeof rec,f)==sizeof rec;
    if(ok){h->image=malloc(header[2]);ok=h->image && fread(h->image,1,header[2],f)==header[2];}
    fclose(f);if(!ok)goto invalid;
    h->source.image=h->image;h->source.base=header[1];h->source.length=header[2];
    for(int i=0;i<FAILURE_MODELS;i++)for(int c=0;c<2;c++){
        h->motion[i][c]=rec[i][c+1];if(!h->motion[i][c])continue;
        unsigned n=scene_word(&h->source,h->motion[i][c]+4);if(!n || n>300)goto invalid;
        for(unsigned t=0;t<n;t++){CharPose poses[64];if(failure_models_pose(h,i,c,t,poses)<0)goto invalid;}
    }
    return 0;
invalid:failure_models_free(h);return -1;
}
/* 036b6c / 037aa4 / 037ca0: result heading and all six rank meshes.
   Every rank object takes the SELECTED rank's intro clip. After three
   loops, the selected one advances its second clip and the others retain
   the last intro pose under the original dim light. */
void result_hud_draw(FailureModels *h,const RecruitResult *s,int kind,int rank,int souls,const float projection[16],const float view[16])
{
    if(!h->models.h || !s->active || !result_hud_visible(s))return;
    if((unsigned)rank>=6)rank=0;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float near=.5f,far=1000;
    float hp[16]={500.f/320.f,0,0,0,0,500.f/240.f,0,0,0,0,(far+near)/(near-far),2.f*far*near/(near-far),0,0,-1,0};
    hud_projection(hp,HUD_CENTRE);
    matrix(GU_VIEW,identity);matrix(GU_PROJECTION,hp);sceGuClear(GU_DEPTH_BUFFER_BIT);sceGuDepthFunc(GU_GEQUAL);
    h->models.drawn_tris=0;
    if(result_hud_record_visible(s)){
        unsigned n=scene_word(&h->source,h->motion[15][0]+4);
        CharPose pose[64];if(failure_models_pose(h,15,0,s->record_clock<n ? s->record_clock:n-1,pose)>=0){
            /* 037f04 increments +98 before sampling; null-motion draws
               retain the final key after the one-shot has finished. */
            const float root[16]={.03f,0,0,0,0,.03f,0,-.05f,0,0,.03f,-1.5f,0,0,0,1};
            const CharLight light={{1,1,1},1.f/.03f,.5f/.03f,{0xffffffff,0xffffffff},1};
            chars_scene_lighting(&light,NULL);chars_draw_material(&h->models,15,root,pose,-1,0,1,NULL);
        }
    }
    unsigned length=scene_word(&h->source,h->motion[9+rank][0]+4),clock=s->draw_clock;
    const unsigned char *flags=RESULT_FLAGS[s->class_id];
    if(flags[0] && clock>=3*scene_word(&h->source,h->motion[9][0]+4)){
        int id=(kind==5 || kind==7 || kind==8) ? 8:s->class_id==3 ? 2:7;
        unsigned t=clock-3*scene_word(&h->source,h->motion[9][0]+4),n=scene_word(&h->source,h->motion[id][0]+4);
        CharPose pose[64];if(failure_models_pose(h,id,0,t<n ? t:n-1,pose)>=0){
            const float root[16]={.025f,0,0,0,0,.025f,0,(souls>0 && flags[1]) ? .08f:0,0,0,.025f,-1.5f,0,0,0,1};
            const CharLight light={{1,1,1},1.f/.025f,.5f/.025f,{0xffffffff,0xffffffff},1};
            chars_scene_lighting(&light,NULL);chars_draw_material(&h->models,id,root,pose,-1,0,1,NULL);
        }
    }
    if(flags[5])for(int i=0;i<6;i++){
        int owner=9+rank,clip=0;unsigned t=clock%length;float z=-1.5f,diffuse=1.f/.025f,ambient=diffuse;
        if(clock>=length*3){
            if(i==rank){clip=1;unsigned n=scene_word(&h->source,h->motion[owner][1]+4);t=clock-3*length;if(t>=n)t=n-1;z+=.025f;}
            else {t=length-1;diffuse=ambient=.5f;}
        }
        /* A shared clip must not replace each mesh's unkeyed bind TRS. */
        CharPose pose[64];int bones=pose_motion(h,9+i,owner,0,clip ? length-1:t,pose);if(bones<0)continue;
        if(clip)for(int b=0;b<bones;b++)scene_sample_njs(&h->source,h->motion[owner][1],t,b,0,pose+b);
        const float root[16]={.025f,0,0,0,0,.025f,0,0,0,0,.025f,z,0,0,0,1};
        const CharLight light={{1,1,1},diffuse,ambient,{0xffffffff,0xffffffff},1};
        chars_scene_lighting(&light,NULL);chars_draw_material(&h->models,9+i,root,pose,-1,0,1,NULL);
    }
    chars_scene_lighting(NULL,NULL);sceGuDepthMask(GU_FALSE);sceGuDepthFunc(GU_GEQUAL);
    matrix(GU_PROJECTION,projection);matrix(GU_VIEW,view);matrix(GU_MODEL,identity);
}
static void matrix(int mode,const float v[16])
{ScePspFMatrix4 m;float *p=(float*)&m;for(int r=0;r<4;r++)for(int c=0;c<4;c++)p[c*4+r]=v[r*4+c];sceGuSetMatrix(mode,&m);}
static void model(FailureModels *h,int id,int clip,float frame,float scale)
{
    CharPose pose[64];if(failure_models_pose(h,id,clip,frame,pose)<0)return;
    const float root[16]={scale,0,0,0,0,scale,0,0,0,0,scale,-1.1f,0,0,0,1};
    const CharLight light={{1,1,1},1.f/scale,.5f/scale,{0xffffffff,0xffffffff},1};
    chars_scene_lighting(&light,NULL);chars_draw_material(&h->models,id,root,pose,-1,0,1,NULL);chars_scene_lighting(NULL,NULL);
}
void failure_hud_draw(FailureModels *h,const FailureHud *state,int kind,int mode,const float projection[16],const float view[16])
{
    if(!h->models.h || !state->active || (state->phase!=3 && state->phase!=4))return;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float near=.5f,far=1000;
    float hp[16]={500.f/320.f,0,0,0,0,500.f/240.f,0,0,0,0,(far+near)/(near-far),2.f*far*near/(near-far),0,0,-1,0};
    hud_projection(hp,HUD_CENTRE);
    matrix(GU_VIEW,identity);matrix(GU_PROJECTION,hp);sceGuClear(GU_DEPTH_BUFFER_BIT);sceGuDepthFunc(GU_GEQUAL);
    h->models.drawn_tris=0;
    unsigned clock=state->clock;int id=failure_hud_heading(kind,state->cause);
    unsigned frames=scene_word(&h->source,h->motion[id][0]+4);
    model(h,id,0,clock<frames ? (float)clock:-1,.02f);
    if(state->choices){
        unsigned intro=scene_word(&h->source,h->motion[4][0]+4),clip=clock<intro ? 0:1,time=clock<intro ? clock:clock-intro;
        int other=mode ? 5:6;
        for(int i=0;i<2;i++){
            id=i ? 4:other;frames=scene_word(&h->source,h->motion[id][clip]+4);
            int animated=(i ? state->selection==1:state->selection!=1) || clock<frames;
            model(h,id,clip,animated && frames ? (float)(time%frames):-1,.022f);
        }
    }
    sceGuDepthMask(GU_FALSE);sceGuDepthFunc(GU_GEQUAL);
    matrix(GU_PROJECTION,projection);matrix(GU_VIEW,view);matrix(GU_MODEL,identity);
}
