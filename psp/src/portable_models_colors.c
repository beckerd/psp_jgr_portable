/* First-launch source PLAY palette matching; no stored colors or geometry. */
#include "portable_models_colors.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define MAX_FACE_POINTS 65536u
#define MAX_BONES 4096u

typedef struct {double p[3];unsigned char color[3];} FacePoint;
typedef struct {
 JgrDisc *disc;JgrJgcCharacter reference;JgrModelCache *cache;
 FacePoint *points;unsigned count,capacity,ordinal;
 double head[16];unsigned head_found;
} Reference;
static unsigned word(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static unsigned half(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static double real(const unsigned char *p){unsigned v=word(p);float f;memcpy(&f,&v,4);return f;}
static int read_at(FILE *f,unsigned at,void *p,unsigned n)
{return fseek(f,at,SEEK_SET)||fread(p,1,n,f)!=n?-1:0;}
static void transform(double out[3],const double m[16],const double p[3],int normal)
{
 double q[3];for(unsigned i=0;i<3;i++)q[i]=m[i*4]*p[0]+m[i*4+1]*p[1]+m[i*4+2]*p[2]+(normal?0:m[i*4+3]);
 if(normal){double d=sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]);if(!d)d=1;for(unsigned i=0;i<3;i++)q[i]/=d;}
 memcpy(out,q,sizeof q);
}
static int collect(const JgrModelBatch *b,void *opaque)
{
 Reference *r=opaque;if(r->ordinal!=11 || b->flags&8)return 0;
 if(b->stride!=28 || b->vertices>MAX_FACE_POINTS-r->count)return -1;
 if(r->count+b->vertices>r->capacity){unsigned n=r->capacity?r->capacity:256;while(n<r->count+b->vertices)n*=2;if(n>MAX_FACE_POINTS)n=MAX_FACE_POINTS;FacePoint *p=realloc(r->points,n*sizeof *p);if(!p)return -1;r->points=p;r->capacity=n;}
 for(unsigned i=0;i<b->vertices;i++){
  const unsigned char *v=b->vertex_bytes+i*28;FacePoint *p=r->points+r->count++;
  for(unsigned k=0;k<3;k++)p->p[k]=real(v+12+k*4);
  memcpy(p->color,v+8,3);
 }return 0;
}
static int reference_node(const JgrModelNode *n,const double matrix[16],unsigned ordinal,int parent,void *opaque)
{
 (void)parent;Reference *r=opaque;JgrModelOptions options=r->reference.model;r->ordinal=ordinal;
 if(ordinal==11){memcpy(r->head,matrix,sizeof r->head);r->head_found=1;}
 options.owner=(int)ordinal;options.cache_vertices=1;memcpy(options.bind,matrix,sizeof options.bind);
 if(jgr_model_inverse(options.inverse_bind,matrix))return -1;
 return n->model?jgr_model_mesh(r->cache,r->disc,&r->reference.source,0,n->model,&options,collect,r):0;
}
static int local_matrices(FILE *bones,unsigned n,double (*matrices)[16])
{
 for(unsigned i=0;i<n;i++){
  unsigned char row[48];JgrModelNode node;double local[16];memset(&node,0,sizeof node);
  if(read_at(bones,i*48,row,48))return -1;
  unsigned parent=half(row);node.eval=half(row+2);
  for(unsigned k=0;k<3;k++){node.position[k]=real(row+4+k*4);node.angle[k]=(int32_t)word(row+16+k*4);node.scale[k]=real(row+28+k*4);}
  jgr_model_node_matrix(local,&node,0);
  if(parent==65535)memcpy(matrices[i],local,sizeof local);
  else{if(parent>=i)return -1;jgr_model_multiply(matrices[i],matrices[parent],local);}
 }return 0;
}
int jgr_models_face_colors(JgrDisc *disc,const JgrJgcCharacter *source,const JgrDiscFile *textures,FILE *sections[10],FILE *lighting,unsigned mode)
{
 Reference r;JgrModelLight light;uint16_t mapping[512];double identity[16],(*matrices)[16]=NULL;
 unsigned char character[48],bone[48],batch[16],vertex[28],lr[20];int result=-1;unsigned matched=0,total=0;
 if(!disc || !source || !textures || !sections || mode<1 || mode>3 || (mode==3&&!lighting))return -1;
 memset(&r,0,sizeof r);r.disc=disc;r.reference=*source;
 if(jgr_model_baked_light(disc,&light) || jgr_jgc_resolve_play(disc,&source->source,textures,0,0,mapping,&r.reference))goto done;
 r.reference.model.palette_mix=1;r.reference.model.baked_light=&light;r.reference.model.toon=0;r.reference.model.light=1;
 r.reference.model.source_material=r.reference.model.source_environment=r.reference.model.source_lighting=r.reference.model.retain_lighting=0;
 r.cache=jgr_model_cache_create();if(!r.cache)goto done;jgr_model_identity(identity);
 if(jgr_model_walk(disc,&r.reference.source,0,r.reference.root,identity,0,1,reference_node,&r) || !r.count || !r.head_found)goto done;
 jgr_model_cache_free(r.cache);r.cache=NULL;
 for(unsigned i=0;i<4;i++)if(fflush(sections[i]))goto done;
 if(lighting&&fflush(lighting))goto done;
 if(read_at(sections[0],0,character,48) || word(character) || !word(character+4) || word(character+4)>MAX_BONES)goto done;
 unsigned bones=word(character+4);if(mode==1 && bones<=11)goto done;
 if(mode==3){matrices=malloc((size_t)bones*sizeof *matrices);if(!matrices || local_matrices(sections[1],bones,matrices))goto done;}
 for(unsigned j=0;j<bones;j++){
  if(mode==1 && j!=11)continue;
  if(read_at(sections[1],j*48,bone,48))goto done;
  unsigned first=word(bone+40),count=word(bone+44);
  if(first>65535 || count>65535-first)goto done;
  for(unsigned k=first;k<first+count;k++){
   if(read_at(sections[2],k*16,batch,16))goto done;
   if(half(batch+14)&8)continue;
   unsigned vo=word(batch),nv=half(batch+8);if(vo>UINT32_MAX/28 || nv>UINT32_MAX/28-vo)goto done;
   for(unsigned i=vo;i<vo+nv;i++){
    unsigned at=i*28;if(read_at(sections[3],at,vertex,28))goto done;double pos[3];for(unsigned axis=0;axis<3;axis++)pos[axis]=real(vertex+12+axis*4);
    unsigned owner=half(vertex+24);if(owner>=bones)goto done;
    if(mode==3)transform(pos,matrices[owner],pos,0);
    double closest=HUGE_VAL;unsigned selected=0;
    for(unsigned p=0;p<r.count;p++){double distance=0;for(unsigned axis=0;axis<3;axis++){double d=pos[axis]-r.points[p].p[axis];distance+=d*d;}if(distance<closest){closest=distance;selected=p;}}
    total++;
    if(closest<=(mode==3?0.016*0.016:0.005*0.005)){memcpy(vertex+8,r.points[selected].color,3);matched++;}
    else if(mode==2)goto done;
    else if(mode==3){
     if(read_at(lighting,i*20,lr,20))goto done;
     if(word(lr+16)&256){
      double normal[3],head_normal[3],dot=0;for(unsigned axis=0;axis<3;axis++)normal[axis]=real(lr+axis*4);
      transform(normal,matrices[owner],normal,1);double length=sqrt(0.4*0.4+0.8*0.8+0.45*0.45);
      double direction[3]={0.4/length,0.8/length,0.45/length};for(unsigned axis=0;axis<3;axis++)dot+=normal[axis]*direction[axis];
      double smooth=0.55+0.45*(dot>0?dot:0);transform(head_normal,r.head,normal,1);dot=0;
      for(unsigned axis=0;axis<3;axis++)dot+=light.cel_direction[axis]*head_normal[axis];
      unsigned cel=(-dot)>0,material=word(lr+12);
      for(unsigned axis=0;axis<3;axis++){
       unsigned value=(material>>(axis*8))&255,legacy=(unsigned)(value*smooth),baked=(unsigned)(value*(light.cel_colors[cel][axis]/255.0));
       unsigned mixed=(unsigned)((legacy+baked)*0.5+0.5);
       vertex[8+axis]=(unsigned char)(mixed+(value-(double)mixed)*source->model.shadow_lift+0.5);
      }
     }
    }
    if(fseek(sections[3],at+8,SEEK_SET)||fwrite(vertex+8,1,3,sections[3])!=3)goto done;
   }
  }
 }
 if(!matched || (mode==3&&matched*2<total) || fflush(sections[3]))goto done;
 for(unsigned i=0;i<4;i++)if(fseek(sections[i],0,SEEK_END))goto done;
 result=0;
done:jgr_model_cache_free(r.cache);free(r.points);free(matrices);return result;
}
