#ifndef JGR_PLAYER_LIGHT_H
#define JGR_PLAYER_LIGHT_H
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef struct {float normal[3];uint32_t material,flags;} PlayerLightVertex;
static inline uint32_t player_light_word(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static inline uint32_t player_light_geometry_hash(const unsigned char *v,unsigned count)
{
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<count;i++,v+=28)for(unsigned j=0;j<28;j++)
        if(j<8 || j>=12)hash=(hash^v[j])*16777619u;
    return hash;
}
static inline const PlayerLightVertex *player_light_bind(const void *image,unsigned size,
                                                        const void *vertices,unsigned nv,unsigned nc)
{
    const unsigned char *b=image;
    if(!b || ((uintptr_t)b&3) || !vertices || nv>100000 || size!=20+nv*20 || memcmp(b,"PLT1",4) ||
       player_light_word(b+4)!=nv || player_light_word(b+8)!=nc || player_light_word(b+16)!=20 ||
       player_light_word(b+12)!=player_light_geometry_hash(vertices,nv))return NULL;
    const PlayerLightVertex *v=(const PlayerLightVertex *)(b+20);
    for(unsigned i=0;i<nv;i++) {
        if(v[i].flags&~0x103u || (v[i].flags&3)>2)return NULL;
        for(int j=0;j<3;j++)if(!isfinite(v[i].normal[j]))return NULL;
    }
    return v;
}
#endif
