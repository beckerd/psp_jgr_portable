#ifndef JGR_SCENE_ARCHIVE_H
#define JGR_SCENE_ARCHIVE_H
#include <stdio.h>
#include <stdint.h>
#include <string.h>
/* SCN1 retains the unmodified Dreamcast images. Validate a descriptor
   without allocating its image or any renderer/audio ownership. */
static inline int scene_archive_record(FILE *f,int id,uint32_t rec[5])
{
    unsigned char magic[4];uint32_t count=0;
    if(!f)return -1;
    if((unsigned)id>=112 || fseek(f,0,SEEK_SET) || fread(magic,1,4,f)!=4 ||
       memcmp(magic,"SCN1",4) || fread(&count,4,1,f)!=1 || count!=112 ||
       fseek(f,8+id*20,SEEK_SET) || fread(rec,20,1,f)!=1 ||
       rec[0]!=(unsigned)id || rec[4]<12 || rec[4]>0x1000000 ||
       rec[3]<8+112*20)return -2;
    if(fseek(f,0,SEEK_END))return -2;
    long end=ftell(f);
    if(end<0 || rec[3]>(unsigned long)end || rec[4]>(unsigned long)end-rec[3])return -4;
    return 1;
}
#endif
