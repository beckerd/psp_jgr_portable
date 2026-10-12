#include "diagnostics.h"
#include "garage_shadow.h"
#include "charpose.h"
#include "source_shadow.h"
#include "render_dot.h"
#include "garage_light.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __PSP__
#include <pspkernel.h>
#endif
static const float (*shadow_planes)[4];
static unsigned shadow_pose_us,shadow_transform_us,shadow_volume_us,shadow_submit_us;
static unsigned shadow_clock(void)
{
#if defined(__PSP__) && JGR_DIAGNOSTICS
    return sceKernelGetSystemTimeLow();
#else
    return 0;
#endif
}
static int shadow_exact=
#ifdef __PSP__
    0;
#else
    1;
#endif
void garage_shadow_exact(int enabled){shadow_exact=!!enabled;}
void garage_shadow_profile(unsigned out[4])
{
    out[0]=shadow_pose_us;out[1]=shadow_transform_us;out[2]=shadow_volume_us;out[3]=shadow_submit_us;
    shadow_pose_us=shadow_transform_us=shadow_volume_us=shadow_submit_us=0;
}
void garage_shadow_frustum(const float planes[6][4]){shadow_planes=planes;}
void garage_shadow_free(GarageShadow *s){free(s->radius);free(s->image);memset(s,0,sizeof *s);}
int garage_shadow_load(GarageShadow *s,const char *path)
{
    memset(s,0,sizeof *s);FILE *f=fopen(path,"rb");if(!f)return -1;
    if(fseek(f,0,SEEK_END)){fclose(f);return -1;}
    long size=ftell(f);rewind(f);
    if(size<152 || size>0x100000){fclose(f);return -2;}
    s->image=malloc(size);
    if(!s->image || fread(s->image,1,size,f)!=(unsigned)size){fclose(f);garage_shadow_free(s);return -3;}
    fclose(f);const GarageShadowHeader *h=(const void*)s->image;s->header=h;
    if(memcmp(h->magic,"GSH1",4)||h->size!=(unsigned)size||h->nodes>960||h->vertices>30720||h->indices>184320||
       h->off_nodes!=152||h->off_vertices!=152+h->nodes*sizeof(GarageShadowNode)||
       h->off_indices!=h->off_vertices+h->vertices*12||h->size!=h->off_indices+h->indices*2)goto invalid;
    s->residents=(const void*)(s->image+32);s->nodes=(const void*)(s->image+h->off_nodes);
    s->vertices=(const void*)(s->image+h->off_vertices);s->indices=(const void*)(s->image+h->off_indices);
    for(int r=0;r<15;r++) {
        unsigned first=s->residents[r][0],n=s->residents[r][1];
        if(first>h->nodes || n>h->nodes-first || n>64)goto invalid;
        for(unsigned j=0;j<n;j++) {
            const GarageShadowNode *b=s->nodes+first+j;
            if(b->parent<-1 || b->parent>=(int)j||b->nv>32||b->v0>h->vertices||b->nv>h->vertices-b->v0||
               b->i0>h->indices||b->ni>h->indices-b->i0||b->ni>512||b->strips>64)goto invalid;
        }
    }
    s->radius=malloc(h->nodes*sizeof *s->radius);
    if(s->radius)for(unsigned j=0;j<h->nodes;j++) {
        const GarageShadowNode *b=s->nodes+j;float radius=0;
        for(unsigned k=0;k<b->nv;k++)for(unsigned axis=0;axis<3;axis++) {
            float v=fabsf(s->vertices[b->v0+k][axis]);
            if(!isfinite(v)){radius=INFINITY;break;}
            if(v>radius)radius=v;
        }
        s->radius[j]=radius;
    }
    return 0;
invalid:garage_shadow_free(s);return -4;
}
/* Reject only complete extruded volumes outside a camera plane. Casters
   themselves may be off screen while their shadows are visible. The source
   near clip has literal register-reuse behavior, so any volume touching
   that plane stays on the unmodified reference path. */
static int shadow_mesh_visible(const float matrix[16],float radius,const float light[3])
{
    if(!shadow_planes || !isfinite(radius))return 1;
    float center[3],extent[3];
    for(unsigned axis=0;axis<3;axis++) {
        const float *m=matrix+axis*4;float offset=light[axis]*30.f;
        center[axis]=m[3]+offset*.5f;
        extent[axis]=radius*(fabsf(m[0])+fabsf(m[1])+fabsf(m[2]))+fabsf(offset)*.5f;
        extent[axis]+=.01f+(fabsf(m[3])+extent[axis])*1e-5f;
        if(!isfinite(center[axis]) || !isfinite(extent[axis]))return 1;
    }
    const float *near=shadow_planes[4];float d=near[3],e=0;
    for(unsigned k=0;k<3;k++){d+=near[k]*center[k];e+=fabsf(near[k])*extent[k];}
    if(d+e<-.05f)return 0; /* source_shadow_prepare rejects both endpoints behind Z=1. */
    if(d-e<=.05f)return 1;
    for(unsigned i=0;i<6;i++) {
        const float *p=shadow_planes[i];d=p[3];
        for(unsigned k=0;k<3;k++)d+=p[k]*center[k]+fabsf(p[k])*extent[k];
        if(d<-.05f)return 0;
    }
    return 1;
}
int garage_shadow_resident(const GarageShadow *s,const Chars *bank,int resident,int motion,
    float frame,const float root[16],const float view[16],GarageShadowSubmit submit,void *context)
{
    if(!s || !s->header || !bank || !bank->h || !root || !view || !submit || (unsigned)resident>=15 ||
       (unsigned)(resident+1)>=bank->h->n_chars || !isfinite(frame))return -1;
    unsigned first=s->residents[resident][0],n=s->residents[resident][1];if(!n)return 0;
    const JgcChar *ch=bank->chars+resident+1;
    if(ch->nbones!=n || (unsigned)motion>=ch->nmotions)return -1;
    CharPose pose[64];float matrices[64][16];unsigned started=shadow_clock();
    for(unsigned j=0;j<n;j++) {
        const GarageShadowNode *b=s->nodes+first+j;
        if(b->parent!=bank->bones[ch->bone0+j].parent-(int)ch->bone0 &&
           !(b->parent==-1 && bank->bones[ch->bone0+j].parent==-1))return -1;
        pose[j].ev=b->ev;memcpy(pose[j].pos,b->pos,12);memcpy(pose[j].ang,b->ang,12);memcpy(pose[j].scl,b->scale,12);
        chars_overlay_pose(bank,ch,ch->motion0+motion,frame,j,1,pose+j);
    }
    chars_pose_mesh_matrices(bank,ch,pose,root,matrices);
    shadow_pose_us+=shadow_clock()-started;
    CharLight normal,cel;garage_light_values(&normal,&cel);unsigned meshes=0;
    float light[3];
    for(int r=0;r<3;r++) {
        const float *m=view+r*4;
        light[r]=render_dot(m,cel.direction,3);
        if(r)light[r]=-light[r]; /* Ninja view: +Y down, +Z forward. */
    }
    for(unsigned j=0;j<n;j++) {
        const GarageShadowNode *b=s->nodes+first+j;if(!b->nv)continue;
        if(s->radius && !shadow_mesh_visible(matrices[j],s->radius[first+j],cel.direction))continue;
        float v[32][3],volume[64][3];uint16_t indices[3072];unsigned t0=shadow_clock();
        if(shadow_exact)for(unsigned k=0;k<b->nv;k++) {
            const float *input=s->vertices[b->v0+k];
            float world[4]={0,0,0,1},p[4]={input[0],input[1],input[2],1};
            for(int r=0;r<3;r++) {
                const float *m=matrices[j]+r*4;
                world[r]=render_dot(m,p,4);
            }
            for(int r=0;r<3;r++) {
                const float *m=view+r*4;
                v[k][r]=render_dot(m,world,4);
                if(r)v[k][r]=-v[k][r];
            }
        } else {
            /* PSP: one combined view*node matrix in native floats. The
               ordered binary64 path above cost 13 ms per selection frame
               and stays the reference for the host tests; corners differ
               by well under a hundredth of a unit. */
            float mv[12];const float *m=matrices[j];
            for(int r=0;r<3;r++)for(int col=0;col<4;col++) {
                const float *vr=view+r*4;
                mv[r*4+col]=vr[0]*m[col]+vr[1]*m[4+col]+vr[2]*m[8+col]+(col==3 ? vr[3]:0.f);
            }
            for(unsigned k=0;k<b->nv;k++) {
                const float *p=s->vertices[b->v0+k];
                v[k][0]=mv[0]*p[0]+mv[1]*p[1]+mv[2]*p[2]+mv[3];
                v[k][1]=-(mv[4]*p[0]+mv[5]*p[1]+mv[6]*p[2]+mv[7]);
                v[k][2]=-(mv[8]*p[0]+mv[9]*p[1]+mv[10]*p[2]+mv[11]);
            }
        }
        unsigned t1=shadow_clock();shadow_transform_us+=t1-t0;
        int count=source_shadow_volume(v,b->nv,s->indices+b->i0,b->ni,b->strips,light,30,volume,indices,3072);
        if(count<0)return -2;
        unsigned t2=shadow_clock();shadow_volume_us+=t2-t1;
        for(unsigned k=0;k<b->nv*2;k++){volume[k][1]=-volume[k][1];volume[k][2]=-volume[k][2];}
        if(count)submit(context,volume,b->nv*2,indices,count);
        shadow_submit_us+=shadow_clock()-t2;
        meshes++;
    }
    return meshes;
}
