#include "graffiti_arrow.h"
#include <pspgu.h>
#include <math.h>
#include <string.h>
#if JGR_PORTABLE_ASSETS
#include "source_data_graffiti_arrow_skater.inc"
#else
#include "generated/source_data/graffiti_arrow_skater.inc"
#endif
static void set_view(const float v[16]);

const float *graffiti_arrow_skater(unsigned character)
{return character<16 ? SKATER_ARROW[character]:NULL;}
int graffiti_arrow_rival_enabled(int kind,int table)
{
    /* 069a7c selects 027310 (flags e238, no GF) for 4/5/7/8;
       all other event-0c rivals use 027224 (flags 3203b, GF bit 1).
       Plain demo streams do not have a controller table. */
    return table>=0 && kind!=4 && kind!=5 && kind!=7 && kind!=8;
}
void graffiti_arrow_billboard(const float descriptor[6],const float parent[16],const float view[16],float root[16])
{
    float combined[16];const float *v=view;
    if(parent) {
        for(int r=0;r<4;r++)for(int c=0;c<4;c++) {
            double sum=(double)view[r*4]*parent[c];
            for(int k=1;k<4;k++)sum+=(double)view[r*4+k]*parent[k*4+c];
            combined[r*4+c]=(float)sum;
        }
        v=combined;
    }
    memset(root,0,64);root[15]=1;
    for(int r=0;r<3;r++) {
        double sum=(double)v[r*4]*descriptor[0];sum+=(double)v[r*4+1]*descriptor[1];
        sum+=(double)v[r*4+2]*descriptor[2];sum+=v[r*4+3];root[r*4+3]=(float)sum;
        /* 28094c removes rotation but retains translation. Ninja's
           (sx,-sy,-sz) and conversion to PSP camera space cancel signs. */
        root[r*5]=descriptor[3+r];
    }
}

int graffiti_arrow_model(const Spot *s,const float view[16],const float eye[3],float root[16])
{
    if(!s || !s->source || !s->active || s->attached || s->complete || s->tagged)return -1;
    float dx=s->x-eye[0],dy=s->y-eye[1],dz=s->z-eye[2];
    if(dx*dx+dy*dy+dz*dz>1440000.f)return -1; /* 03ea30: 1200 squared */
    graffiti_arrow_billboard(s->source->unused,NULL,view,root);
    return s->required ? 1:0; /* effect table 17 (red) / 2 (optional) */
}
int graffiti_arrow_draw_at(Chars *bank,const float descriptor[6],const float parent[16],
                          const float view[16],int required,float frame)
{
    if(!bank || !bank->h || !descriptor)return 0;
    chars_arrows_begin();
    float root[16];graffiti_arrow_billboard(descriptor,parent,view,root);
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};set_view(identity);
    chars_draw_arrow(bank,required ? 1:0,root,frame);chars_arrows_end();set_view(view);return 1;
}
int graffiti_arrow_draw_object(Chars *bank,const GfoObject *o,const float parent[16],
                              const float view[16],int required,float frame,int hidden)
{
    if(hidden || !o || !o->handle || !o->tpl || o->progress.complete ||
       (required && o->progress.busy))return 0;
    float descriptor[6];memcpy(descriptor,o->tpl->descriptor+0x3c,sizeof descriptor);
    /* 06e074 and the final-stage drawers have NO city 1200-unit gate. */
    return graffiti_arrow_draw_at(bank,descriptor,parent,view,required,frame);
}
int graffiti_arrows_draw_final(Chars *bank,const GfoObject objects[5],const Graffiti *g,const float view[16],int hidden)
{
    if(hidden || !g || g->n!=5)return 0;
    int drawn=0;
    for(int i=0;i<5;i++) {
        const Spot *s=g->spots+i;const GfoObject *o=objects+i;
        if(!s->active || s->complete || s->tagged || (g->mg_active && g->near==i))continue;
        float parent[16];for(int r=0;r<4;r++)for(int c=0;c<4;c++)parent[r*4+c]=o->matrix[c*4+r];
        /* 0ecfd8 translates by original 1de928 BEFORE the Goji arrow.
           The outer four use GF+100 directly in 0e975c. */
        if(i==4)for(int r=0;r<3;r++)parent[r*4+3]+=parent[r*4+1]*14.39f;
        drawn+=graffiti_arrow_draw_object(bank,o,parent,view,1,s->arrow_frame,0);
    }
    return drawn;
}
static void set_view(const float v[16])
{
    ScePspFMatrix4 matrix;float *m=(float *)&matrix;
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)m[c*4+r]=v[r*4+c];
    sceGuSetMatrix(GU_VIEW,&matrix);
}
int graffiti_arrows_draw_view(Chars *bank,const Graffiti *g,const float view[16],const float eye[3],int hidden,const ObjectView *objects)
{
    if(hidden || !bank || !bank->h || !g)return 0;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    int drawn=0;chars_arrows_begin();set_view(identity);
    for(int i=0;i<g->n;i++) {
        /* GF+154 is the large-spray lock, not the pre-painted flag. */
        if(g->mg_active && g->near==i)continue;
        const Spot *s=g->spots+i;
        if(!object_tag_visible(objects,s->x,s->y,s->z))continue;
        float root[16];int model=graffiti_arrow_model(g->spots+i,view,eye,root);
        if(model<0)continue;
        chars_draw_arrow(bank,model,root,g->spots[i].arrow_frame);drawn++;
    }
    chars_arrows_end();set_view(view);return drawn;
}
int graffiti_arrows_draw(Chars *bank,const Graffiti *g,const float view[16],const float eye[3],int hidden)
{return graffiti_arrows_draw_view(bank,g,view,eye,hidden,NULL);}
