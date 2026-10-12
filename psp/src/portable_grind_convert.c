#include "portable_grind_convert.h"
#include "portable_stage.h"
#include "portable_models.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define EB 0x8c010000u
#define SB 0x8cb00000u
static unsigned word(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static double number(const unsigned char *p){unsigned bits=word(p);float f;memcpy(&f,&bits,4);return f;}
static int get(JgrDisc *d,const JgrDiscFile *f,unsigned base,unsigned a,unsigned *v){unsigned char b[4];if(a<base||jgr_disc_read(d,f,a-base,b,4))return -1;*v=word(b);return 0;}
static int valid(const JgrDiscFile *f,unsigned p){return p>=SB&&p-SB<f->size;}
int jgr_grind_lights(JgrDisc *d,const char *cache,FILE *out)
{
 JgrDiscFile exe,source;JgrStageProp *props=NULL;unsigned count=0,capacity=0;int result=-1;
 if(!d||!cache||!out||jgr_disc_find(d,"1ST_READ.BIN",&exe)||jgr_disc_source(d,"decompile/afs/STAGE6/000_st6dat.bin",&source))return -1;
 for(unsigned sector=0;sector<81;sector++){
  unsigned p,head;if(get(d,&exe,EB,0x8c20ae08+sector*4,&p))goto done;if(!valid(&source,p))continue;
  if(get(d,&source,SB,p,&head))goto done;
  int pointers=valid(&source,head),terminated=0;
  for(unsigned guard=0;guard<10000;guard++,p+=pointers?4:52){
   unsigned a=p;if(pointers){if(get(d,&source,SB,p,&a))goto done;if(!a){terminated=1;break;}}
   unsigned type;if(get(d,&source,SB,a,&type))goto done;if(!pointers&&(int32_t)type<-1){terminated=1;break;}
   if(type>=10&&type!=13&&type!=25&&type!=34)continue;
   if(count>=4096)goto done;
   if(count==capacity){unsigned n=capacity?capacity*2:64;JgrStageProp *q=realloc(props,n*sizeof *q);if(!q)goto done;props=q;capacity=n;}
   JgrStageProp *prop=props+count++;memset(prop,0,sizeof *prop);
   unsigned root,texlist;if(get(d,&exe,EB,0x8c20b37c+type*4,&root)||get(d,&exe,EB,0x8c20b40c+type*4,&texlist))goto done;
   prop->root=root;prop->texlist=texlist;
   unsigned char b[36];if(jgr_disc_read(d,&source,a-SB+4,b,sizeof b))goto done;JgrModelNode node;memset(&node,0,sizeof node);
   for(unsigned k=0;k<3;k++){node.position[k]=number(b+4*k);node.angle[k]=(int32_t)word(b+12+4*k);node.scale[k]=number(b+24+4*k);if(!isfinite(node.position[k])||!isfinite(node.scale[k]))goto done;}
   jgr_model_node_matrix(prop->matrix,&node,0);
  }
  if(!terminated)goto done;
 }
 if(!count||jgr_stage_light_bank(d,6,cache,props,count,out))goto done;
 result=0;
done:free(props);return result;
}
