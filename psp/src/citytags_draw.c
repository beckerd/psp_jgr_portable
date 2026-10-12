#include "ge_submit.h"
#include "ge_projection.h"
#include "citytags.h"
#include "source_trs.h"
#include "asset_progress.h"
#include <pspkernel.h>
#include <pspgu.h>
#include <pspgum.h>
#include <string.h>
typedef struct {float u,v;unsigned colour;float x,y,z;} CityTagVertex;
static void wall_matrices(const GfoTemplate *t,const Spot *s,float matrices[64][16])
{
    /* 03e8a4 -> 235a72 draws the CNK tree in world space. The descriptor
       TRS builds the interaction volume (0c4ab4), not the visible mesh.
       Its authored root already carries placement: applying both sends
       completed car/wall tags to another part of the city. */
    const float root[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};(void)s;
    Chars bank={0};JgcChar ch={0};bank.bones=(JgcBone *)gfo_bones(t);ch.nbones=t->nbones;
    chars_mesh_matrices(&bank,&ch,-1,0,root,matrices);
}
static void wall_position(const float *m,const float pos[3],float out[3])
{
    /* Keep the original rounded FTRV adapter, but execute once at load.
       In particular, replacing double with float here changes vertices. */
    for(int k=0;k<3;k++){double x=(double)m[k*4]*pos[0];x+=(double)m[k*4+1]*pos[1];x+=(double)m[k*4+2]*pos[2];x+=m[k*4+3];out[k]=(float)x;}
}
void citytags_prepare(CityTags *tags,const Graffiti *g)
{
    if(tags->worldspace || !tags->image || tags->count!=(unsigned)g->n)return;
    for(unsigned i=0;i<tags->count;i++) {
        const GfoTemplate *t=tags->mesh[i];float matrices[64][16];
        wall_matrices(t,g->spots+i,matrices);
        GfoVertex *v=(GfoVertex *)gfo_vertices(t);
        for(unsigned j=0;j<t->nvertex;j++) {
            float pos[3];wall_position(matrices[v[j].bone],v[j].pos,pos);
            memcpy(v[j].pos,pos,sizeof pos);
        }
        asset_progress();
    }
    tags->worldspace=1;
}
static void citytags_draw_textures(const CityTags *tags,const Graffiti *g,const ObjectView *view,const Tex2dSet *staff)
{
    if(!g || tags->count!=(unsigned)g->n || !tags->mesh || !tags->mask ||
       !tags->art[0] || !tags->art[1] || !tags->art[2])return;
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGuEnable(GU_DEPTH_TEST);sceGuDisable(GU_ALPHA_TEST);
    /* Authored paint can sit only .03 units ahead of its wall (Shibuya's
       uphill gate). That separation is below one 16-bit depth step at
       road distance. Bias only the paint by two depth-buffer units; the
       reversed GEQUAL depth test still occludes it behind nearer geometry.
       Keep the original mesh positions and restore the viewport afterward. */
    sceGuDepthOffset(GE_DECAL_DEPTH_BIAS);
    for(unsigned i=0;i<tags->count;i++) {
        const Spot *s=g->spots+i;if(!s->active || s->attached)continue;
        if(!object_tag_visible(view,s->x,s->y,s->z))continue;
        const GfoTemplate *t=tags->mesh[i];GfoProgress p={s->stroke,s->reveal,s->complete,s->started,0};
        /* 03e3cc creates mode 0; 03e540's pre-painted variant creates
           mode 1. Only the latter keeps graph stroke 25 visible. */
        int mode=s->pretag!=0;
        if(!s->started && !mode)continue;
        float matrices[64][16];if(!tags->worldspace)wall_matrices(t,s,matrices);
        const GfoVertex *src=gfo_vertices(t);const uint32_t *material=(const uint32_t *)(src+t->nvertex);
        /* Texlist 1 is the rival's underpaint (reveal stroke 25), not a
           foreground mask. Both layers are translucent and do not write
           depth: the GE blends in submission order, unlike the source's
           sorted translucent rendering. Submit underpaint before texlist
           0 so each revealed stroke covers it, retaining world occlusion. */
        for(unsigned layer=0;layer<2;layer++)for(unsigned first=0;first<t->nvertex/3;) {
            unsigned end=first+1;while(end<t->nvertex/3 && !memcmp(material+first*3,material+end*3,12))end++;
            unsigned tex=material[first*3],flags=material[first*3+1];
            if(tex!=1-layer){first=end;continue;}
            unsigned width=tex ? 256:128u<<(t->kind-1),height=tex ? 256:128;
            const unsigned short *image=tex ? tags->mask:tags->art[t->kind-1];
            if(staff) {
                /* Original shared texlists: XL -> buffer 0, M -> 1,
                   S -> 2. Preserve the mesh UVs, including the final
                   wall's bottom-half THANKS FOR PLAYING artwork. */
                const Tex2d *card=&staff[3-t->kind].t[0];
                image=card->px;width=card->w;height=card->h;
            }
            sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_4444,0,0,0);sceGuTexImage(0,width,height,width,image);
            sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_REPEAT,GU_REPEAT);
            static const int factor[8]={GU_FIX,GU_FIX,GU_OTHER_COLOR,GU_ONE_MINUS_OTHER_COLOR,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,GU_DST_ALPHA,GU_ONE_MINUS_DST_ALPHA};
            unsigned blend=flags>>8,sf=(blend>>5)&7,df=(blend>>8)&7;
            if(flags&8){sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,factor[sf],factor[df],sf==1?0xffffff:0,df==1?0xffffff:0);}else sceGuDisable(GU_BLEND);
            sceGuDepthMask(flags&8 ? GU_TRUE:GU_FALSE);
            if(flags&16)sceGuDisable(GU_CULL_FACE);else{sceGuEnable(GU_CULL_FACE);sceGuFrontFace(GU_CCW);}
            CityTagVertex *v=sceGuGetMemory((end-first)*3*sizeof *v);
            for(unsigned j=first*3;j<end*3;j++) {
                CityTagVertex *d=v+j-first*3;
                d->u=src[j].u;d->v=src[j].v;d->colour=gfo_colour(&p,src[j].reveal,mode);
                /* CNK vertex-colour lists (23/2a) use the rewritten
                   vertex colour, not the chunk's diffuse RGB. */
                if(tags->worldspace)memcpy(&d->x,src[j].pos,12);
                else wall_position(matrices[src[j].bone],src[j].pos,&d->x);
            }
            ge_submit_aux(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,(end-first)*3,0,v);first=end;
        }
    }
    sceGuDepthOffset(0);
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);sceGuEnable(GU_CULL_FACE);sceGuDisable(GU_TEXTURE_2D);
}
void citytags_draw_view(const CityTags *tags,const Graffiti *g,const ObjectView *view)
{citytags_draw_textures(tags,g,view,NULL);}
void citytags_draw(const CityTags *tags,const Graffiti *g)
{citytags_draw_view(tags,g,NULL);}
void citytags_draw_staff(const CityTags *tags,const Graffiti *g,const Tex2dSet cards[3])
{
    if(!cards)return;
    for(unsigned i=0;i<3;i++)if(cards[i].n!=1 || !cards[i].t[0].px || cards[i].t[0].w!=512 || cards[i].t[0].h!=512)return;
    citytags_draw_textures(tags,g,NULL,cards);
}
