#include "movie_shadow.h"
#include "charpose.h"
#include "source_shadow.h"
#include "render_dot.h"
#include <string.h>
/* Render-thread scratch, not reentrant. No frame-time heap allocation. */
static uint32_t nodes[CHAR_RENDER_MAX_BONES];
static int parents[CHAR_RENDER_MAX_BONES];
static CharPose poses[CHAR_RENDER_MAX_BONES];
static float matrices[CHAR_RENDER_MAX_BONES][16];
static int16_t shadow_strips[3072];
static int tree(const Scene *s,uint32_t p,int parent,unsigned *count)
{
    while(p) {
        if(*count>=CHAR_RENDER_MAX_BONES || !scene_range(s,p,52))return 0;
        unsigned own=(*count)++;nodes[own]=p;parents[own]=parent;
        if(!tree(s,scene_word(s,p+44),own,count))return 0;
        p=scene_word(s,p+48);
    }
    return 1;
}
int movie_shadow_actor(const Movie *movie,const Chars *bank,unsigned actor,const float view[16],GarageShadowSubmit submit,void *context)
{
    if(!movie || !bank || !bank->h || !view || !submit || actor>=(unsigned)movie->model_count)return -1;
    const Scene *s=&movie->source;uint32_t a=movie_actor(movie,actor);
    if(scene_word(s,a+12)!=8)return 0;
    unsigned model=movie->model0+actor;if(model>=bank->h->n_chars)return -1;
    const JgcChar *ch=bank->chars+model;unsigned n=0;
    if(!tree(s,scene_word(s,a),-1,&n) || n!=ch->nbones || n>64)return -1;
    uint32_t motion=scene_word(s,a+4);float frame=movie_actor_frame(movie,a),root[16]={0};
    for(int i=0;i<16;i+=5)root[i]=1;
    for(unsigned j=0;j<n;j++) {
        const JgcBone *b=bank->bones+ch->bone0+j;
        if(b->parent!=(parents[j]<0 ? -1:parents[j]+(int)ch->bone0))return -1;
        chars_sample_pose(bank,ch,-1,0,j,poses+j);
        /* Shadow-motion renderer uses the same CNK interpolation family
           as the cel renderer, NOT ordinary Ninja's weighted endpoints. */
        if(motion && !scene_sample_njs_nodes(s,motion,frame,j,1,n,poses+j))return -1;
    }
    chars_pose_mesh_matrices(bank,ch,poses,root,matrices);
    uint32_t lp=scene_word(s,movie->descriptor+4),lightp=scene_word(s,lp+4);
    float direction[3],light[3];if(!scene_read(s,lightp,direction,12))return -1;
    for(int r=0;r<3;r++) {
        const float *v=view+r*4;
        light[r]=render_dot(v,direction,3);
        if(r)light[r]=-light[r]; /* PSP view -> Ninja +Y down, +Z forward */
    }
    int meshes=0;
    for(unsigned j=0;j<n;j++) {
        uint32_t mesh=scene_word(s,nodes[j]+4);if(!mesh)continue;
        if(!scene_range(s,mesh,24))return -1;
        uint32_t vl=scene_word(s,mesh),pl=scene_word(s,mesh+4);
        uint16_t vh[4],ph[3];
        if(!scene_read(s,vl,vh,sizeof vh) || !scene_read(s,pl,ph,sizeof ph))return -1;
        unsigned nv=vh[3],words=0;
        if(vh[0]!=34 || vh[2]!=0 || !nv || nv>32 || vh[1]!=1+3*nv || ph[0]!=58 || !ph[2] || ph[2]>64)return -1;
        float input[32][3],v[32][3],volume[64][3];uint16_t indices[3072];
        if(!ph[1] || !scene_read(s,vl+8,input,nv*12))return -1;
        unsigned capacity=ph[1]-1;
        const int16_t *strips=scene_bytes(s,pl+6,capacity*2);
        if(!strips) {
            if(capacity>sizeof shadow_strips/sizeof *shadow_strips ||
               !scene_read(s,pl+6,shadow_strips,capacity*2))return -1;
            strips=shadow_strips;
        }
        for(unsigned k=0;k<ph[2];k++) {
            if(words>=capacity)return -1;
            int len=strips[words++];if(len<0)len=-len;
            if(len<3 || (unsigned)len>capacity-words)return -1;
            words+=len;
        }
        for(unsigned k=0;k<nv;k++) {
            float world[4]={0,0,0,1},p[4]={input[k][0],input[k][1],input[k][2],1};
            for(int r=0;r<3;r++)world[r]=render_dot(matrices[j]+r*4,p,4);
            for(int r=0;r<3;r++){v[k][r]=render_dot(view+r*4,world,4);if(r)v[k][r]=-v[k][r];}
        }
        int count=source_shadow_volume(v,nv,strips,words,ph[2],light,500,volume,indices,3072);
        if(count<0)return -1;
        for(unsigned k=0;k<nv*2;k++){volume[k][1]=-volume[k][1];volume[k][2]=-volume[k][2];}
        if(count)submit(context,volume,nv*2,indices,count);
        meshes++;
    }
    return meshes;
}
