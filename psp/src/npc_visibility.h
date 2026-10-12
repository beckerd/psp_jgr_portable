#ifndef NPC_VISIBILITY_H
#define NPC_VISIBILITY_H
#include <math.h>

/* Source street-person view/fade policy: 089b78, 08b608, 0c9d50, 08a2c0.
   PSP distance budget: opaque through 150, fade to zero at 200 (source
   used 170/300). Coordinates are camera-local, positive Z forward; all
   distance thresholds retain the source FOV scaling. */
typedef struct { float near_fade,far_fade,far_clip,cone_tan,cone_inverse_cos,x_scale; } NpcVisibility;
static inline NpcVisibility npc_visibility_params(int fov,float (*sine)(int))
{
    if(fov<=0 || fov>=32768)fov=0x3555;
    float scale=fov==0x3555 ? 1.f:13653.f/(float)fov;
    int angle=(int)((float)(fov>>1)*1.2f); /* 0c9d08: truncate AFTER multiplying */
    float inverse=1.f/sine(angle+16384);
    return (NpcVisibility){20.f*scale,150.f*scale,200.f*scale,sine(angle)*inverse,inverse,1.f};
}
static inline void npc_visibility_aspect(NpcVisibility *v,float aspect)
{
    /* The source cone surrounds a 640x480 viewport. main preserves its
       vertical lens on PSP, exposing more horizontal space at 480x272.
       Test that extra space in source screen coordinates; using the old
       circular cone directly clips people inside the widescreen edges.
       Keep depth/fade and the original 4:3 policy unchanged. */
    v->x_scale=isfinite(aspect) && aspect>4.f/3.f ? (4.f/3.f)/aspect:1.f;
}
static inline int npc_visibility_class(const NpcVisibility *v,float x,float y,float depth)
{
    if(depth < -8.f || depth > v->far_clip)return 0;
    x*=v->x_scale;
    if((sqrtf(x*x+y*y)-depth*v->cone_tan)*v->cone_inverse_cos>8.f)return 0;
    return depth<=v->near_fade ? 1:depth<v->far_fade ? 2:3;
}
static inline float npc_visibility_opacity(const NpcVisibility *v,int mode,float depth)
{
    if(!mode)return 0.f;
    if(mode==2)return 1.f;
    /* Source writes negative sqrt to Ninja's additive material alpha. */
    float fade=mode==1 ? 1.f-depth/v->near_fade:
        (depth-v->far_fade)/(v->far_clip-v->far_fade);
    if(fade<=0.f)return 1.f;
    if(fade>=1.f)return 0.f;
    return 1.f-sqrtf(fade);
}
/* 06dd7c actor gate on the camera depth of the source bound centre: 0 hidden
   beyond far, 1 detailed model below near, 2 low model up to fade, 3 low
   model with (fade-depth)/(far-fade) written to the additive material alpha,
   a linear fade that reaches invisible at far. near==fade==far expresses a
   0c9d50 hard cut. The frustum test replaces the original viewport cone. */
static inline int source_fade_class(float depth,float near,float fade,float far)
{
    if(!isfinite(depth) || depth>far)return 0;
    return depth<near ? 1:depth<fade ? 2:3;
}
static inline float source_fade_opacity(int mode,float depth,float fade,float far)
{
    if(!mode)return 0.f;
    if(mode<3 || !(far>fade))return 1.f;
    float t=(depth-fade)/(far-fade);
    return t<=0.f ? 1.f:t>=1.f ? 0.f:1.f-t;
}
#endif
