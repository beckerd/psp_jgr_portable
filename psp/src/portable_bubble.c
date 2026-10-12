#include "portable_bubble.h"
#include "portable_models.h"
#include <stdlib.h>
#include <string.h>
#define BUBBLE_BYTES (256u*1024u)
typedef struct {JgrDisc *disc;const JgrDiscFile *source;JgrModelCache *cache;unsigned char *vertices,*indices,*batches;unsigned nv,ni,nb;} Bubble;
static void bw(unsigned char *p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static uint32_t bc(const unsigned char *p,unsigned n){uint32_t v=~0u;while(n--){v^=*p++;for(unsigned i=0;i<8;i++)v=(v>>1)^(0xedb88320u&(0u-(v&1)));}return ~v;}
static int bubble_batch(const JgrModelBatch *b,void *user)
{
    Bubble *s=user;if(b->stride!=24||b->vertices>1024-s->nv||b->indices>4096-s->ni||s->nb==64)return -1;
    unsigned char *out=s->batches+s->nb++*20;unsigned values[5]={s->nv,s->ni,b->indices,b->texture,b->flags};for(unsigned k=0;k<5;k++)bw(out+k*4,values[k]);
    memcpy(s->vertices+s->nv*24,b->vertex_bytes,b->vertices*24);memcpy(s->indices+s->ni*2,b->index_bytes,b->indices*2);s->nv+=b->vertices;s->ni+=b->indices;if(s->ni&1)s->ni++;return 0;
}
static int bubble_node(const JgrModelNode *n,const double matrix[16],unsigned ordinal,int parent,void *user)
{
    Bubble *s=user;(void)ordinal;(void)parent;if(!n->model)return 0;
    JgrModelOptions o;jgr_model_options_default(&o);memcpy(o.matrix,matrix,sizeof o.matrix);o.light=0;o.source_material=1;o.texture_count=8192;
    return jgr_model_mesh(s->cache,s->disc,s->source,0x8c8e1000,n->model,&o,bubble_batch,s);
}
int jgr_convert_bubble(JgrDisc *disc,const JgrDiscFile *source,FILE *out)
{
    Bubble s={0};unsigned char *data=calloc(BUBBLE_BYTES,1);unsigned offset=176;int result=-1;double identity[16];
    if(!disc||!source||!out||!data)goto done;
    s.disc=disc;s.source=source;s.cache=jgr_model_cache_create();s.vertices=calloc(1024,24);s.indices=calloc(4096,2);s.batches=calloc(64,20);
    if(!s.cache||!s.vertices||!s.indices||!s.batches)goto done;
    jgr_model_identity(identity);
    for(unsigned mesh=0;mesh<3;mesh++){
        uint32_t root=mesh==0?0x8c8e66c4:mesh==1?0x8c8e649c:0x8c8e6274;s.nv=s.ni=s.nb=0;memset(s.indices,0,4096*2);jgr_model_cache_clear(s.cache);
        if(jgr_model_walk(disc,source,0x8c8e1000,root,identity,0,1,bubble_node,&s)||!s.nv||!s.ni||!s.nb)goto done;
        unsigned counts[3]={s.nv,s.ni,s.nb},units[3]={24,2,20};const unsigned char *parts[3]={s.vertices,s.indices,s.batches};
        for(unsigned part=0;part<3;part++){
            offset=(offset+15)&~15u;unsigned bytes=counts[part]*units[part];if(offset>BUBBLE_BYTES||bytes>BUBBLE_BYTES-offset)goto done;
            unsigned char *entry=data+32+(mesh*3+part)*16;bw(entry,part+1);bw(entry+4,counts[part]);bw(entry+8,offset);bw(entry+12,bytes);memcpy(data+offset,parts[part],bytes);offset+=bytes;
        }
    }
    memcpy(data,"JGBL",4);bw(data+4,1);bw(data+8,9);bw(data+12,offset);bw(data+16,bc(data+32,144));bw(data+20,bc(data+176,offset-176));bw(data+24,176);
    result=fwrite(data,1,offset,out)==offset?0:-1;
 done:jgr_model_cache_free(s.cache);free(s.vertices);free(s.indices);free(s.batches);free(data);return result;
}
