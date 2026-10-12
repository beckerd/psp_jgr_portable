#include "ge_submit.h"
#include "ge_projection.h"
#include "object_graffiti.h"
#include "object_graffiti_pose.h"
#include <pspkernel.h>
#include <pspgu.h>
#include <pspgum.h>
#include <malloc.h>
#include <stdlib.h>

unsigned short *gfo_texture_load(const char *path)
{
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0777);if(fd<0)return NULL;
    if(sceIoLseek(fd,0,PSP_SEEK_END)!=128*128*2){sceIoClose(fd);return NULL;}
    sceIoLseek(fd,0,PSP_SEEK_SET);
    unsigned short *p=memalign(16,128*128*2);
    if(!p){sceIoClose(fd);return NULL;}
    int n=sceIoRead(fd,p,128*128*2);sceIoClose(fd);
    if(n!=128*128*2){free(p);return NULL;}
    for(int i=0;i<128*128;i++) {unsigned c=p[i];p[i]=(c&0xf0f0)|((c>>8)&15)|((c&15)<<8);}
    sceKernelDcacheWritebackRange(p,128*128*2);return p;
}
typedef struct {float u,v;unsigned color;float x,y,z;} Vertex;
static void draw_actor(const GfoObject *o,const unsigned short *texture,const Chars *owner,
                    int character,int motion,float frame,const float root[16],const CharPose *completed)
{
    if(!texture || !o || !o->handle || !o->progress.started)return;
    Chars bank={0};JgcChar ch={0};int mi=-1;
    if(owner) {
        if(character<0 || (unsigned)character>=owner->h->n_chars)return;
        bank=*owner;ch=owner->chars[character];
        if(o->tpl->nbones>ch.nbones || motion<0 || (unsigned)motion>=ch.nmotions)return;
        mi=(int)ch.motion0+motion;
    }
    /* 07bcd8 / 07df68 / 099ed4 pass the member's current motion to
       0b8b1c, but with GF+bc's OWN tree. Channels advance once per node.
       Missing channels use the tag's bind TRS, never the actor's bind TRS. */
    bank.bones=(JgcBone *)gfo_bones(o->tpl);ch.bone0=0;ch.nbones=o->tpl->nbones;
    float mats[64][16];
    if(completed) {
        CharPose pose[64];if(!gfo_completed_pose(o->tpl,completed,pose))return;
        chars_pose_mesh_matrices(&bank,&ch,pose,root,mats);
    } else chars_mesh_matrices(&bank,&ch,mi,frame,root,mats);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_4444,0,0,0);
    sceGuTexImage(0,128,128,128,texture);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_CULL_FACE);sceGuDepthMask(GU_TRUE);
    /* Animated owner geometry has the same sub-depth-step paint separation
       as city walls. Keep body/limb occlusion and the original animated mesh. */
    sceGuEnable(GU_DEPTH_TEST);sceGuDepthOffset(GE_DECAL_DEPTH_BIAS);
    {
        const GfoVertex *src=gfo_vertices(o->tpl);
        Vertex *v=sceGuGetMemory(o->tpl->nvertex*sizeof *v);
        for(unsigned j=0;j<o->tpl->nvertex;j++) {
            const float *p=src[j].pos,*m=mats[src[j].bone];v[j].u=src[j].u;v[j].v=src[j].v;
            v[j].color=gfo_colour(&o->progress,src[j].reveal,0);
            v[j].x=m[0]*p[0]+m[1]*p[1]+m[2]*p[2]+m[3];
            v[j].y=m[4]*p[0]+m[5]*p[1]+m[6]*p[2]+m[7];
            v[j].z=m[8]*p[0]+m[9]*p[1]+m[10]*p[2]+m[11];
        }
        ge_submit_aux(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,o->tpl->nvertex,0,v);
    }
    sceGuDepthOffset(0);
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);sceGuEnable(GU_CULL_FACE);sceGuDisable(GU_TEXTURE_2D);
}
void gfo_draw_actor(const GfoObject *o,const unsigned short *texture,const Chars *owner,
                    int character,int motion,float frame,const float root[16])
{draw_actor(o,texture,owner,character,motion,frame,root,NULL);}
void gfo_draw_actor_pose(const GfoObject *o,const unsigned short *texture,const CharPose *pose,const float root[16])
{draw_actor(o,texture,NULL,0,-1,0,root,pose);}
void gfo_draw(const GfoWorld *w,const unsigned short *texture)
{
    for(unsigned i=0;i<GFO_CAPACITY;i++) {
        const GfoObject *o=w->objects+i;if(!o->handle || !o->visible)continue;
        float root[16];for(int r=0;r<4;r++)for(int c=0;c<4;c++)root[r*4+c]=o->matrix[c*4+r];
        gfo_draw_actor(o,texture,NULL,0,-1,0,root);
    }
}
