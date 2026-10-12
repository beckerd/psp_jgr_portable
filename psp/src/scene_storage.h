#ifndef JGR_SCENE_STORAGE_H
#define JGR_SCENE_STORAGE_H
#include "scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char **pages;
    unsigned length;
} SceneStorage;

static inline void scene_storage_free(SceneStorage *s)
{
    if(s->pages) {
        unsigned count=(s->length+SCENE_PAGE_SIZE-1)>>SCENE_PAGE_SHIFT;
        for(unsigned i=0;i<count;i++)free(s->pages[i]);
        free(s->pages);
    }
    memset(s,0,sizeof *s);
}

/* Fragmented gameplay heaps can have enough total free memory but no hole
   large enough for a raw EVENT (EVENT 8 is 226896 bytes). Read the unmodified
   image into 4 KiB pages, with no file I/O or allocation during playback. */
static inline int scene_storage_load(SceneStorage *s,FILE *f,unsigned offset,unsigned length)
{
    scene_storage_free(s);
    if(!f || length<12 || length>0x1000000 || fseek(f,offset,SEEK_SET))return -4;
    unsigned count=(length+SCENE_PAGE_SIZE-1)>>SCENE_PAGE_SHIFT;
    s->pages=calloc(count,sizeof *s->pages);
    if(!s->pages)return -3;
    s->length=length;
    for(unsigned i=0;i<count;i++) {
        unsigned n=length-i*SCENE_PAGE_SIZE;if(n>SCENE_PAGE_SIZE)n=SCENE_PAGE_SIZE;
        s->pages[i]=malloc(n);
        if(!s->pages[i]){scene_storage_free(s);return -3;}
        if(fread(s->pages[i],1,n,f)!=n){scene_storage_free(s);return -4;}
    }
    return 1;
}
#endif
