#ifndef JGR_SOURCE_LIGHT_H
#define JGR_SOURCE_LIGHT_H
#include <stdint.h>
#include <math.h>
#include <float.h>
#include "render_dot.h"
/* Original 2337b4 (normal) and 2956e0 (cel) scalar kernels. Transform the
   light by the model-view transpose, then FMUL each component by -diffuse;
   FIPR adds ambient as its fourth product. Normal has an ambient floor;
   cel retains signed intensity and splits triangles at zero. */
static inline float source_light_value(const float local[3],const float normal[3],float diffuse,float ambient,int cel)
{
    float x=local[0]*-diffuse,y=local[1]*-diffuse,z=local[2]*-diffuse;
    const float a[4]={x,y,z,ambient},b[4]={normal[0],normal[1],normal[2],1};
    float value=render_dot(a,b,4);return cel ? value:fmaxf(ambient,value);
}
/* 24980c adds the 27126c material offset BEFORE light multiplication.
   Delay saturation/quantization until final vertex packing. */
static inline uint32_t source_light_colour_offset(uint32_t material,const float rgb[3],float intensity,const float *offset)
{
    uint32_t result=material&0xff000000u;
    for(unsigned j=0;j<3;j++) {
        float value=(float)((material>>(j*8))&255)/255.f;
        if(offset)value+=offset[j];
        value*=rgb[j];
        value*=intensity;value*=255.f;
        int channel=(int)value;if(channel>255)channel=255;if(channel<0)channel=0;
        result|=(unsigned)channel<<(j*8);
    }
    return result;
}
static inline uint32_t source_light_colour(uint32_t material,const float rgb[3],float intensity)
{return source_light_colour_offset(material,rgb,intensity,0);}
/* Most vertices only need an 8-bit colour or a side-of-light decision.
   Allegrex has no binary64 FPU. Bound the single-precision dot's error,
   keeping the original double kernel for ambiguous quantization/boundaries.
   This changes neither final vertex colours nor split-edge interpolation. */
static inline float source_light_estimate(const float local[3],const float normal[3],float diffuse,float ambient,float *error)
{
    float x=(local[0]*-diffuse)*normal[0],y=(local[1]*-diffuse)*normal[1],z=(local[2]*-diffuse)*normal[2];
    *error=(fabsf(x)+fabsf(y)+fabsf(z)+fabsf(ambient))*(8.f*FLT_EPSILON)+8.f*FLT_MIN;
    return ((x+y)+z)+ambient;
}
static inline uint32_t source_light_normal_colour(uint32_t material,const float rgb[3],const float local[3],const float normal[3],float diffuse,float ambient)
{
#ifndef JGR_LIGHT_REFERENCE
    float error,value=source_light_estimate(local,normal,diffuse,ambient,&error);
    if(isfinite(value) && isfinite(error)) {
        uint32_t low=source_light_colour(material,rgb,fmaxf(ambient,value-error));
        uint32_t high=source_light_colour(material,rgb,fmaxf(ambient,value+error));
        if(low==high)return low;
    }
#endif
    return source_light_colour(material,rgb,source_light_value(local,normal,diffuse,ambient,0));
}
/* One normal-light batch has immutable RGB and ambient. Retain the exact
   material normalization, rather than performing six channel divisions for
   every low/high quantization proof. This is draw-local CPU state only. */
typedef struct {
    float rgb[3],ambient,base[3];
    uint32_t material,ambient_colour;
    unsigned ready,valid;
} SourceNormalPalette;
static inline void source_normal_palette_begin(SourceNormalPalette *p,const float rgb[3],float ambient)
{
    p->ambient=ambient;p->valid=0;p->ready=isfinite(ambient);
    for(unsigned j=0;j<3;j++){p->rgb[j]=rgb[j];p->ready&=isfinite(rgb[j]);}
}
static inline uint32_t source_normal_palette_colour(const SourceNormalPalette *p,float intensity)
{
    uint32_t colour=p->material&0xff000000u;
    for(unsigned j=0;j<3;j++) {
        float value=p->base[j];value*=intensity;value*=255.f;
        int channel=(int)value;if(channel>255)channel=255;if(channel<0)channel=0;
        colour|=(unsigned)channel<<(j*8);
    }
    return colour;
}
static inline uint32_t source_light_normal_colour_cached(uint32_t material,SourceNormalPalette *p,
    const float local[3],const float normal[3],float diffuse)
{
#ifdef JGR_LIGHT_REFERENCE
    return source_light_normal_colour(material,p->rgb,local,normal,diffuse,p->ambient);
#else
    if(!p->ready)return source_light_normal_colour(material,p->rgb,local,normal,diffuse,p->ambient);
    if(!p->valid || p->material!=material) {
        p->material=material;p->valid=1;
        for(unsigned j=0;j<3;j++) {
            p->base[j]=(float)((material>>(j*8))&255)/255.f;
            p->base[j]*=p->rgb[j];
        }
    }
    float error,value=source_light_estimate(local,normal,diffuse,p->ambient,&error);
    if(isfinite(value) && isfinite(error)) {
        float low=value-error,high=value+error;
        /* Exactly the old pair of fmax/colour evaluations, collapsed when
           the complete intensity interval lies below the ambient floor. */
        if(high<=p->ambient) {
            if(!(p->valid&2u)){p->ambient_colour=source_normal_palette_colour(p,p->ambient);p->valid|=2u;}
            return p->ambient_colour;
        }
        if(low<p->ambient)low=p->ambient;
        if(high<p->ambient)high=p->ambient;
        uint32_t a=source_normal_palette_colour(p,low),b=source_normal_palette_colour(p,high);
        if(a==b)return a;
    }
    return source_normal_palette_colour(p,source_light_value(local,normal,diffuse,p->ambient,0));
#endif
}
static inline float source_light_cel_side(const float local[3],const float normal[3],float diffuse,float ambient)
{
#ifdef JGR_LIGHT_REFERENCE
    return source_light_value(local,normal,diffuse,ambient,1);
#else
    float error,value=source_light_estimate(local,normal,diffuse,ambient,&error);
    if(isfinite(value) && isfinite(error) && fabsf(value)>error)return value>0 ? 1.f:-1.f;
    return source_light_value(local,normal,diffuse,ambient,1)>0 ? 1.f:-1.f;
#endif
}
typedef struct {float u,v;uint32_t color;float x,y,z;} SourceLitVertex;
/* A Ninja strip normally shares one material color. Its two cel colors
   are constant for the strip, even when the light boundary moves. */
typedef struct {uint32_t material,color[2];unsigned valid;} SourceCelPalette;
static inline uint32_t source_cel_colour(uint32_t material,int side,const float rgb[2][3],const float *offset,SourceCelPalette *cache)
{
#ifndef JGR_LIGHT_REFERENCE
    if(cache){
        if(cache->material!=material){cache->material=material;cache->valid=0;}
        if(!(cache->valid&(1u<<side))){cache->color[side]=source_light_colour_offset(material,rgb[side],1.f,offset);cache->valid|=1u<<side;}
        return cache->color[side];
    }
#endif
    return source_light_colour_offset(material,rgb[side],1.f,offset);
}
/* Cel polygons are cut at signed intensity zero. Letting GU interpolate
   between the two vertex colours produces a gradient the original lacks.
   At most a triangle and a quad: nine expanded vertices per source tri. */
static inline unsigned source_cel_triangle_cached(const SourceLitVertex input[3],const float shade[3],SourceLitVertex out[9],const float rgb[2][3],const float *offset,SourceCelPalette *cache)
{
    unsigned count=0;
    int positive=(shade[0]>0)+(shade[1]>0)+(shade[2]>0);
    if(positive==0 || positive==3) {
        for(unsigned k=0;k<3;k++){out[k]=input[k];out[k].color=source_cel_colour(input[k].color,positive==3,rgb,offset,cache);}
        return 3;
    }
    for(int side=0;side<2;side++) {
        SourceLitVertex poly[4];unsigned n=0;
        for(unsigned i=0;i<3;i++) {
            unsigned j=(i+1)%3;int a=side ? shade[i]>0:shade[i]<=0,b=side ? shade[j]>0:shade[j]<=0;
            if(a)poly[n++]=input[i];
            if(a!=b) {
                float t=shade[i]/(shade[i]-shade[j]);SourceLitVertex v=input[i];
                v.u+=(input[j].u-v.u)*t;v.v+=(input[j].v-v.v)*t;
                v.x+=(input[j].x-v.x)*t;v.y+=(input[j].y-v.y)*t;v.z+=(input[j].z-v.z)*t;
                poly[n++]=v;
            }
        }
        for(unsigned j=1;j+1<n;j++) {
            unsigned indices[3]={0,j,j+1};
            for(unsigned k=0;k<3;k++){SourceLitVertex v=poly[indices[k]];v.color=source_cel_colour(v.color,side,rgb,offset,cache);out[count++]=v;}
        }
    }
    return count;
}
static inline unsigned source_cel_triangle_colours_offset(const SourceLitVertex input[3],const float shade[3],SourceLitVertex out[9],const float rgb[2][3],const float *offset)
{return source_cel_triangle_cached(input,shade,out,rgb,offset,0);}
static inline unsigned source_cel_triangle_colours(const SourceLitVertex input[3],const float shade[3],SourceLitVertex out[9],const float rgb[2][3])
{return source_cel_triangle_colours_offset(input,shade,out,rgb,0);}
#if !JGR_PORTABLE_ASSETS
/* Legacy host-reference convenience; production supplies its live palette. */
static inline unsigned source_cel_triangle(const SourceLitVertex input[3],const float shade[3],SourceLitVertex out[9])
{
    
#include "generated/source_data/source_light_h__rgb_0.inc"

    return source_cel_triangle_colours(input,shade,out,rgb);
}
#endif
#endif
