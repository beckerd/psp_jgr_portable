#include "traffic_bounds.h"
#include <math.h>
#include <string.h>
static const unsigned char *span(const Scene *s,unsigned p,unsigned n)
{return p>=s->base && p-s->base<=s->length && n<=s->length-(p-s->base)?s->image+p-s->base:0;}
static unsigned word(const unsigned char *p){unsigned v;memcpy(&v,p,4);return v;}
static int bounds_enabled=1;
void traffic_bounds_mode(int enabled){bounds_enabled=!!enabled;}
float traffic_motion_radius(const Chars *c,unsigned model,const Scene *s,unsigned motion)
{
    if(!c || !c->h || model>=c->h->n_chars || !c->render_radius || !s || !s->image)return INFINITY;
    const JgcChar *ch=c->chars+model;const unsigned char *m=span(s,motion,12);
    if(!m || !ch->nbones || ch->nbones>64)return INFINITY;
    unsigned kind=word(m+8),channels=kind>>16;
    if(kind!=0x20003 && kind!=0x30007)return INFINITY;
    const unsigned char *md=span(s,word(m),ch->nbones*channels*8);if(!md)return INFINITY;
    float reach[64],scale[64],radius=0;
    for(unsigned i=0;i<ch->nbones;i++) {
        const JgcBone *b=c->bones+ch->bone0+i;int parent=b->parent-(int)ch->bone0;
        if(parent>=(int)i || (i && parent<0))return INFINITY;
        float extent[3],largest=1;
        for(int j=0;j<3;j++) {
            extent[j]=fabsf(b->pos[j]);float v=fabsf(b->scl[j]);
            if(!isfinite(v) || !isfinite(extent[j]))return INFINITY;
            if(v>largest)largest=v;
        }
        for(unsigned channel=0;channel<channels;channel+=2) {
            const unsigned char *row=md+i*channels*8;
            unsigned p=word(row+4*channel),count=word(row+channels*4+4*channel);
            if(!p)continue;
            if(count>10000)return INFINITY;
            const unsigned char *keys=span(s,p,count*16);if(!keys)return INFINITY;
            /* The caller already sampled/validated the live root position,
               which is the centre of this bound. Root translation cancels
               from every vertex's distance to that centre (reach[0] is zero).
               Keep the channel's count/span validation, but do not walk its
               unrelated route keys. Nonfinite keys elsewhere on the route
               cannot change this radius; the pose sampler rejects them if
               subsequently selected. Root scale and all child channels still
               contribute and retain their full finite/extrema checks. */
            if(bounds_enabled && !i && !channel)continue;
            for(unsigned k=0;k<count;k++)for(int axis=0;axis<3;axis++) {
                float v;memcpy(&v,keys+k*16+4+axis*4,4);v=fabsf(v);
                if(!isfinite(v))return INFINITY;
                if(channel){if(v>largest)largest=v;}else if(v>extent[axis])extent[axis]=v;
            }
        }
        float ps=parent>=0?scale[parent]:1.f;
        scale[i]=ps*largest*1.0001f;
        reach[i]=i?reach[parent]+ps*sqrtf(extent[0]*extent[0]+extent[1]*extent[1]+extent[2]*extent[2]):0;
        float owner=c->render_radius[ch->bone0+i];
        if(owner>=0){float r=reach[i]+scale[i]*owner*1.732051f;if(r>radius)radius=r;}
    }
    return radius*1.001f+.05f;
}
