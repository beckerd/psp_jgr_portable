/* Host raster check: background identity, subpixel coverage, tile seams,
   framebuffer swaps, EDRAM isolation, and restoration of the viewport. */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "render_stubs/pspgu.h"
#include "../src/gfx.h"
#define BUF_WIDTH 512
#define GU_TEXTURE_16BIT 2
#define GU_VERTEX_16BIT (2<<7)
#define GU_TFX_REPLACE 3
#define GU_TCC_RGB 0
#define GU_NEAREST 0
#define ge_submit_aux sceGuDrawArray
static unsigned pixels[2*1024*1024/4],edram_size=sizeof pixels;
static unsigned title_target,target,fences;
static void *draw_buffer;
static int offset[2],viewport[2];
static void *sceGeEdramGetAddr(void){return pixels;}
static unsigned sceGeEdramGetSize(void){return edram_size;}
static void sceGuDrawBufferList(int format,void *address,int width)
{assert(format==GU_PSM_8888 && width==512);target=(unsigned)(uintptr_t)address;assert(!(target&8191));}
static void sceGuTexSync(void){}
static void sceGuTexFlush(void){}
static void sceGuTexScale(float u,float v){assert(u==1 && v==1);}
static void sceGuOffset(int x,int y){offset[0]=x;offset[1]=y;}
static void sceGuViewport(int x,int y,int w,int h)
{assert(x==2048 && y==2048);viewport[0]=w;viewport[1]=h;}
void gfx_wait_idle(void){fences++;}
void gfx_mode2d(void){sceGuDisable(GU_DEPTH_TEST);sceGuDepthMask(GU_TRUE);}
void gfx_mode3d(void){sceGuEnable(GU_DEPTH_TEST);sceGuDepthMask(GU_FALSE);sceGuDisable(GU_TEXTURE_2D);}
#include "../src/title_smoothing.inc"
static unsigned sample(int x,int y)
{
    assert(x>=0 && x<512 && y>=0 && y<272);
    const unsigned *p=(const unsigned *)gu.texture+y*512+x;
    assert(p>=pixels && p<pixels+sizeof pixels/sizeof *pixels);
    return *p;
}
static void sceGuDrawArray(int primitive,int format,int count,const void *indices,const void *vertices)
{
    assert(primitive==GU_SPRITES && !indices && !(count&1));
    assert(format==(GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D));
    assert(!gu.enabled[GU_BLEND] && !gu.enabled[GU_DEPTH_TEST]);
    const TitleBlitVertex *v=vertices;
    for(int i=0;i<count;i+=2)for(int y=v[i].y;y<v[i+1].y;y++)for(int x=v[i].x;x<v[i+1].x;x++) {
        float u=v[i].u+(x-v[i].x+.5f)*(v[i+1].u-v[i].u)/(v[i+1].x-v[i].x);
        float w=v[i].v+(y-v[i].y+.5f)*(v[i+1].v-v[i].v)/(v[i+1].y-v[i].y);
        unsigned c;
        if(gu.minfilter==GU_NEAREST)c=sample((int)u,(int)w);
        else {
            int a=(int)(u-.5f),b=(int)(w-.5f);
            assert(fabsf(u-a-1.f)<.001f && fabsf(w-b-1.f)<.001f);
            unsigned p[4]={sample(a,b),sample(a+1,b),sample(a,b+1),sample(a+1,b+1)};
            c=0;
            for(int channel=0;channel<3;channel++) {
                unsigned sum=0;for(int j=0;j<4;j++)sum+=(p[j]>>(8*channel))&255;
                c|=(sum/4)<<(8*channel);
            }
        }
        pixels[target/4+y*512+x]=c&0xffffff;
    }
}
static void reset(unsigned buffer)
{
    gu_used=0;draw_buffer=(void *)(uintptr_t)buffer;target=buffer;
    for(unsigned i=0;i<sizeof pixels/sizeof *pixels;i++)pixels[i]=0x123456;
    for(int y=0;y<SCR_H;y++)for(int x=0;x<SCR_W;x++)
        pixels[buffer/4+y*512+x]=(x*1031+y*7891)&0xffffff;
}
static void check_tiles(int logo)
{
    unsigned original[SCR_W*SCR_H];
    unsigned base=(unsigned)(uintptr_t)draw_buffer/4;
    for(int y=0;y<SCR_H;y++)for(int x=0;x<SCR_W;x++)original[y*SCR_W+x]=pixels[base+y*512+x];
    for(unsigned tile=0;tile<gfx_title_tiles();tile++) {
        gfx_title_tile_begin(tile);
        assert(viewport[0]==SCR_W*2 && viewport[1]==SCR_H*2);
        assert(offset[0]==2048-SCR_W+(int)(tile&1)*SCR_W);
        assert(offset[1]==2048-SCR_H+(int)(tile>>1)*SCR_H);
        assert(target==title_target && gu.depthmask==GU_FALSE);
        /* Synthetic white diagonal spans both horizontal and vertical seams. */
        if(logo)for(int y=0;y<SCR_H;y++)for(int x=0;x<SCR_W;x++)
            if(x+(int)(tile&1)*SCR_W>y+(int)(tile>>1)*SCR_H+201)
                pixels[target/4+y*512+x]=0xffffff;
        gfx_title_tile_end(tile);
        assert(target==(unsigned)(uintptr_t)draw_buffer);
        assert(viewport[0]==SCR_W && viewport[1]==SCR_H);
        assert(offset[0]==2048-SCR_W/2 && offset[1]==2048-SCR_H/2);
    }
    unsigned partial=0;
    for(int y=0;y<SCR_H;y++)for(int x=0;x<SCR_W;x++) {
        unsigned actual=pixels[base+y*512+x],before=original[y*SCR_W+x],covered=0;
        if(logo)for(int sy=0;sy<2;sy++)for(int sx=0;sx<2;sx++)covered+=x*2+sx>y*2+sy+201;
        unsigned expected=0;
        for(int c=0;c<3;c++)expected|=((((before>>(c*8))&255)*(4-covered)+255*covered)/4)<<(c*8);
        assert(actual==expected);partial+=covered>0 && covered<4;
    }
    assert(!logo || partial>200);
    /* Other color buffer and city texture pins were never render targets. */
    unsigned other=base ? 0:0x88000/4;
    for(unsigned i=0;i<512*272;i++)assert(pixels[other+i]==0x123456);
    for(unsigned i=0x154000/4;i<title_target/4;i++)assert(pixels[i]==0x123456);
}
int main(void)
{
    gfx_title_smoothing(1);assert(title_target==0x178000 && gfx_title_tiles()==4 && fences==1);
    for(unsigned buffer=0;buffer<=0x88000;buffer+=0x88000)for(int logo=0;logo<2;logo++){reset(buffer);check_tiles(logo);}
    gfx_title_smoothing(0);assert(!title_target && gfx_title_tiles()==1);
    reset(0);gfx_title_tile_begin(0);gfx_title_tile_end(0);assert(!gu_used && !target);
    edram_size=0x154000;gfx_title_smoothing(1);assert(!title_target && gfx_title_tiles()==1);
    puts("title smoothing checks passed");
}
