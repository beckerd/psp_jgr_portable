#include "ge_submit.h"
#include <string.h>
#include <pspkernel.h>
#include <pspgu.h>
#include <malloc.h>
#include "gfx.h"
#include "font.h"
#include "font_source.h"

/* 5x7 pixel font, glyphs authored as ascii art */

static const char CHARSET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-!^><";

static const char *GLYPHS[][7] = {
    { "..#..", ".#.#.", "#...#", "#...#", "#####", "#...#", "#...#" }, /* A */
    { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." }, /* B */
    { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." }, /* C */
    { "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####." }, /* D */
    { "#####", "#....", "#....", "####.", "#....", "#....", "#####" }, /* E */
    { "#####", "#....", "#....", "####.", "#....", "#....", "#...." }, /* F */
    { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###." }, /* G */
    { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" }, /* H */
    { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####" }, /* I */
    { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." }, /* J */
    { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" }, /* K */
    { "#....", "#....", "#....", "#....", "#....", "#....", "#####" }, /* L */
    { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" }, /* M */
    { "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#" }, /* N */
    { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." }, /* O */
    { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." }, /* P */
    { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" }, /* Q */
    { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" }, /* R */
    { ".####", "#....", "#....", ".###.", "....#", "....#", "####." }, /* S */
    { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." }, /* T */
    { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." }, /* U */
    { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." }, /* V */
    { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#" }, /* W */
    { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" }, /* X */
    { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." }, /* Y */
    { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" }, /* Z */
    { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." }, /* 0 */
    { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", "#####" }, /* 1 */
    { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" }, /* 2 */
    { ".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###." }, /* 3 */
    { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." }, /* 4 */
    { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." }, /* 5 */
    { ".###.", "#....", "####.", "#...#", "#...#", "#...#", ".###." }, /* 6 */
    { "#####", "....#", "...#.", "..#..", "..#..", "..#..", "..#.." }, /* 7 */
    { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." }, /* 8 */
    { ".###.", "#...#", "#...#", ".####", "....#", "....#", ".###." }, /* 9 */
    { ".....", "..#..", "..#..", ".....", "..#..", "..#..", "....." }, /* : */
    { ".....", ".....", ".....", ".....", ".....", ".##..", ".##.." }, /* . */
    { ".....", ".....", ".....", ".###.", ".....", ".....", "....." }, /* - */
    { "..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.." }, /* ! */
    { "..#..", ".###.", "#####", "..#..", "..#..", ".....", "....." }, /* ^ up arrow */
    { "#....", "##...", "###..", "####.", "###..", "##...", "#...." }, /* > right arrow */
    { "....#", "...##", "..###", ".####", "..###", "...##", "....#" }, /* < left arrow */
};

static int glyph_index(char c)
{
    const char *p;
    if (c >= 'a' && c <= 'z')
        c -= 32;
    p = strchr(CHARSET, c);
    if (!p || !c)
        return -1;
    return (int)(p - CHARSET);
}

int font_width(const char *s, int scale)
{
    int n = (int)strlen(s);
    if (n == 0)
        return 0;
    return n * 6 * scale - scale;
}

/* One sprite per glyph. Immutable 128x32 T4 atlas: 2 KiB plus one CLUT
   block, replacing up to 35 independently rasterized sprites per glyph. */
static unsigned char __attribute__((aligned(16))) pixel_atlas[128*32/2];
static const unsigned __attribute__((aligned(16))) pixel_palette[8]={0,0xffffffff};
static int pixel_atlas_ready;
typedef struct {unsigned short u,v;unsigned color;short x,y,z,pad;} PixelGlyph;
static void pixel_font_init(void)
{
    if(pixel_atlas_ready)return;
    for(unsigned glyph=0;glyph<sizeof CHARSET-1;glyph++) {
        unsigned x=(glyph&15)*8,y=(glyph>>4)*8;
        for(unsigned row=0;row<7;row++)for(unsigned col=0;col<5;col++)
            if(GLYPHS[glyph][row][col]=='#') {
                unsigned at=(y+row)*128+x+col;
                pixel_atlas[at>>1]|=1u<<((at&1)*4);
            }
    }
    sceKernelDcacheWritebackRange(pixel_atlas,sizeof pixel_atlas);
    pixel_atlas_ready=1;
}
void font_draw(float x,float y,int scale,unsigned color,const char *s)
{
    if(scale<=0)return;
    unsigned count=0;for(const char *p=s;*p;p++)count+=glyph_index(*p)>=0;
    if(!count)return;
    pixel_font_init();
    PixelGlyph *v=sceGuGetMemory(count*2*sizeof *v);unsigned i=0;
    for(const char *p=s;*p;p++,x+=6*scale) {
        int gi=glyph_index(*p);if(gi<0)continue;
        unsigned u=(gi&15)*8,t=(gi>>4)*8;
        v[i++]=(PixelGlyph){u,t,color,(short)x,(short)y,0,0};
        v[i++]=(PixelGlyph){u+5,t+7,color,(short)(x+5*scale),(short)(y+7*scale),0,0};
    }
    sceGuEnable(GU_TEXTURE_2D);sceGuClutMode(GU_PSM_8888,0,15,0);
    sceGuClutLoad(1,pixel_palette);sceGuTexMode(GU_PSM_T4,0,0,0);
    sceGuTexImage(0,128,32,128,pixel_atlas);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    ge_submit_aux(GU_SPRITES,GU_TEXTURE_16BIT|GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,i,0,v);
    sceGuDisable(GU_TEXTURE_2D);
}

void font_draw_center(float cx, float y, int scale, unsigned int color, const char *s)
{
    font_draw(cx - font_width(s, scale) * 0.5f, y, scale, color, s);
}

/* ---- real font atlas ---- */
typedef struct { float u, v; unsigned color; float x, y, z; } FV;
static unsigned char *g_font; static int g_fw, g_fh, g_cw, g_ch, g_cols; static unsigned int *g_clut;
static unsigned char *g_wide;static unsigned *g_wide_clut;
static int font_load_sheet(const char *path,int wide)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return -1;
    unsigned h[6]; if(sceIoRead(fd,h,24)!=24 || memcmp(h,"FNT2",4) ||
        h[1]!=512 || h[2]!=512 || h[3]!=(wide ? 24:12) || h[4]!=24 || h[5]!=(wide ? 21:42)){sceIoClose(fd);return -2;}
    int n=h[1]*h[2]/2;unsigned char *pixels=memalign(16,n);unsigned *palette=memalign(16,64);
    if(!pixels || !palette){free(pixels);free(palette);sceIoClose(fd);return -3;}
    int ok=sceIoRead(fd,palette,64)==64;
    ok=ok && sceIoRead(fd,pixels,n)==n;sceIoClose(fd);
    if(!ok){free(pixels);free(palette);return -2;}
    gfx_wait_idle();
    if(wide){free(g_wide);free(g_wide_clut);g_wide=pixels;g_wide_clut=palette;}
    else {free(g_font);free(g_clut);g_font=pixels;g_clut=palette;
        g_fw = h[1]; g_fh = h[2]; g_cw = h[3]; g_ch = h[4]; g_cols = h[5];}
    sceKernelDcacheWritebackAll(); return 0;
}
int font_real_load(const char *path){return font_load_sheet(path,0);}
int font_real_load_wide(const char *path){return font_load_sheet(path,1);}
int font_real_wide_ready(void){return g_wide!=NULL;}
void font_real_free_wide(void){gfx_wait_idle();free(g_wide);free(g_wide_clut);g_wide=NULL;g_wide_clut=NULL;}
static void font_bind(int wide)
{
    sceGuEnable(GU_TEXTURE_2D); sceGuClutMode(GU_PSM_8888, 0, 0xff, 0); sceGuClutLoad(2, wide ? g_wide_clut:g_clut);
    sceGuTexMode(GU_PSM_T4, 0, 0, 0); sceGuTexImage(0, g_fw, g_fh, g_fw, wide ? g_wide:g_font);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA); sceGuTexFilter(GU_LINEAR, GU_LINEAR); sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}
float font_real_width(const char *s, float scale) { return (float)strlen(s) * g_cw * 0.75f * scale; }
int font_real_source_width(const char *s) { return (int)strlen(s) * g_cw; }
void font_real_draw(float x, float y, float scale, unsigned int color, const char *s)
{ font_real_draw_xy(x,y,.75f*scale,.75f*scale,color,s); }
void font_real_draw_xy(float x, float y, float sx, float sy, unsigned int color, const char *s)
{
    if (!g_font) { font_draw(x, y, sx < 1.125f ? 1 : 2, color, s); return; }
    int n = (int)strlen(s); if (!n) return;
    FV *v = (FV *)sceGuGetMemory(n * 2 * sizeof(FV)); int i = 0; float cx = x, hh = g_ch * sy;
    int sheet=-1,first=0;
    for (unsigned at=0;at<(unsigned)n;) {
        unsigned bytes,code=font_source_code((const unsigned char *)s+at,n-at,&bytes);
        if(!bytes)break;at+=bytes;
        int u,ty,cw;if(!font_source_cell(code,&u,&ty,&cw)){cx+=(bytes==2 ? 24:12)*sx;continue;}
        int wide=bytes==2;
        if(wide && !g_wide){cx+=24*sx;continue;}
        if(sheet!=wide){if(i>first){font_bind(sheet);ge_submit_aux(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,i-first,0,v+first);}first=i;sheet=wide;}
        float w=cw*sx,u0=u,v0=ty,u1=u0+cw,v1=v0+g_ch;
        v[i].u = u0; v[i].v = v0; v[i].color = color; v[i].x = cx; v[i].y = y; v[i].z = 0;
        v[i + 1].u = u1; v[i + 1].v = v1; v[i + 1].color = color; v[i + 1].x = cx + w; v[i + 1].y = y + hh; v[i + 1].z = 0;
        i += 2; cx += w;
    }
    if(i>first){font_bind(sheet);
        ge_submit_aux(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, i-first, 0, v+first);}
    sceGuDisable(GU_TEXTURE_2D);
}
void font_real_draw_center(float cx, float y, float scale, unsigned int color, const char *s) { font_real_draw(cx - font_real_width(s, scale) * 0.5f, y, scale, color, s); }
