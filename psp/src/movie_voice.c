#include "movie_voice.h"
#include "adx.h"
#include <string.h>
static unsigned be32(const unsigned char *p)
{return (unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3];}
int movie_voice_format(const void *header,unsigned header_length,unsigned length,unsigned *offset,unsigned *samples)
{
    const unsigned char *p=header;
    if(!p || header_length<36 || length<36 || p[0]!=0x80 || p[1] || p[4]!=3 || p[5]!=18 || p[6]!=4 || p[7]!=1 ||
       be32(p+8)!=44100 || p[16]!=1 || p[17]!=0xf4 || p[18]!=3 || p[19])return 0;
    unsigned start=((unsigned)p[2]<<8|p[3])+4,n=be32(p+12);
    if(start<36 || start>length || !n || n>0xffffff || (n+31)/32>(length-start)/18)return 0;
    *offset=start;*samples=n;return 1;
}
int movie_voice_bind(MovieVoice *v,const void *image,unsigned length)
{
    memset(v,0,sizeof *v);unsigned start,n;
    if(!movie_voice_format(image,length,length,&start,&n))return 0;
    v->blocks=(const unsigned char *)image+start;v->count=(n+31)/32;v->samples=n;v->ready=1;return 1;
}
int16_t movie_voice_next(MovieVoice *v)
{
    if(!v->active || !v->ready || v->error)return 0;
    unsigned index=v->position&31;
    if(!index && adx_decode_continuous(v->blocks+v->position/32*18,18,1,v->decoded,32,v->history,7334,-3283)!=1){
        v->active=0;v->error=1;return 0;
    }
    int16_t result=v->decoded[index];if(++v->position>=v->samples)v->active=0;return result;
}
