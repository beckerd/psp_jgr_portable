#ifndef JGR_GRIND_MAP_H
#define JGR_GRIND_MAP_H
#include <stdint.h>
/* GMP1: nine {x,y,name[32],prompt[64]} records, one source image-list
   quad, glyph names[48], RGBA palette[16], and a 256x64 4-bit font. */
typedef struct {float x,y;char name[32],prompt[64];} GrindMapEntry;
typedef struct {
    GrindMapEntry entry[9];
    unsigned char title[64];
    char glyphs[48];
    uint32_t palette[16] __attribute__((aligned(16)));
    unsigned char pixels[8192] __attribute__((aligned(16)));
} GrindMap;
int grind_map_load(GrindMap *,const char *path);
#endif
