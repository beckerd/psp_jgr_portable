#include "pathcoll.h"
#include <math.h>
#include <string.h>
extern float player_fsca_sin(int);
static float rf(const Scene *s,uint32_t a)
{ uint32_t u=scene_word(s,a);float f;memcpy(&f,&u,4);return f; }
int pathcoll_body_bound(const Scene *s,uint32_t node,uint32_t motion,float frame,float bounds[4])
{
    if(!s || !scene_bytes(s,node,68) || (scene_word(s,node)&65535)!=4 ||
       scene_word(s,node+60) || scene_word(s,node+64))return -1;
    unsigned vertices=scene_word(s,node+4);if(!scene_bytes(s,vertices,96))return -2;
    CharPose p={0};
    for(unsigned j=0;j<3;j++) {
        p.pos[j]=rf(s,node+12+j*4);p.ang[j]=(int32_t)scene_word(s,node+24+j*4);p.scl[j]=rf(s,node+36+j*4);
    }
    if(motion && !scene_sample_collision(s,motion,frame,0,&p))return -3;
    float radius=0;
    for(unsigned v=0;v<8;v++) {
        float reach=0;
        for(unsigned j=0;j<3;j++)reach+=fabsf(rf(s,vertices+v*12+j*4)*p.scl[j]);
        if(!isfinite(reach))return -4;
        if(reach>radius)radius=reach;
    }
    /* L1 encloses the Euclidean radius under every rotation. Generous
       rounding margin also encloses the source's FTRV evaluation. */
    for(int j=0;j<3;j++){if(!isfinite(p.pos[j]))return -4;bounds[j]=p.pos[j];}
    bounds[3]=radius*1.001f+.1f;return 0;
}
static void matrix(const CharPose *p,float m[9])
{
    float sx=player_fsca_sin(p->ang[0]),cx=player_fsca_sin((unsigned)p->ang[0]+16384u);
    float sy=player_fsca_sin(p->ang[1]),cy=player_fsca_sin((unsigned)p->ang[1]+16384u);
    float sz=player_fsca_sin(p->ang[2]),cz=player_fsca_sin((unsigned)p->ang[2]+16384u);
    float zys=cz*sy,zxs=sz*sy;
    float values[9]={cz*cy,(float)((double)zys*sx-(double)sz*cx),(float)((double)zys*cx+(double)sz*sx),
                    sz*cy,(float)((double)zxs*sx+(double)cz*cx),(float)((double)zxs*cx-(double)cz*sx),
                    -sy,cy*sx,cy*cx};
    for(int r=0;r<3;r++)for(int c=0;c<3;c++)m[r*3+c]=values[r*3+c]*p->scl[c];
}
static int sample_matrix(const Scene *s,uint32_t node,uint32_t motion,float frame,
                    float *vertices,unsigned capacity,CharPose *pose,float *basis)
{
    if(!s || !vertices || !scene_bytes(s,node,68))return -1;
    /* All supplied traffic descriptors have one node. Do not silently drop
       a future hierarchy or reinterpret body volumes as ground surfaces. */
    unsigned kind=scene_word(s,node)&65535,mesh=scene_word(s,node+4),nv,src;
    if(scene_word(s,node+60)||scene_word(s,node+64))return -2;
    if(kind==4){src=mesh;nv=8;}
    else if(kind==6 && scene_bytes(s,mesh,12)){src=scene_word(s,mesh);nv=scene_word(s,mesh+8)&65535;}
    else return -3;
    if(!nv || nv>capacity || !scene_bytes(s,src,nv*12u))return -4;
    CharPose p={0};
    for(unsigned j=0;j<3;j++) {
        p.pos[j]=rf(s,node+12+j*4);p.ang[j]=(int32_t)scene_word(s,node+24+j*4);p.scl[j]=rf(s,node+36+j*4);
        if(!isfinite(p.pos[j])||!isfinite(p.scl[j]))return -5;
    }
    if(motion && !scene_sample_collision(s,motion,frame,0,&p))return -6;
    /* Ninja Z-Y-X, then scale. Preserve source matrix products separately
       from the FTRV vertex dot products. */
    float m[9];matrix(&p,m);if(basis)memcpy(basis,m,sizeof m);
    for(unsigned v=0;v<nv;v++) {
        float xyz[3];for(int j=0;j<3;j++){xyz[j]=rf(s,src+v*12+j*4);if(!isfinite(xyz[j]))return -5;}
        for(int r=0;r<3;r++)vertices[v*3+r]=(float)((double)m[r*3]*xyz[0]+(double)m[r*3+1]*xyz[1]+(double)m[r*3+2]*xyz[2]+p.pos[r]);
    }
    if(pose)*pose=p;
    return (int)nv;
}

int pathcoll_sample(const Scene *s,uint32_t node,uint32_t motion,float frame,
                    float *vertices,unsigned capacity,CharPose *pose)
{return sample_matrix(s,node,motion,frame,vertices,capacity,pose,NULL);}

int pathcoll_body(const Scene *s,uint32_t node,uint32_t motion,float frame,
                  float vertices[24],float center[3],float bounds[4])
{
    if(!s || (scene_word(s,node)&65535)!=4)return -1;
    CharPose p;float m[9],local[3];
    int n=sample_matrix(s,node,motion,frame,vertices,8,&p,m);if(n!=8)return -2;
    for(int j=0;j<3;j++){local[j]=rf(s,node+48+j*4);if(!isfinite(local[j]))return -3;}
    /* 0be0e4: the contact center is authored node+48 transformed by FTRV,
       NOT the derived broadphase-sphere center at record+32. */
    for(int r=0;r<3;r++)center[r]=(float)((double)m[r*3]*local[0]+(double)m[r*3+1]*local[1]+(double)m[r*3+2]*local[2]+p.pos[r]);
    /* 0c820c: eight individually rounded additions, mean, max distance. */
    for(int j=0;j<3;j++){float sum=0;for(int v=0;v<8;v++)sum+=vertices[v*3+j];bounds[j]=sum*.125f;}
    bounds[3]=0;
    for(int v=0;v<8;v++) {
        float d[3];for(int j=0;j<3;j++)d[j]=bounds[j]-vertices[v*3+j];
        double squared=(double)d[0]*d[0];squared+=(double)d[1]*d[1];squared+=(double)d[2]*d[2];squared+=0.;
        float length=sqrtf((float)squared);if(length>bounds[3])bounds[3]=length;
    }
    return 0;
}

int pathcoll_surfaces(const Scene *s,uint32_t node,const float *vertices,unsigned nvertices,
                      ColTri *out,unsigned capacity)
{
    if(!s || !vertices || !scene_bytes(s,node,68))return -1;
    if((scene_word(s,node)&65535)!=6 || scene_word(s,node+60) || scene_word(s,node+64))return -2;
    uint32_t mesh=scene_word(s,node+4);if(!scene_bytes(s,mesh,12))return -3;
    uint32_t patches=scene_word(s,mesh+4),counts=scene_word(s,mesh+8);
    unsigned nv=counts&65535,np=counts>>16,total=0;
    if(!nv || nvertices!=nv || !scene_bytes(s,patches,(size_t)np*36))return -3;
    for(unsigned i=0;i<nv*3;i++)if(!isfinite(vertices[i]))return -4;
    /* Validate all records before publishing anything into the caller's
       current/previous caches. A bad later strip must not leave a half pose. */
    for(unsigned j=0;j<np;j++) {
        uint32_t patch=patches+j*36,indices=scene_word(s,patch+24),flags=scene_word(s,patch+28);
        unsigned n=flags&65535;
        if(n>COL_SURFACE_TRIANGLES-total || !scene_bytes(s,indices,(n+2u)*2))return -5;
        const unsigned char *raw=scene_bytes(s,indices,(n+2u)*2);
        for(unsigned k=0;k<n+2;k++){int16_t index;memcpy(&index,raw+k*2,2);if(index<0 || (unsigned)index>=nv)return -5;}
        total+=n;
    }
    if(!out)return (int)total;
    if(total>capacity)return -6;
    unsigned written=0;
    for(unsigned j=0;j<np;j++) {
        uint32_t patch=patches+j*36,flags=scene_word(s,patch+28),kinds=scene_word(s,patch+32);
        const unsigned char *raw=scene_bytes(s,scene_word(s,patch+24),((flags&65535)+2u)*2);
        unsigned n=flags&65535,wind=!!(flags&0x40000000u);
        for(unsigned k=0;k<n;k++,wind^=1) {
            uint16_t ids[3];memcpy(ids,raw+k*2,6);ColTri *t=out+written++;
            memcpy(t->v,vertices+ids[0]*3,12);
            memcpy(t->v+3,vertices+ids[wind ? 1:2]*3,12);
            memcpy(t->v+6,vertices+ids[wind ? 2:1]*3,12);
            t->surf=kinds>>16;t->mask=kinds&65535;
            t->dbl=(flags&0x80000000u ? COL_PATCH_BIT31:0)|(wind ? 0:COL_PATCH_REVERSED);t->src=0;
        }
    }
    return (int)written;
}
