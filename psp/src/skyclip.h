#ifndef JGR_SKYCLIP_H
#define JGR_SKYCLIP_H
#include "stage.h"
#include "clip_math.h"
#include "vfpu_profile.h"
#include <math.h>
#include <string.h>
/* PSP submission adapter, NOT source scenery changes. Huge background
   triangles can exceed the GE's screen-coordinate range. Clip them before
   submission, retaining world positions and perspective-correct UVs. */
static inline int skyclip_triangle_masked_inner(const JgsVert input[3],const float planes[6][4],unsigned mask,JgsVert output[27],unsigned *vectors)
{
    JgsVert a[12]={{0}},b[12];int n=3;memcpy(a,input,3*sizeof *a);
    for(int p=0;p<6 && n;p++) {
        if(!(mask&(1u<<p)))continue;
        int count=0;const float *plane=planes[p];
        float distance[12] __attribute__((aligned(16)));
        *vectors+=((unsigned)n+3)/4;
        clip_math_polygon_distances(a,(unsigned)n,plane,distance);
        unsigned outside=0;
        for(int i=0;i<n;i++){if(!isfinite(distance[i]))return 0;outside+=distance[i]<0;}
        /* Accepted planes cannot change any vertex bytes. Rejected planes
           have no output. Avoid copying the entire polygon in either case. */
        if(!outside)continue;
        if(outside==(unsigned)n)return 0;
        for(int i=0;i<n;i++) {
            const JgsVert *v=a+i,*w=a+(i+1)%n;
            float d=distance[i],e=distance[(i+1)%n];
            if(d>=0){if(count>=12)return 0;b[count++]=*v;}
            if((d>=0)!=(e>=0)) {
                if(count>=12)return 0;
                float t=d/(d-e);JgsVert *q=b+count++;
                q->x=v->x+t*(w->x-v->x);q->y=v->y+t*(w->y-v->y);q->z=v->z+t*(w->z-v->z);
                q->u=v->u+t*(w->u-v->u);q->v=v->v+t*(w->v-v->v);q->color=0;
                for(int k=0;k<32;k+=8){float c=(v->color>>k)&255;
                    q->color|=(unsigned)(c+t*((float)((w->color>>k)&255)-c)+.5f)<<k;}
            }
        }
        memcpy(a,b,count*sizeof *a);n=count;
    }
    if(n>11)return 0;
    int count=0;for(int i=1;i+1<n;i++){output[count++]=a[0];output[count++]=a[i];output[count++]=a[i+1];}
    return count;
}
static inline int skyclip_triangle_inner(const JgsVert input[3],const float planes[6][4],JgsVert output[27],unsigned *vectors)
{return skyclip_triangle_masked_inner(input,planes,63,output,vectors);}
static inline int skyclip_triangle(const JgsVert input[3],const float planes[6][4],JgsVert output[27])
{
    unsigned vectors=0,stamp=vfpu_profile_clip_begin();
#if CLIP_MATH_VFPU
    RenderMathState state;render_math_begin(&state);
#endif
    int count=skyclip_triangle_inner(input,planes,output,&vectors);
#if CLIP_MATH_VFPU
    render_math_end(&state);
#endif
    vfpu_profile_clip_end(stamp,vectors);return count;
}
#endif
