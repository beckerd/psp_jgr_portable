#include "heliwreck.h"

int heliwreck_load(Chars *bank,const char *path)
{
    int error=chars_load(bank,path);if(error){chars_free(bank);return error;}
    if(bank->h->n_chars!=20){chars_free(bank);return -1;}
    for(int i=0;i<20;i++) {
        const JgcChar *c=bank->chars+i;
        if(c->nmotions!=(i%10<2 ? 2:1) || c->motion0+c->nmotions>bank->h->n_motions) {
            chars_free(bank);return -1;
        }
        if(i%10<2 && (bank->motions[c->motion0].nframes!=80 || bank->motions[c->motion0+1].nframes!=60)) {
            chars_free(bank);return -1;
        }
    }
    return 0;
}
void heliwreck_draw(Chars *bank,const HeliDestruction *s,const float root[16])
{
    if(!bank->base)return;
    unsigned frames[8];int first=s->variant ? 10:0;
    for(int i=0;i<8;i++)frames[i]=bank->motions[bank->chars[first+2+i].motion0].nframes;
    HeliWreckDraw draws[8];int n=heli_wreck_draws(s,frames,draws);
    for(int i=0;i<n;i++) {
        const HeliWreckDraw *d=draws+i;
        chars_draw_material(bank,first+d->part,root,0,d->motion,d->frame,1.f+d->material,0);
    }
}
