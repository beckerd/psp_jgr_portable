#ifndef JGR_SPRAY_TRACKS_H
#define JGR_SPRAY_TRACKS_H
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef struct {const unsigned char *data;unsigned size,base,table,count;} SprayTracks;
static inline uint32_t spray_track_word(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static inline const unsigned char *spray_track_bytes(const SprayTracks *b,uint32_t a,unsigned n)
{return a>=b->base && a-b->base<=b->size && n<=b->size-(a-b->base) ? b->data+(a-b->base):NULL;}
static inline unsigned spray_track_u(const SprayTracks *b,uint32_t a)
{const void *p=spray_track_bytes(b,a,4);return p ? spray_track_word(p):0;}
static inline float spray_track_f(const SprayTracks *b,uint32_t a)
{float f=0;const void *p=spray_track_bytes(b,a,4);if(p)memcpy(&f,p,4);return f;}
static inline int camera_tracks_init(SprayTracks *b,const void *data,unsigned size,unsigned count)
{
    if(!data || size<16 || !count || count>17 || memcmp(data,"SPC1",4) || spray_track_word((const char *)data+12)!=count)return 0;
    SprayTracks t={(const unsigned char *)data+16,size-16,spray_track_word((const char *)data+4),spray_track_word((const char *)data+8),count};
    if(!spray_track_bytes(&t,t.table,count*4))return 0;
    for(unsigned style=0;style<count;style++) {
        unsigned rec=spray_track_u(&t,t.table+style*4);
        if(!spray_track_bytes(&t,rec,8))return 0;
        unsigned ini=spray_track_u(&t,rec),tr=spray_track_u(&t,rec+4),md=spray_track_u(&t,tr);
        if(!spray_track_bytes(&t,ini,64) || !spray_track_bytes(&t,tr,12) ||
           !spray_track_bytes(&t,md,32) || spray_track_u(&t,tr+8)!=0x401c1 || !spray_track_u(&t,tr+4))return 0;
        for(int ch=0;ch<4;ch++) {
            unsigned p=spray_track_u(&t,md+ch*4),n=spray_track_u(&t,md+16+ch*4),stride=ch<2 ? 16:8;
            if(!n || n>1024 || !spray_track_bytes(&t,p,n*stride))return 0;
            unsigned first=spray_track_u(&t,p),previous=0;
            for(unsigned k=0;k<n;k++) {
                unsigned clock=spray_track_u(&t,p+k*stride)-first;
                if(clock<previous || clock>100000)return 0;previous=clock;
                if(ch<2)for(int j=0;j<3;j++)if(!isfinite(spray_track_f(&t,p+k*stride+4+j*4)))return 0;
                if(ch>=2) {
                    int32_t angle=(int32_t)spray_track_u(&t,p+k*stride+4);
                    if(angle< -65536 || angle>65536 || (ch==3 && (angle<=0 || angle>=32768)))return 0;
                }
            }
        }
    }
    *b=t;return 1;
}
static inline int spray_tracks_init(SprayTracks *b,const void *data,unsigned size)
{return camera_tracks_init(b,data,size,17);}
/* 01eb80 -> 0206dc -> 0bf75c. Source keys use relative unsigned clocks.
   The original interpolator takes (frame-keyClock), without division by
   key spacing. Preserve this even for sparse target/angle streams. */
static inline int spray_tracks_sample(const SprayTracks *b,unsigned style,float frame,const float matrix[16],
                                      float eye[3],float forward[3],int *roll,int *fov,float *next)
{
    if(!b->data || style>=b->count || !isfinite(frame) || frame<0)return 0;
    unsigned rec=spray_track_u(b,b->table+style*4),tr=spray_track_u(b,rec+4),md=spray_track_u(b,tr);
    float point[2][3];
    for(int ch=0;ch<4;ch++) {
        unsigned p=spray_track_u(b,md+ch*4),n=spray_track_u(b,md+16+ch*4),stride=ch<2 ? 16:8;
        unsigned first=spray_track_u(b,p),k=n-1;
        while(k && frame<(float)(spray_track_u(b,p+k*stride)-first))k--;
        unsigned at=p+k*stride;float f=frame-(float)(spray_track_u(b,at)-first);
        if(ch<2)for(int j=0;j<3;j++) {
            float a=spray_track_f(b,at+4+j*4),v=a;
            if(k+1<n)v=fmaf(f,spray_track_f(b,at+stride+4+j*4)-a,a);
            point[ch][j]=v;
        } else {
            int a=(int32_t)spray_track_u(b,at+4),v=a;
            if(k+1<n) {
                int d=(int32_t)spray_track_u(b,at+stride+4)-a;
                if(d>32768)d-=65536;else if(d< -32768)d+=65536;
                v=(int)fmaf((float)d,f,(float)a);
            }
            if(ch==2)*roll=v;else *fov=v;
        }
    }
    float transformed[2][3];
    for(int ch=0;ch<2;ch++)for(int j=0;j<3;j++) {
        double v=(double)matrix[j*4]*point[ch][0];v+=(double)matrix[j*4+1]*point[ch][1];
        v+=(double)matrix[j*4+2]*point[ch][2];v+=matrix[j*4+3];transformed[ch][j]=(float)v;
    }
    memcpy(eye,transformed[0],12);
    for(int j=0;j<3;j++)forward[j]=transformed[1][j]-eye[j];
    double n=(double)forward[0]*forward[0];n+=(double)forward[1]*forward[1];n+=(double)forward[2]*forward[2];
    float length=sqrtf((float)n);if(length<=0)return 0;
    /* 2809b8: FIPR, FSRRA, then three FMULs.
       Component-wise division rounds differently in the original replay. */
    float inverse=1.f/length;
    for(int j=0;j<3;j++)forward[j]*=inverse;
    *next=frame+1.f;float frames=spray_track_u(b,tr+4);if(*next>=frames)*next=frames-1.f;
    return 1;
}
#endif
