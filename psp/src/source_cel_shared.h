#ifndef JGR_SOURCE_CEL_SHARED_H
#define JGR_SOURCE_CEL_SHARED_H
#include "source_light.h"
#include "source_cel_vfpu.h"

/* Retained reference for unusual masks and the independent emitter tests. */
static inline unsigned source_cel_shared_reference(const SourceLitVertex tri[3],const float shade[3],
    const unsigned short original[3],SourceLitVertex boundary[4],unsigned short ordered[9],
    unsigned first,const float rgb[2][3],const float *offset,SourceCelPalette *colors)
{
    unsigned short edge[3][2];unsigned count=0;
    for(unsigned i=0;i<3;i++) {
        unsigned j=(i+1)%3;if((shade[i]>0)==(shade[j]>0))continue;
        float t=shade[i]/(shade[i]-shade[j]);SourceLitVertex v=tri[i];
        v.u+=(tri[j].u-v.u)*t;v.v+=(tri[j].v-v.v)*t;
        v.x+=(tri[j].x-v.x)*t;v.y+=(tri[j].y-v.y)*t;v.z+=(tri[j].z-v.z)*t;
        for(unsigned side=0;side<2;side++) {
            edge[i][side]=(unsigned short)(first+count);boundary[count]=v;
            boundary[count++].color=source_cel_colour(v.color,side,rgb,offset,colors);
        }
    }
    unsigned used=0;
    for(unsigned side=0;side<2;side++) {
        unsigned short polygon[4];unsigned n=0;
        for(unsigned i=0;i<3;i++) {
            unsigned j=(i+1)%3;
            int a=side ? shade[i]>0:shade[i]<=0,b=side ? shade[j]>0:shade[j]<=0;
            if(a)polygon[n++]=original[i];
            if(a!=b)polygon[n++]=edge[i][side];
        }
        for(unsigned j=1;j+1<n;j++) {
            ordered[used++]=polygon[0];ordered[used++]=polygon[j];ordered[used++]=polygon[j+1];
        }
    }
    return count;
}
static inline void source_cel_shared_edge(const SourceLitVertex *a,const SourceLitVertex *b,
    float sa,float sb,SourceLitVertex out[2],const float rgb[2][3],const float *offset,SourceCelPalette *colors)
{
    float t=sa/(sa-sb),u=a->u,v=a->v,x=a->x,y=a->y,z=a->z;
    u+=(b->u-u)*t;v+=(b->v-v)*t;x+=(b->x-x)*t;y+=(b->y-y)*t;z+=(b->z-z)*t;
    /* Explicit aligned fields avoid generic structure-copy calls into the
       frame-owned GU arena. Preserve each directed edge's material. */
    out[0].u=out[1].u=u;out[0].v=out[1].v=v;
    out[0].x=out[1].x=x;out[0].y=out[1].y=y;out[0].z=out[1].z=z;
    out[0].color=source_cel_colour(a->color,0,rgb,offset,colors);
    out[1].color=source_cel_colour(a->color,1,rgb,offset,colors);
}
static inline unsigned source_cel_shared_triangle(const SourceLitVertex tri[3],const float shade[3],
    const unsigned short original[3],SourceLitVertex boundary[4],unsigned short ordered[9],
    unsigned first,const float rgb[2][3],const float *offset,SourceCelPalette *colors)
{
    unsigned mask=(shade[0]>0)|((shade[1]>0)<<1)|((shade[2]>0)<<2);
#ifdef JGR_CEL_EMIT_REFERENCE
    return source_cel_shared_reference(tri,shade,original,boundary,ordered,first,rgb,offset,colors);
#else
    if(!mask || mask==7)return source_cel_shared_reference(tri,shade,original,boundary,ordered,first,rgb,offset,colors);
    /* Six sign masks completely determine the two directed edges and both
       polygon fans. Edge order and side0-before-side1 match the reference. */
    unsigned e0=(mask==3 || mask==4) ? 1:0,e1=(mask==2 || mask==5) ? 1:2;
    unsigned j=(e0+1)%3;source_cel_shared_edge(tri+e0,tri+j,shade[e0],shade[j],boundary,rgb,offset,colors);
    j=(e1+1)%3;source_cel_shared_edge(tri+e1,tri+j,shade[e1],shade[j],boundary+2,rgb,offset,colors);
    unsigned short a=original[0],b=original[1],c=original[2];
    unsigned short e00=(unsigned short)first,e01=(unsigned short)(first+1),e10=(unsigned short)(first+2),e11=(unsigned short)(first+3);
#define CEL_ORDER(A,B,C,D,E,F,G,H,I) do { \
    ordered[0]=(A);ordered[1]=(B);ordered[2]=(C);ordered[3]=(D);ordered[4]=(E); \
    ordered[5]=(F);ordered[6]=(G);ordered[7]=(H);ordered[8]=(I); } while(0)
    switch(mask) {
    case 1:CEL_ORDER(e00,b,c,e00,c,e10,a,e01,e11);break;
    case 2:CEL_ORDER(a,e00,e10,a,e10,c,e01,b,e11);break;
    case 3:CEL_ORDER(e00,c,e10,a,b,e01,a,e01,e11);break;
    case 4:CEL_ORDER(a,b,e00,a,e00,e10,e01,c,e11);break;
    case 5:CEL_ORDER(e00,b,e10,a,e01,e11,a,e11,c);break;
    case 6:CEL_ORDER(a,e00,e10,e01,b,c,e01,c,e11);break;
    }
#undef CEL_ORDER
    return 4;
#endif
}
/* The indexed mixed packet already owns its source vertices and resolved
   intensities. Read its two directed crossing edges in place: gathering
   three complete temporary vertices is unnecessary. The caller supplies
   the two edge-start material colours, including the current alpha. */
static inline unsigned source_cel_shared_edge0(unsigned mask)
{return mask==3 || mask==4 ? 1u:0u;}
static inline unsigned source_cel_shared_edge1(unsigned mask)
{return mask==2 || mask==5 ? 1u:2u;}
static inline void source_cel_shared_indexed_edge(const SourceLitVertex *a,const SourceLitVertex *b,
    float sa,float sb,uint32_t material,SourceLitVertex out[2],const float rgb[2][3],const float *offset,SourceCelPalette *colors)
{
    float t=sa/(sa-sb),u=a->u,v=a->v,x=a->x,y=a->y,z=a->z;
    u+=(b->u-u)*t;v+=(b->v-v)*t;x+=(b->x-x)*t;y+=(b->y-y)*t;z+=(b->z-z)*t;
    out[0].u=out[1].u=u;out[0].v=out[1].v=v;
    out[0].x=out[1].x=x;out[0].y=out[1].y=y;out[0].z=out[1].z=z;
    out[0].color=source_cel_colour(material,0,rgb,offset,colors);
    out[1].color=source_cel_colour(material,1,rgb,offset,colors);
}
/* Preconditions match draw_cel_mixed's admission: finite resolved shades,
   a mixed mask (1..6), disjoint frame-owned output, and valid source IDs. */
static inline unsigned source_cel_shared_indexed_impl(const SourceLitVertex *vertices,const float *shade,
    const unsigned short original[3],unsigned mask,uint32_t material0,uint32_t material1,
    SourceLitVertex boundary[4],unsigned short ordered[9],unsigned first,
    const float rgb[2][3],const float *offset,SourceCelPalette *colors,int vector)
{
    unsigned e0=source_cel_shared_edge0(mask),e1=source_cel_shared_edge1(mask);
    unsigned i=original[e0],j=original[(e0+1)%3];
    if(vector)source_cel_vfpu_edge(vertices+i,vertices+j,shade[i],shade[j],material0,boundary,rgb,offset,colors);
    else source_cel_shared_indexed_edge(vertices+i,vertices+j,shade[i],shade[j],material0,boundary,rgb,offset,colors);
    i=original[e1];j=original[(e1+1)%3];
    if(vector)source_cel_vfpu_edge(vertices+i,vertices+j,shade[i],shade[j],material1,boundary+2,rgb,offset,colors);
    else source_cel_shared_indexed_edge(vertices+i,vertices+j,shade[i],shade[j],material1,boundary+2,rgb,offset,colors);
    unsigned short a=original[0],b=original[1],c=original[2];
    unsigned short e00=(unsigned short)first,e01=(unsigned short)(first+1),e10=(unsigned short)(first+2),e11=(unsigned short)(first+3);
#define CEL_INDEX_ORDER(A,B,C,D,E,F,G,H,I) do { \
    ordered[0]=(A);ordered[1]=(B);ordered[2]=(C);ordered[3]=(D);ordered[4]=(E); \
    ordered[5]=(F);ordered[6]=(G);ordered[7]=(H);ordered[8]=(I); } while(0)
    switch(mask) {
    case 1:CEL_INDEX_ORDER(e00,b,c,e00,c,e10,a,e01,e11);break;
    case 2:CEL_INDEX_ORDER(a,e00,e10,a,e10,c,e01,b,e11);break;
    case 3:CEL_INDEX_ORDER(e00,c,e10,a,b,e01,a,e01,e11);break;
    case 4:CEL_INDEX_ORDER(a,b,e00,a,e00,e10,e01,c,e11);break;
    case 5:CEL_INDEX_ORDER(e00,b,e10,a,e01,e11,a,e11,c);break;
    case 6:CEL_INDEX_ORDER(a,e00,e10,e01,b,c,e01,c,e11);break;
    }
#undef CEL_INDEX_ORDER
    return 4;
}
static inline unsigned source_cel_shared_indexed(const SourceLitVertex *vertices,const float *shade,
    const unsigned short original[3],unsigned mask,uint32_t material0,uint32_t material1,
    SourceLitVertex boundary[4],unsigned short ordered[9],unsigned first,
    const float rgb[2][3],const float *offset,SourceCelPalette *colors)
{
    return source_cel_shared_indexed_impl(vertices,shade,original,mask,material0,material1,
        boundary,ordered,first,rgb,offset,colors,0);
}
static inline unsigned source_cel_shared_indexed_vfpu(const SourceLitVertex *vertices,const float *shade,
    const unsigned short original[3],unsigned mask,uint32_t material0,uint32_t material1,
    SourceLitVertex boundary[4],unsigned short ordered[9],unsigned first,
    const float rgb[2][3],const float *offset,SourceCelPalette *colors)
{
    return source_cel_shared_indexed_impl(vertices,shade,original,mask,material0,material1,
        boundary,ordered,first,rgb,offset,colors,1);
}
#endif
