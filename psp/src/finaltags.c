#include "finaltags.h"
#include "source_transform.h"
#include <math.h>
#include <string.h>
static unsigned word(const unsigned char *p){unsigned v;memcpy(&v,p,4);return v;}
static int finite_words(const unsigned char *p,unsigned n)
{ for(unsigned j=0;j<n;j++){float f;memcpy(&f,p+j*4,4);if(!isfinite(f))return 0;}return 1; }
static unsigned tag_points(const FinalTags *tags,unsigned i,float points[34][3],unsigned *directions)
{
    const unsigned char *d=tags->objects[i].tpl->descriptor;
    memcpy(points[0],d+0x24,12);memcpy(points[1],d+0x30,12);
    memcpy(points[2],d+0x54,72);memcpy(points[8],d+0x9c,72);
    unsigned count=14;*directions=(0x3fu<<8)|2u;
    if(tags->nvolumes[i]){memcpy(points[14],tags->subvolumes[i],13*12);count+=13;*directions|=0x3fu<<20;}
    for(unsigned k=0;k<tags->nanchors[i];k++)memcpy(points[count++],tags->anchors[i][k].position,12);
    return count;
}
int finaltags_init(FinalTags *out,const void *image,unsigned size)
{
    const unsigned char *b=image;FinalTags tags={0};
    static const unsigned source[5]={0x8ccfa970,0x8ccfb3ac,0x8ccfbde8,0x8ccfc824,0x8ccfce4c};
    if(!b || ((uintptr_t)b&3) || size<8 || memcmp(b,"FGT1",4) || word(b+4)!=5)return -1;
    unsigned at=8;
    for(unsigned i=0;i<5;i++) {
        if(size-at<16+sizeof(GfoTemplate))return -1;
        unsigned bytes=word(b+at),style=word(b+at+4),na=word(b+at+8),nv=word(b+at+12);
        const GfoTemplate *t=(const GfoTemplate *)(b+at+16);
        if(bytes>size-at || t->bank!=4 || t->chain || t->index!=i || t->source!=source[i] ||
           t->kind!=(i<4 ? 3u:1u) || na!=(i<4 ? 7u:0u) || nv!=(i<4 ? 1u:0u) ||
           style!=(i<4 ? 0x3800u:0u) || !t->nbones || t->nbones>64 ||
           !t->nvertex || t->nvertex%3 || t->nvertex>4096)return -1;
        unsigned expected=16+sizeof *t+t->nbones*sizeof(JgcBone)+t->nvertex*sizeof(GfoVertex)+t->nvertex*4+na*32+nv*176;
        if(bytes!=expected)return -1;
        if(!finite_words(t->descriptor,3) || !finite_words(t->descriptor+24,51))return -1;
        const JgcBone *bones=gfo_bones(t);
        for(unsigned j=0;j<t->nbones;j++) {
            if(bones[j].parent < -1 || bones[j].parent >= (int)j || bones[j].batch0 || bones[j].nbatch ||
               !finite_words((const unsigned char *)bones[j].pos,3) || !finite_words((const unsigned char *)bones[j].scl,3))return -1;
        }
        const GfoVertex *v=gfo_vertices(t);
        for(unsigned j=0;j<t->nvertex;j++)if(v[j].bone>=t->nbones || !isfinite(v[j].u) || !isfinite(v[j].v) || !finite_words((const unsigned char *)v[j].pos,3))return -1;
        const uint32_t *materials=(const uint32_t *)(v+t->nvertex);
        for(unsigned j=0;j<t->nvertex/3;j++)if(materials[j*3]>1)return -1;
        const SprayAnchor *anchors=(const SprayAnchor *)(materials+t->nvertex);
        for(unsigned j=0;j<na;j++)if(!finite_words((const unsigned char *)anchors[j].position,3))return -1;
        const unsigned char *volumes=(const unsigned char *)(anchors+na);
        for(unsigned j=0;j<nv;j++)if(!finite_words(volumes+j*176,42) || !finite_words(volumes+j*176+172,1))return -1;
        tags.objects[i].tpl=t;tags.objects[i].handle=i+1;tags.objects[i].parent=i+1;
        tags.materials[i]=materials;tags.anchors[i]=anchors;tags.subvolumes[i]=volumes;
        tags.style[i]=style;tags.nanchors[i]=na;tags.nvolumes[i]=nv;at+=bytes;
    }
    if(at!=size)return -1;
    for(unsigned i=0;i<5;i++) {
        float points[34][3];unsigned directions,count=tag_points(&tags,i,points,&directions),unique=0;
        for(unsigned p=0;p<count;p++) {
            unsigned other=0;
            for(;other<p;other++)if((other<32 ? (directions>>other)&1u:0u)==(p<32 ? (directions>>p)&1u:0u) &&
                !memcmp(points[other],points[p],12))break;
            tags.point_map[i][p]=other<p ? tags.point_map[i][other]:(unsigned char)unique++;
        }
        tags.point_count[i]=(unsigned char)unique;
    }
    *out=tags;return 0;
}
void finaltags_anchor(const FinalTags *tags,unsigned i,unsigned stroke,SprayAnchor *out)
{
    if(i>=5 || stroke>=tags->nanchors[i])return;
    *out=tags->anchors[i][stroke];const float *m=tags->objects[i].matrix,*v=tags->anchors[i][stroke].position;
    for(int j=0;j<3;j++) {
        double sum=(double)m[j]*v[0];sum+=(double)m[4+j]*v[1];sum+=(double)m[8+j]*v[2];sum+=m[12+j];out->position[j]=(float)sum;
    }
}
void finaltags_subvolume(const FinalTags *tags,unsigned i,unsigned volume,unsigned char out[176])
{
    if(i>=5 || volume>=tags->nvolumes[i])return;
    const unsigned char *raw=tags->subvolumes[i]+volume*176;const float *m=tags->objects[i].matrix;
    memset(out,0,176);memcpy(out+168,raw+168,8);
    float points[13][3],result[13][3];memcpy(points,raw,sizeof points);
    source_column_points(m,points,result,0x3fu<<6,13);memcpy(out,result,sizeof result);
}
void finaltags_pose(FinalTags *tags,unsigned i,const float matrix[16],Spot *s)
{
    if(i>=5)return;
    GfoObject *o=tags->objects+i;
    const unsigned char *map=tags->point_map[i];
    if(!(tags->pose_valid&(1u<<i)) || memcmp(matrix,tags->pose_matrix[i],64)) {
        float points[34][3];unsigned directions,count=tag_points(tags,i,points,&directions),unique_directions=0;
        for(unsigned p=0;p<count;p++) {
            /* First-reference numbering makes this forward compaction safe;
               later input points cannot be overwritten by an earlier alias. */
            if(map[p]!=p)memcpy(points[map[p]],points[p],12);
            if(p<32 && (directions&(1u<<p)))unique_directions|=1u<<map[p];
        }
        source_column_points(matrix,points,tags->pose_points[i],unique_directions,tags->point_count[i]);
        memcpy(tags->pose_matrix[i],matrix,64);tags->pose_valid|=1u<<i;
        for(unsigned k=0;k<16;k++)if(!isfinite(matrix[k]))tags->pose_valid&=~(1u<<i);
    }
    const float (*result)[3]=tags->pose_points[i];
    memcpy(o->matrix,matrix,64);memcpy(o->center,result[map[0]],12);memcpy(o->normal,result[map[1]],12);
    for(unsigned p=0;p<12;p++)memcpy(o->volume+p*3,result[map[p+2]],12);
    s->source_data=1;s->hasvol=1;s->size=o->tpl->kind;s->nstrokes=tags->nanchors[i];s->style_mask=tags->style[i];
    s->x=o->center[0];s->y=o->center[1];s->z=o->center[2];
    memcpy(s->pts,o->volume,72);memcpy(s->nrm,o->volume+18,72);
    memcpy(s->facing,o->normal,12);
    s->camera_valid=tags->nvolumes[i]!=0;
    if(s->camera_valid) {
        memset(s->camera_volume,0,176);memcpy(s->camera_volume+168,tags->subvolumes[i]+168,8);
        for(unsigned p=0;p<13;p++)memcpy(s->camera_volume+p*12,result[map[p+14]],12);
    }
    if(s->nstrokes) {
        unsigned first=14+(s->camera_valid ? 13:0);
        for(unsigned k=0;k<s->nstrokes;k++) {
            s->anchors[k]=tags->anchors[i][k];memcpy(s->anchors[k].position,result[map[first+k]],12);
        }
    }
}
void finaltags_progress(FinalTags *tags,unsigned i,const Spot *s)
{
    if(i>=5)return;
    tags->objects[i].progress=(GfoProgress){s->stroke,s->reveal,s->complete,s->started,0};
}
int finaltags_camera_reuse(const FinalTags *tags,unsigned i,const float root[16],float out[16])
{
    if(i>=5 || !(tags->camera_valid&(1u<<i)) || memcmp(root,tags->camera_root[i],64))return 0;
    memcpy(out,tags->camera_result[i],64);return 1;
}
void finaltags_camera_store(FinalTags *tags,unsigned i,const float root[16],const float result[16])
{
    if(i>=5)return;
    tags->camera_valid&=~(1u<<i);
    for(unsigned k=0;k<16;k++)if(!isfinite(root[k]) || !isfinite(result[k]))return;
    memcpy(tags->camera_root[i],root,64);memcpy(tags->camera_result[i],result,64);tags->camera_valid|=1u<<i;
}
