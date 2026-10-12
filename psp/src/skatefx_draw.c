#include "ge_submit.h"
#include "skatefx_draw.h"
#include <pspgu.h>
#include <pspgum.h>
typedef struct {float u,v;unsigned colour;float x,y,z;} TrailVertex;
static void ribbon_draw(const SkateRibbon *r,float u,float v)
{
    if(r->count<2)return;
    TrailVertex *out=sceGuGetMemory(r->count*2*sizeof *out);
    for(int i=0;i<r->count;i++)for(int side=0;side<2;side++){
        /* 17f18c / 0d98a8: ten samples, white modulation, the lower
           half of the character's original trail texture; additive. */
        float x=(float)i/9.f+r->uv_shift;if(x>1.f)x=1.f;
        const float *p=r->points[i][side];
        out[i*2+side]=(TrailVertex){(u+x*63.f)/256.f,(v+(side ? 63.f:32.f))/128.f,0xffffffff,p[0],p[1],p[2]};
    }
    ge_submit_aux(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,r->count*2,0,out);
}
void skate_ribbons_draw(const SkateEffects *fx,int model,const Tex2dSet *textures)
{
    if(!fx || (unsigned)model>=16 || !textures || textures->n!=2)return;
    int active=fx->dash_age>=2 || fx->trail[0].count>=2 || fx->trail[1].count>=2;
    for(int i=0;i<20 && !active;i++)active=fx->trail_tails[i].count>=2;
    if(!active)return;
    const Tex2d *t=textures->t+model/8;float u=(model%4)*64.f,v=(model%8/4)*64.f;
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGumUpdateMatrix();
    sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_ALPHA_TEST);sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);sceGuEnable(GU_BLEND);sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_4444,0,0,0);sceGuTexImage(0,t->w,t->h,t->w,t->px);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    /* 045cc8 is the subtle dark skate smear, not the luminous jump trail.
       UVs 0..127 select the upper-left half; vertex alpha grows with age. */
    if(fx->dash_age>=2){
        sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
        int n=fx->dash_age;
        for(int side=0;side<2;side++){
            TrailVertex *out=sceGuGetMemory(n*2*sizeof *out);
            for(int i=0;i<n;i++)for(int edge=0;edge<2;edge++){
                const float *p=fx->dash_points[side][i][edge];unsigned alpha=i*255/(n-1);
                out[edge ? n*2-1-i:i]=(TrailVertex){(u+(127*i/(n-1))*.25f)/256.f,(v+edge*31.75f)/128.f,
                    (alpha<<24)|0xffffff,p[0],p[1],p[2]};
            }
            ge_submit_aux(GU_TRIANGLE_FAN,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,n*2,0,out);
        }
    }
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_FIX,0,0xffffff);
    for(int side=0;side<2;side++)ribbon_draw(fx->trail+side,u,v);
    for(int i=0;i<20;i++)ribbon_draw(fx->trail_tails+i,u,v);
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);sceGuDisable(GU_TEXTURE_2D);sceGuEnable(GU_CULL_FACE);
}
