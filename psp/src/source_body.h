#ifndef JGR_SOURCE_BODY_H
#define JGR_SOURCE_BODY_H
#include "source_probe.h"

/* Ninja 2702e4: eight authored box vertices against a sphere. This is the
   actor/body-volume path (0b5f90), not the world-triangle capsule sweep.
   Keep the original face/adjacency tables, strict AABB gate and .025 normal
   comparisons. A conventional closest-point OBB test is NOT this routine. */
static inline float body_length(const float *v){return sqrtf(probe_ninja_dot(v,v));}
static inline int body_vector_zero(const float *v)
{return v[0]==0.f && v[1]==0.f && v[2]==0.f;}
/* The original zero-vector normalization produces three NaNs. Its later
   direction comparisons treat that vector as equal, because NaN > .025 is
   false. Carry that decision explicitly instead of executing 1/0 and 0*inf
   on Allegrex. A nonzero vector whose squared length underflows to zero
   instead has at least one infinite component: it differs from a finite
   unit normal. Return -1 for that case; ordinary vectors keep the exact math. */
static inline int body_normalize(float *v)
{
    if(body_vector_zero(v))return 0;
    float length=body_length(v);if(length==0.f)return -1;
    float inverse=1.f/length;for(int j=0;j<3;j++)v[j]*=inverse;
    return 1;
}
static inline int body_direction_equal(const float *a,const float *b)
{for(int j=0;j<3;j++)if(fabsf(a[j]-b[j])>.025f)return 0;return 1;}
static inline int source_body_box_sphere(const float box[24],const float sphere[4])
{
    static const unsigned char faces[6][4]={
        {0,1,2,3},{4,7,6,5},{0,3,7,4},{2,1,5,6},{0,4,5,1},{3,2,6,7}}; /* 2a4a98 */
    static const unsigned char adjacent[6][4]={
        {4,3,5,2},{2,5,3,4},{0,5,1,4},{0,4,1,5},{2,1,3,0},{0,3,1,2}}; /* 2a4af8 */
    for(int j=0;j<24;j++)if(!isfinite(box[j]))return 0;
    for(int j=0;j<4;j++)if(!isfinite(sphere[j]))return 0;
    if(sphere[3]<0)return 0;
    /* 27489c/274aa4 -> 2301ac. The latter compares summed interval lengths
       with their enclosing span, strictly, in single precision. */
    for(int j=0;j<3;j++) {
        float low=box[j],high=box[j];
        for(int v=1;v<8;v++){float x=box[v*3+j];if(x<low)low=x;if(x>high)high=x;}
        float sl=sphere[j]-sphere[3],sh=sphere[j]+sphere[3];
        float outer=(high>sh ? high:sh)-(low<sl ? low:sl);
        float widths=((high-low)+sh)-sl;
        if(!(widths>outer))return 0;
    }
    float normals[6][3];
    for(int f=0;f<6;f++) {
        const float *a=box+faces[f][0]*3,*b=box+faces[f][1]*3,*c=box+faces[f][2]*3;
        float u[3],v[3];for(int j=0;j<3;j++){u[j]=b[j]-a[j];v[j]=c[j]-a[j];}
        for(int j=0;j<3;j++){int x=(j+1)%3,y=(j+2)%3;normals[f][j]=source_fmac(v[x],u[y],-(u[x]*v[y]));}
        /* A degenerate face projects to NaNs and unconditionally accepts
           after the strict AABB gate. Preserve that authored behavior. */
        if(body_normalize(normals[f])<=0)return 1;
    }
    int inside=1;
    for(int f=0;f<6;f++) {
        const float *n=normals[f],*a=box+faces[f][0]*3;float projected[3],d[3];
        float offset=(-(n[0]*a[0])-n[1]*a[1])-n[2]*a[2];
        float t=-(probe_ninja_dot(n,sphere)+offset)/probe_ninja_dot(n,n);
        for(int j=0;j<3;j++){projected[j]=source_fmac(n[j],t,sphere[j]);d[j]=projected[j]-sphere[j];}
        float distance=body_length(d),squared=distance*distance;int face=1;
        for(int e=0;e<4;e++) {
            const float *p=box+faces[f][e]*3,*q=box+faces[f][(e+1)%4]*3;
            float edge[3],rel[3],closest[3];
            for(int j=0;j<3;j++){edge[j]=p[j]-q[j];rel[j]=projected[j]-p[j];}
            float edge_squared=probe_ninja_dot(edge,edge);
            /* A nonzero edge can also square to zero. Its old infinite/
               NaN projection has the same distance-only face decision. */
            if(edge_squared==0.f){if(distance>sphere[3])face=0;continue;}
            float along=probe_ninja_dot(edge,rel)/edge_squared;
            for(int j=0;j<3;j++){closest[j]=source_fmac(edge[j],along,p[j]);rel[j]=closest[j]-projected[j];}
            int normalized=body_normalize(rel);
            if((normalized==0 || (normalized>0 && body_direction_equal(normals[adjacent[f][e]],rel))) && !(distance>sphere[3]))continue;
            if(distance>sphere[3]){face=0;continue;}
            for(int j=0;j<3;j++)rel[j]=projected[j]-closest[j];
            if(probe_ninja_dot(rel,rel)>sphere[3]*sphere[3]-squared)face=0;
        }
        if(face)return 1;
        int normalized=body_normalize(d);
        if(normalized<0 || (normalized>0 && !body_direction_equal(n,d)))inside=0;
    }
    return inside;
}

/* Reuse only the query-independent terms of the unchanged routine above.
   One stack instance can serve all player spheres for an immutable prop.
   Initialization does not touch vertices. Bounds are prepared on first use;
   normals/edges stay lazy until a sphere passes the original strict gate. */
typedef struct {
    const float *vertices;
    float low[3],high[3],normals[6][3],offset[6],nn[6];
    float edge[6][4][3],edge_nn[6][4];
    unsigned state,face_mask; /* 0 unexamined, 1 invalid, 2 bounds, 3 normals, 4 degenerate face */
} SourceBodyBoxPrepared;
static inline void source_body_box_prepare(SourceBodyBoxPrepared *p,const float box[24])
{p->vertices=box;p->state=p->face_mask=0;}
static inline int source_body_box_sphere_prepared(SourceBodyBoxPrepared *p,const float sphere[4])
{
    static const unsigned char faces[6][4]={
        {0,1,2,3},{4,7,6,5},{0,3,7,4},{2,1,5,6},{0,4,5,1},{3,2,6,7}};
    static const unsigned char adjacent[6][4]={
        {4,3,5,2},{2,5,3,4},{0,5,1,4},{0,4,1,5},{2,1,3,0},{0,3,1,2}};
    const float *box=p->vertices;
    if(!p->state){
        p->state=1;
        for(int j=0;j<24;j++)if(!isfinite(box[j]))return 0;
        for(int j=0;j<3;j++){
            p->low[j]=p->high[j]=box[j];
            for(int v=1;v<8;v++){
                float x=box[v*3+j];if(x<p->low[j])p->low[j]=x;if(x>p->high[j])p->high[j]=x;
            }
        }
        p->state=2;
    }
    if(p->state==1)return 0;
    for(int j=0;j<4;j++)if(!isfinite(sphere[j]))return 0;
    if(sphere[3]<0)return 0;
    for(int j=0;j<3;j++){
        float low=p->low[j],high=p->high[j],sl=sphere[j]-sphere[3],sh=sphere[j]+sphere[3];
        float outer=(high>sh ? high:sh)-(low<sl ? low:sl);
        float widths=((high-low)+sh)-sl;
        if(!(widths>outer))return 0;
    }
    if(p->state==4)return 1; /* still requires each sphere's strict AABB gate */
    if(p->state==2){
        for(int f=0;f<6;f++){
            const float *a=box+faces[f][0]*3,*b=box+faces[f][1]*3,*c=box+faces[f][2]*3;
            float u[3],v[3],*n=p->normals[f];
            for(int j=0;j<3;j++){u[j]=b[j]-a[j];v[j]=c[j]-a[j];}
            for(int j=0;j<3;j++){int x=(j+1)%3,y=(j+2)%3;n[j]=source_fmac(v[x],u[y],-(u[x]*v[y]));}
            if(body_normalize(n)<=0){p->state=4;return 1;}
        }
        p->state=3;
    }
    int inside=1;
    for(int f=0;f<6;f++){
        const float *n=p->normals[f];float projected[3],d[3];
        /* A first-face hit must not prepare the remaining five faces.
           Each visited face always tests four edges in the reference. */
        if(!(p->face_mask&(1u<<f))){
            const float *a=box+faces[f][0]*3;
            p->offset[f]=(-(n[0]*a[0])-n[1]*a[1])-n[2]*a[2];p->nn[f]=probe_ninja_dot(n,n);
            for(int e=0;e<4;e++){
                const float *a=box+faces[f][e]*3,*b=box+faces[f][(e+1)%4]*3;
                float *edge=p->edge[f][e];
                for(int j=0;j<3;j++)edge[j]=a[j]-b[j];
                p->edge_nn[f][e]=probe_ninja_dot(edge,edge);
            }
            p->face_mask|=1u<<f;
        }
        float t=-(probe_ninja_dot(n,sphere)+p->offset[f])/p->nn[f];
        for(int j=0;j<3;j++){projected[j]=source_fmac(n[j],t,sphere[j]);d[j]=projected[j]-sphere[j];}
        float distance=body_length(d),squared=distance*distance;int face=1;
        for(int e=0;e<4;e++){
            const float *a=box+faces[f][e]*3,*edge=p->edge[f][e];float rel[3],closest[3];
            for(int j=0;j<3;j++)rel[j]=projected[j]-a[j];
            if(p->edge_nn[f][e]==0.f){if(distance>sphere[3])face=0;continue;}
            float along=probe_ninja_dot(edge,rel)/p->edge_nn[f][e];
            for(int j=0;j<3;j++){closest[j]=source_fmac(edge[j],along,a[j]);rel[j]=closest[j]-projected[j];}
            int normalized=body_normalize(rel);
            if((normalized==0 || (normalized>0 && body_direction_equal(p->normals[adjacent[f][e]],rel))) && !(distance>sphere[3]))continue;
            if(distance>sphere[3]){face=0;continue;}
            for(int j=0;j<3;j++)rel[j]=projected[j]-closest[j];
            if(probe_ninja_dot(rel,rel)>sphere[3]*sphere[3]-squared)face=0;
        }
        if(face)return 1;
        int normalized=body_normalize(d);
        if(normalized<0 || (normalized>0 && !body_direction_equal(n,d)))inside=0;
    }
    return inside;
}

/* 0b6218's per-recipient payload. Other is the contact partner; reversing
   recipient swaps all paired fields, including the two attribute words.
   Distance sorts the outer 0b5e7c contact list, not a penetration depth. */
typedef struct {
    float direction[3],other_direction[3],speed,other_speed;
    float center[3],other_center[3],previous[3],other_previous[3];
    uint32_t flags,other_flags;
} SourceActorContact;
static inline float source_actor_contact(SourceActorContact *out,
    const float current[3],const float previous[3],unsigned flags,
    const float other[3],const float other_previous[3],unsigned other_flags)
{
    float distance[3];
    for(int j=0;j<3;j++){out->direction[j]=current[j]-previous[j];out->other_direction[j]=other[j]-other_previous[j];distance[j]=current[j]-other[j];}
    /* 2809b8 uses FIPR/FSRRA and returns the original vector length. */
    out->speed=0;out->other_speed=0;
    float *vectors[2]={out->direction,out->other_direction};float *speeds[2]={&out->speed,&out->other_speed};
    for(int i=0;i<2;i++) {
        float *v=vectors[i];if(v[0]==0 && v[1]==0 && v[2]==0)continue;
        float squared=probe_fipr(v,v);
        /* Subnormal motion may square/flush to zero despite nonzero
           components. Treat that unmeasurable displacement as stationary. */
        if(squared<=0.f){memset(v,0,3*sizeof *v);continue;}
        float inverse=1.f/sqrtf(squared);
        *speeds[i]=squared*inverse;for(int j=0;j<3;j++)v[j]*=inverse;
    }
    memcpy(out->center,current,12);memcpy(out->other_center,other,12);
    memcpy(out->previous,previous,12);memcpy(out->other_previous,other_previous,12);
    out->flags=flags;out->other_flags=other_flags;
    return sqrtf(probe_fipr(distance,distance));
}
#endif
