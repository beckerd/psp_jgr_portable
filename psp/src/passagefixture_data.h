#ifndef JGR_PASSAGE_FIXTURE_DATA_H
#define JGR_PASSAGE_FIXTURE_DATA_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Diagnostic selection metadata only. Positions remain in the user's
   validated route asset, and are read once before the fixture starts. */
static unsigned passage_fixture_word(const unsigned char *p)
{
    return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);
}
static int passage_fixture_route_points(float points[11][3],const char *path)
{
    unsigned char header[64];FILE *f=fopen(path,"rb");
    if(!f)return -1;
    int result=-1;
    if(fread(header,1,32,f)!=32 || memcmp(header,"TRT3",4))goto done;
    unsigned count=passage_fixture_word(header+4),stride=passage_fixture_word(header+16);
    unsigned channels=passage_fixture_word(header+20),start=passage_fixture_word(header+24);
    if(!count || count>30000 || passage_fixture_word(header+8)!=0 ||
       passage_fixture_word(header+12)!=20 || channels>120 || stride!=440+channels*4 ||
       start!=passage_fixture_word(header+28) || start>=count || count-start<=620)goto done;
    long data_start=512+(long)channels*4;
    if(fseek(f,0,SEEK_END) || ftell(f)!=data_start+(long)count*stride)goto done;
    for(unsigned i=0;i<11;i++) {
        unsigned tick=321+i*30;
        if(fseek(f,data_start+(long)(start+tick-1)*stride,SEEK_SET) ||
           fread(header,1,sizeof header,f)!=sizeof header ||
           passage_fixture_word(header+56)!=tick || header[60]!=1 || header[61]!=0)goto done;
        for(unsigned k=0;k<3;k++) {
            uint32_t bits=passage_fixture_word(header+k*4);
            memcpy(&points[i][k],&bits,4);
            if(!isfinite(points[i][k]))goto done;
        }
    }
    result=0;
done:
    if(fclose(f))result=-1;
    return result;
}

/* Kogane's diagnostic path is user-local test input, not shipped game data.
   Format: JGR_PASSAGE 1 5 followed by five finite XYZ triples. */
static int passage_fixture_local_points(float points[11][3],const char *path)
{
    FILE *f=fopen(path,"rb");if(!f)return -1;
    char magic[16],extra;unsigned version,count;int result=-1;
    if(fscanf(f,"%15s %u %u",magic,&version,&count)!=3 ||
       strcmp(magic,"JGR_PASSAGE") || version!=1 || count!=5)goto done;
    for(unsigned i=0;i<count;i++)for(unsigned k=0;k<3;k++)
        if(fscanf(f,"%f",&points[i][k])!=1 || !isfinite(points[i][k]))goto done;
    if(fscanf(f," %c",&extra)==1 || ferror(f))goto done;
    result=0;
done:
    if(fclose(f))result=-1;
    return result;
}
#endif
