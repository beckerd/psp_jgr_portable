#ifndef JGR_GE_PROJECTION_H
#define JGR_GE_PROJECTION_H
#include <math.h>
/* PSP depth is 16-bit. Dreamcast cameras (Shibuya intro: near=.1,
   far=32768; gameplay: near=1, far=60000) make separate wall layers
   collide in that buffer. Preserve the camera/FOV
   and far distance, but use an eight-unit near plane and the full depth
   range. Apply only to the world projection, not HUD/menu matrices. */
#define GE_DEPTH_NEAR 65535
#define GE_DEPTH_FAR 0
/* Surface paint needs an explicit order when its authored separation is
   smaller than one 16-bit depth step. Positive is forward with GEQUAL.
   Apply only to decal passes and restore zero before unrelated geometry. */
#define GE_DECAL_DEPTH_BIAS 2
static inline float ge_world_near(float authored_near)
{return fmaxf(authored_near,8.f);}
/* DEMO28 (movie 27), the gang/city introduction after Professor K.
   These city shots need PLAY's depth precision, but shot 2 approaches
   the rooftop cast within eight units. Its four-unit plane retains the
   close-up. Other DEMOs include camera-space art only 1..6 units away;
   do not apply the gameplay minimum to those movies/HUD panels. */
static inline float ge_movie_near(int movie,int segment,float authored_near)
{
    if(movie==27)return fmaxf(authored_near,segment==2 ? 4.f:8.f);
    /* DEMO02's Love Shockers street shots have overlapping wall/rail layers.
       Its other shots include close camera-space radio art; keep those at
       their authored near plane. */
    if(movie==1 && (segment==3 || segment==4))return fmaxf(authored_near,1.f);
    /* Post-Kogane Poison Jam refinery shot, shared by both story routes.
       Keep Professor K's close radio art at its authored near distance. */
    if((movie==4 && segment==4) || (movie==6 && segment==3))return fmaxf(authored_near,3.f);
    return authored_near;
}
/* Inward world planes for a conservative NDC +/-4 guard band. A close
   wall can cross the PSP's finite screen-coordinate range even when most
   of its triangle is on screen. Clip it before the GE drops the triangle.
   Camera view is row-major, looking along negative Z. */
static inline void ge_camera_guard_planes(const float view[16],float focal,float near,float far,float out[6][4])
{
    for(int p=0;p<4;p++)for(int j=0;j<4;j++)
        out[p][j]=-view[8+j]+((p&1) ? -1.f:1.f)*focal/(p<2 ? 1280.f:960.f)*view[(p/2)*4+j];
    for(int j=0;j<4;j++){out[4][j]=-view[8+j];out[5][j]=view[8+j];}
    out[4][3]-=near;out[5][3]+=far;
}
#endif
