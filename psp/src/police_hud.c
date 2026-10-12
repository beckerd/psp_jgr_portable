#include "police_hud.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void police_hud_free(PoliceHud *h)
{ chars_free(&h->models);free(h->image);memset(h,0,sizeof *h); }
int police_hud_pose(const PoliceHud *h,int level,float frame,CharPose poses[64])
{
    if(!h || !h->models.h || !h->image || (unsigned)level>=4)return -1;
    const JgcChar *c=h->models.chars+level;
    if(c->nbones>64)return -1;
    for(unsigned i=0;i<c->nbones;i++) {
        chars_sample_pose(&h->models,c,-1,0,i,poses+i);
        if(!scene_sample_njs(&h->source,h->motion[level],frame,i,0,poses+i))return -1;
    }
    /* 049834 writes @(20,r3): decimal 20 = object +0x14, X rotation. Its
       sparse motion only changes scale, so sampling cannot supply this. */
    if(level==3)poses[0].ang[0]=16384;
    return c->nbones;
}
int police_hud_load(PoliceHud *h,const char *mesh,const char *motion)
{
    memset(h,0,sizeof *h);
    if(chars_load(&h->models,mesh))return -1;
    if(h->models.h->n_chars!=4 || h->models.h->n_motions)goto invalid;
    FILE *f=fopen(motion,"rb");if(!f)goto invalid;
    unsigned header[7];long size=-1;
    if(!fseek(f,0,SEEK_END))size=ftell(f);rewind(f);
    int ok=size>=28 && size<=0x80000 && fread(header,1,28,f)==28 &&
        !memcmp(header,"PHM2",4) && header[2]==(unsigned)size-28 && header[1]<=0xffffffffu-header[2];
    if(ok){h->image=malloc(header[2]);ok=h->image && fread(h->image,1,header[2],f)==header[2];}
    fclose(f);if(!ok)goto invalid;
    h->source.image=h->image;h->source.base=header[1];h->source.length=header[2];
    memcpy(h->motion,header+3,16);
    for(int level=0;level<4;level++) {
        CharPose pose[64];int n=level==0 || level==3 ? 30:60;
        if(scene_word(&h->source,h->motion[level]+4)!=(unsigned)n)goto invalid;
        /* Validate every sparse-key segment before publishing the bank. */
        for(int t=0;t<n;t++)if(police_hud_pose(h,level,t,pose)<0)goto invalid;
    }
    return 0;
invalid:police_hud_free(h);return -1;
}
