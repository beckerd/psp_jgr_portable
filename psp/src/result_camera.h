#ifndef JGR_RESULT_CAMERA_H
#define JGR_RESULT_CAMERA_H
#include "spray_tracks.h"
#include <stdio.h>
#include <stdlib.h>
typedef struct {unsigned char *image;SprayTracks tracks;} ResultCamera;
static inline void result_camera_free(ResultCamera *c)
{free(c->image);memset(c,0,sizeof *c);}
static inline int result_camera_load(ResultCamera *c,unsigned character)
{
    if(character>=16)return -1;
    result_camera_free(c);char path[80];snprintf(path,sizeof path,"resultcam%02u.bin",character);
    FILE *f=fopen(path,"rb");
    if(!f)return -1;
    int ok=0;long size=0;
    if(!fseek(f,0,SEEK_END) && (size=ftell(f))>16 && size<=65536) {
        rewind(f);c->image=malloc(size);
        if(c->image && fread(c->image,1,size,f)==(unsigned)size)ok=camera_tracks_init(&c->tracks,c->image,size,1);
    }
    fclose(f);if(!ok){result_camera_free(c);return -2;}return 0;
}
#endif
