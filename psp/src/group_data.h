#ifndef GROUP_DATA_H
#define GROUP_DATA_H
#include <string.h>

/* Bounds-checked PSP GRP1 container adapter, not a Dreamcast task. All
   subrecords are retained; the old RIV1 path only exported subgroup zero. */
typedef struct {
    const unsigned char *subs;
    unsigned count, direct, members, fallback;
    int supported;
} GroupRecord;
static inline unsigned group_u32(const unsigned char *p)
{ return p[0] | ((unsigned)p[1]<<8) | ((unsigned)p[2]<<16) | ((unsigned)p[3]<<24); }
static inline int group_record_read(const void *data,unsigned len,int record,GroupRecord *out)
{
    const unsigned char *d=data;unsigned at=8;
    if(!d || len<8 || memcmp(d,"GRP1",4) || record<0 ||
       group_u32(d+4)>64 || (unsigned)record>=group_u32(d+4))return -1;
    for(int r=0;r<=record;r++) {
        if(len-at<4)return -1;
        unsigned count=group_u32(d+at);at+=4;
        if(count>64)return -1;
        GroupRecord result={d+at,count,0,0,0,1};
        for(unsigned s=0;s<count;s++) {
            if(len-at<44)return -1;
            unsigned type=group_u32(d+at),entries=group_u32(d+at+40);at+=44;
            if(entries>(type==31?256:32) || entries>(len-at)/28)return -1;
            if(type<=25 || type==31 || type==32) {
                if(entries)result.direct++;else result.supported=0;
            } else if(type>=26 && type<=30) {
                result.fallback++;
                if(entries)result.members+=entries-1;
            } else result.supported=0;
            at+=28*entries;
        }
        if(r==record){*out=result;return 0;}
    }
    return -1;
}
#endif
