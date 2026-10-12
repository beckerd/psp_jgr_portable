#include "grind_map.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
int grind_map_load(GrindMap *map,const char *path)
{
    unsigned char header[8];int bad=0;
    memset(map,0,sizeof *map);
    FILE *file=fopen(path,"rb");if(!file)return -1;
    if(fread(header,1,8,file)!=8 || memcmp(header,"GMP1\x09\0\0\0",8) ||
       fread(map->entry,1,936,file)!=936 || fread(map->title,1,64,file)!=64 ||
       fread(map->glyphs,1,48,file)!=48 || fread(map->palette,1,64,file)!=64 ||
       fread(map->pixels,1,8192,file)!=8192 || fgetc(file)!=EOF || ferror(file))bad=1;
    if(fclose(file))bad=1;
    for(unsigned i=0;!bad && i<9;i++) {
        GrindMapEntry *e=map->entry+i;
        if(!isfinite(e->x) || !isfinite(e->y) || e->x<0 || e->x>640 || e->y<0 || e->y>480 ||
           !memchr(e->name,0,32) || !memchr(e->prompt,0,64))bad=1;
    }
    if(!memchr(map->glyphs,0,43))bad=1;
    if(map->title[0]>=8 || map->title[1])bad=1;
    if(bad){memset(map,0,sizeof *map);return -1;}
    return 0;
}
