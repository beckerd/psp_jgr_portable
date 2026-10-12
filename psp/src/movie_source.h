#ifndef JGR_MOVIE_SOURCE_H
#define JGR_MOVIE_SOURCE_H
#include "scene_storage.h"

/* Preserve original virtual addresses and bytes. Large DEMOs must not
   require one multi-megabyte hole in a heap fragmented by gameplay. */
typedef struct {
    unsigned char *image;
    SceneStorage paged;
    unsigned length;
} MovieSource;

static inline void movie_source_free(MovieSource *s)
{free(s->image);scene_storage_free(&s->paged);memset(s,0,sizeof *s);}

static inline int movie_source_load(MovieSource *s,const char *path)
{
    movie_source_free(s);
    FILE *f=fopen(path,"rb");if(!f)return -1;
    int result=-4;long n=0;
    if(fseek(f,0,SEEK_END) || (n=ftell(f))<12 || n>0x1000000 || fseek(f,0,SEEK_SET))goto done;
#ifdef JGR_MOVIE_ALLOC_LIMIT
    if(n<=JGR_MOVIE_ALLOC_LIMIT)
#endif
    s->image=malloc((unsigned)n);
    if(s->image) {
        if(fread(s->image,1,(unsigned)n,f)!=(unsigned)n)goto done;
    } else {
        result=scene_storage_load(&s->paged,f,0,(unsigned)n);
        if(result<0)goto done;
    }
    s->length=(unsigned)n;result=0;
done:
    fclose(f);if(result)movie_source_free(s);return result;
}
#endif
