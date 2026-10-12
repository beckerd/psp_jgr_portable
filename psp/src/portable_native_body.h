/* Setup-only swept triangle contacts. The gameplay kernel remains separately
 * selected; exact source operation order matters to long controller replays.
 * No original program bytes, geometry or recordings are embedded here. */
#ifndef JGR_PORTABLE_NATIVE_BODY_H
#define JGR_PORTABLE_NATIVE_BODY_H
#include "collide.h"
#include "source_probe.h"
static inline void native_body_sub(float *out,const float *a,const float *b) { for (int i=0;i<3;i++) out[i]=a[i]-b[i]; }
static inline float native_body_dot(const float *a,const float *b) { return probe_fipr(a,b); }
static inline float native_body_distance(const float *a,const float *b) { float d[3];native_body_sub(d,a,b);return sqrtf(native_body_dot(d,d)); }
/* FUN_8c0b7420 rejects projections outside the segment; endpoint tests
   happen separately. Do not clamp the projection parameter. */
static inline int native_body_point_edge(const float *p,const float *a,const float *b,float radius,float *distance)
{
    float d[3],v[3],q[3],qa[3],qb[3];native_body_sub(d,b,a);native_body_sub(v,p,a);
    float dd=probe_ninja_dot(d,d);if(dd==0.f)return 0;
    float t=probe_ninja_dot(v,d)/dd;
    for(int k=0;k<3;k++)q[k]=source_fmac(t,d[k],a[k]);
    native_body_sub(v,p,q);float squared=probe_ninja_dot(v,v);
    *distance=squared<.025f ? 0.f:sqrtf(squared);
    if(*distance>radius)return 0;
    native_body_sub(qa,q,a);native_body_sub(qb,q,b);
    return probe_fipr(qa,qb)<=0.f;
}
/* FUN_8c0b7540: closest points on the two INFINITE lines, accepted only
   when BOTH lie within their segments. Endpoint/parallel cases are handled
   by the point/edge and point/point tests in the caller. */
static inline void native_body_cross(const float *a,const float *b,float *n)
{for(int k=0;k<3;k++){int j=(k+1)%3,l=(k+2)%3;n[k]=a[j]*b[l]-a[l]*b[j];}}
static inline float native_body_determinant(const float *a,const float *b,const float *c)
{
    float r=(a[1]*b[2])*c[0];
    r=source_fmac(c[2],a[0]*b[1],r);r=source_fmac(b[0],a[2]*c[1],r);
    r-=(a[2]*b[1])*c[0];r-=(a[1]*b[0])*c[2];r-=(a[0]*c[1])*b[2];return r;
}
static inline int native_body_edges(const float *a,const float *b,const float *c,const float *d,float radius,float *distance)
{
    float u[3],v[3],w[3],p[3],q[3],pa[3],pb[3];native_body_sub(u,b,a);native_body_sub(v,d,c);native_body_sub(w,a,c);
    float uu=probe_ninja_dot(u,u),uv=probe_ninja_dot(u,v),vv=probe_ninja_dot(v,v);
    if(uu==0.f || vv==0.f)return 0;
    if(fabsf(uv/(sqrtf(uu)*sqrtf(vv)))>.975f)memcpy(p,a,12);
    else {
        float n[3],m[3],axis[3],point[3];native_body_cross(u,v,n);
        float inverse=1.f/sqrtf(probe_ninja_dot(n,n));for(int k=0;k<3;k++)n[k]*=inverse;
        native_body_cross(n,v,m);inverse=1.f/sqrtf(probe_ninja_dot(m,m));for(int k=0;k<3;k++)m[k]*=inverse;
        native_body_cross(n,m,axis);
        float values[3]={probe_ninja_dot(n,a),probe_ninja_dot(m,c),probe_ninja_dot(axis,c)};
        float det=native_body_determinant(n,m,axis);if(det==0.f)return 0;inverse=1.f/det;
        for(int k=0;k<3;k++){float aa[3],bb[3],cc[3];memcpy(aa,n,12);memcpy(bb,m,12);memcpy(cc,axis,12);aa[k]=values[0];bb[k]=values[1];cc[k]=values[2];point[k]=native_body_determinant(aa,bb,cc)*inverse;}
        int i=0,j=1;float den=source_fmac(axis[i],u[j],-(u[i]*axis[j]));
        if(!(fabsf(den)>.025f)){i=1;j=2;den=source_fmac(axis[i],u[j],-(u[i]*axis[j]));}
        if(!(fabsf(den)>.025f)){i=0;j=2;den=source_fmac(axis[i],u[j],-(u[i]*axis[j]));}
        float t=source_fmac(axis[i],point[j]-a[j],-((point[i]-a[i])*axis[j]))/den;
        for(int k=0;k<3;k++)p[k]=source_fmac(t,u[k],a[k]);
    }
    native_body_sub(w,p,c);float t=probe_ninja_dot(v,w)/vv;
    for(int k=0;k<3;k++)q[k]=source_fmac(t,v[k],c[k]);
    native_body_sub(w,p,q);float squared=probe_ninja_dot(w,w);*distance=squared<.025f ? 0.f:sqrtf(squared);
    if(*distance>radius)return 0;
    native_body_sub(pa,p,a);native_body_sub(pb,p,b);if(probe_fipr(pa,pb)>0.f)return 0;
    native_body_sub(pa,q,c);native_body_sub(pb,q,d);return probe_fipr(pa,pb)<=0.f;
}
/* FUN_8c0b7758: the three oriented cross products must agree; zero cross
   products (point on an edge/vertex) are included. */
static inline int native_body_inside(const float *p,const float *v)
{
    float vec[3][3],cross[3][3];
    for(int i=0;i<3;i++)for(int k=0;k<3;k++)vec[i][k]=v[i*3+k]-p[k];
    for(int i=0;i<3;i++) {
        const float *a=vec[(i+1)%3],*b=vec[(i+2)%3];
        for(int k=0;k<3;k++){int j=(k+1)%3,l=(k+2)%3;cross[i][k]=source_fmac(-a[l],b[j],a[j]*b[l]);}
        if(probe_fipr(cross[i],cross[i])==0.f)return 1;
    }
    float xy=probe_fipr(cross[0],cross[1]),yz=probe_fipr(cross[1],cross[2]),zx=probe_fipr(cross[2],cross[0]);
    return (xy>=0.f && yz>=0.f && zx>=0.f) || (xy<=0.f && yz<=0.f && zx<=0.f);
}
typedef struct {
    float raw[3],unit[3],squared,offset;int valid;
} NativeBodyPlane;
static inline void native_body_plane(NativeBodyPlane *p,const float *v)
{
    /* 103848 builds (b-a) cross (c-b); 272518 projects against this raw
       absolute plane. Only the published contact normal is normalized. */
    probe_plane(v,v+3,v+6,p->raw);
    p->squared=probe_ninja_dot(p->raw,p->raw);p->valid=p->squared>0.f;
    if(!p->valid)return;
    float inverse=1.f/sqrtf(probe_fipr(p->raw,p->raw));
    for(int k=0;k<3;k++)p->unit[k]=p->raw[k]*inverse;
    p->offset=(-(p->raw[0]*v[0])-p->raw[1]*v[1])-p->raw[2]*v[2];
}
static inline int native_body_triangle(const ColTri *tri,const float *old,const float *a,const float *b,
                                       float radius,ColBodyHit *hit,const NativeBodyPlane *plane)
{
    NativeBodyPlane local;
    if(radius<0.f)return 0;
    if(!plane){native_body_plane(&local,tri->v);plane=&local;}
    if(!plane->valid)return 0;
    /* 0b54c0 chooses winding for the plane but passes the unchanged strip
       order into 0b56b8. Edge projection arithmetic is not bit-symmetric
       under endpoint reversal, even though its geometric line is the same. */
    float strip[9];const float *vertices=tri->v;
    if(tri->dbl&COL_PATCH_REVERSED){
        memcpy(strip,tri->v,12);memcpy(strip+3,tri->v+6,12);memcpy(strip+6,tri->v+3,12);vertices=strip;
    }
    const float *raw=plane->raw,*n=plane->unit;
    float nn=plane->squared,offset=plane->offset,v[3],reference[3];
    float old_dot=probe_ninja_dot(raw,old)+offset;
    float old_t=-old_dot/nn;
    for(int k=0;k<3;k++){reference[k]=source_fmac(old_t,raw[k],old[k]);v[k]=old[k]-reference[k];}
    if(probe_fipr(v,raw)<=0.f)return 0;
    float da=probe_ninja_dot(raw,a)+offset,db=probe_ninja_dot(raw,b)+offset,pa[3],pb[3];
    for(int k=0;k<3;k++){pa[k]=source_fmac(-da/nn,raw[k],a[k]);pb[k]=source_fmac(-db/nn,raw[k],b[k]);}
    da=copysignf(native_body_distance(a,pa),da);db=copysignf(native_body_distance(b,pb),db);
    float depth=0.f;int found=0;
    int stationary=!memcmp(a,b,12),crossing=0;float q[3],dir[3],qa[3],qb[3];
    native_body_sub(dir,b,a);float length=sqrtf(probe_ninja_dot(dir,dir)),nd=probe_ninja_dot(raw,dir);
    float ca=probe_ninja_dot(raw,a)+offset,cb=probe_ninja_dot(raw,b)+offset;
    float inverse=1.f/sqrtf(nn),distance_a=fabsf(ca)*inverse,distance_b=fabsf(cb)*inverse;
    if(native_body_distance(a,b)>1.f && length>0.f) {
        if(fabsf(nd/(length*sqrtf(nn)))<.025f){memcpy(q,pa,12);crossing=distance_a==0.f;}
        else {float t=-ca/nd;for(int k=0;k<3;k++)q[k]=source_fmac(t,dir[k],a[k]);crossing=1;}
        native_body_sub(qa,q,a);native_body_sub(qb,q,b);if(probe_fipr(qa,qb)>0.f)crossing=0;
    }
    if(crossing) {
        if(native_body_inside(q,vertices)) {
            native_body_sub(qa,a,q);native_body_sub(qb,b,q);
            float distance=probe_fipr(qa,raw)<=0.f ? distance_a:probe_fipr(qb,raw)<=0.f ? distance_b:fmaxf(distance_a,distance_b);
            depth=radius+distance;found=1;
        }
    } else if(distance_a>radius && distance_b>radius)return 0;
    /* A crossing face already has depth >= radius. Edge/vertex depths are
       radius minus a nonnegative distance, so none can increase that hit. */
    if(!found)for (int i=0;i<3;i++) {
        const float *v0=vertices+3*i,*v1=vertices+3*((i+1)%3);float d;
        /* An unchanged sweep has no line feature, and its two endpoint
           tests are identical. Keep every triangle/contact and its order. */
        if (!stationary) {
            if (native_body_edges(v0,v1,a,b,radius,&d)) { if (radius-d>depth) depth=radius-d;found=1; }
            if (native_body_point_edge(v0,a,b,radius,&d)) { if (radius-d>depth) depth=radius-d;found=1; }
        }
        for (int j=0;j<(stationary ? 1:2);j++) {
            const float *p=j ? b : a;
            if (native_body_point_edge(p,v0,v1,radius,&d)) { if (radius-d>depth) depth=radius-d;found=1; }
            d=native_body_distance(v0,p);
            if (d<radius) { if (radius-d>depth) depth=radius-d;found=1; }
        }
    }
    if (!crossing) for (int i=0;i<(stationary ? 1:2);i++) {
        float d=fabsf(i ? db : da);
        if (d<radius && native_body_inside(i ? pb : pa,vertices)) { if (radius-d>depth) depth=radius-d;found=1; }
    }
    if (!found) return 0;
    for (int i=0;i<3;i++) { hit->p[i]=reference[i];hit->n[i]=n[i]; }
    hit->depth=depth;hit->surf=tri->surf;hit->mask=tri->mask;return 1;
}
#endif
