#ifndef JGR_OBJECT_VIEW_H
#define JGR_OBJECT_VIEW_H
#include <math.h>
#include <string.h>

/* 0c287c prepares the three task views; 0c32cc/3344 test wedges and
   0c3470 tests the small XZ circle. City initializers use different sizes.
   Keep this explicit and frame-local: a cinematic/garage view must never
   inherit the last gameplay camera. No actor/physics task is culled here. */
typedef struct {
    float eye[3],fx,fz,fy,horizontal,tangent,cx,cz,radius2,pickup_back,tag_back;
    int valid;
} ObjectView;
static inline void object_view_setup(ObjectView *v,unsigned city,const float eye[3],
                                     const float forward[3],float tangent,float back_scale)
{
    memset(v,0,sizeof *v);
    if(city<1 || city>6 || city==4 || !(tangent>0.f) || !isfinite(tangent))return;
    for(int k=0;k<3;k++)if(!isfinite(eye[k]) || !isfinite(forward[k]))return;
    float horizontal=sqrtf(forward[0]*forward[0]+forward[2]*forward[2]);
    float length=sqrtf(horizontal*horizontal+forward[1]*forward[1]);
    if(!(horizontal>.0001f) || !isfinite(length))return;
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_object_view_h__small_0.inc"
#else

#include "generated/source_data/object_view_h__small_0.inc"

#endif

    const float *s=small[city-1];
    memcpy(v->eye,eye,12);v->fx=forward[0]/horizontal;v->fz=forward[2]/horizontal;
    v->fy=forward[1]/length;v->horizontal=horizontal/length;v->tangent=tangent;
    /* Circles use normalized XZ; wedge origins use the full 3D forward.
       0c287c's alternate source view multiplies only the back offsets. */
    v->cx=eye[0]+v->fx*s[0];v->cz=eye[2]+v->fz*s[0];v->radius2=s[1]*s[1];
    v->pickup_back=s[2]*back_scale*v->horizontal;v->tag_back=s[3]*back_scale*v->horizontal;
    v->valid=1;
}
static inline int object_view_wedge(const ObjectView *v,float dx,float dz,float back)
{
    if(v->fy<-.4f || v->fy>.3f)return 1;
    float front=dx*v->fx+dz*v->fz+back,side=dx*v->fz-dz*v->fx;
    /* Use the live PSP horizontal FOV, retaining an outward rounding margin
       at the boundary. A wider display must not lose its side objects. */
    float margin=.001f+(fabsf(dx)+fabsf(dz)+back)*0x1p-18f;
    return front>=-margin && fabsf(side)<=front*v->tangent+margin;
}
/* 0c3498 alone: cranes, lifts and selected animated scenery use the
   caller's current medium circle, without either task-view wedge. */
static inline int object_circle_visible(const ObjectView *v,float x,float y,float z)
{
#ifndef JGR_OBJECT_VIEW_REFERENCE
    if(v && v->valid) {
        if(!isfinite(x) || !isfinite(y) || !isfinite(z))return 1;
        float dx=x-v->cx,dz=z-v->cz;
        return dx*dx+dz*dz<=v->radius2+.01f;
    }
#else
    (void)v;(void)x;(void)y;(void)z;
#endif
    return 1;
}
/* The view's circle (0c3470 small by default, 0c3498 after traffic_view_setup)
   and the wedge whose apex sits `back` behind the eye. */
static inline int object_range_visible(const ObjectView *v,float x,float y,float z,float back)
{
#ifndef JGR_OBJECT_VIEW_REFERENCE
    if(v && v->valid) {
        if(!isfinite(x) || !isfinite(y) || !isfinite(z))return 1;
        float dx=x-v->cx,dz=z-v->cz;
        if(dx*dx+dz*dz>v->radius2+.01f)return 0;
        return object_view_wedge(v,x-v->eye[0],z-v->eye[2],back);
    }
#else
    (void)v;(void)x;(void)y;(void)z;(void)back;
#endif
    return 1;
}
/* 0c3470 small circle with the 0c32cc wedge: cans, health and effects. */
static inline int object_pickup_visible(const ObjectView *v,float x,float y,float z)
{return object_range_visible(v,x,y,z,v ? v->pickup_back:0.f);}
/* 0c3498 standard circle with the 0c3344 wedge, whose apex sits tag_back
   behind the eye: 06b920 traffic and 091f64 set-placed objects. */
static inline int object_prop_visible(const ObjectView *v,float x,float y,float z)
{return object_range_visible(v,x,y,z,v ? v->tag_back:0.f);}
static inline int object_tag_visible(const ObjectView *v,float x,float y,float z)
{
#ifndef JGR_OBJECT_VIEW_REFERENCE
    if(v && v->valid) {
        float dx=x-v->eye[0],dy=y-v->eye[1],dz=z-v->eye[2];
        if(!isfinite(dx) || !isfinite(dy) || !isfinite(dz))return 1;
        if(dx*dx+dy*dy+dz*dz>1440000.f)return 0;
        return object_view_wedge(v,dx,dz,v->tag_back);
    }
#endif
    return 1;
}
#endif
