/* 2D textures for the DC's screen image lists (FUN_8c0b09b4 entries): 'TEX1' files from tools/jgs_tex2d.py (RGBA4444) */
#ifndef JGR_TEX2D_H
#define JGR_TEX2D_H
#include "hud_layout.h"
typedef struct { unsigned short *px; int w, h; } Tex2d;
typedef struct { Tex2d t[8]; int n; unsigned char *data; } Tex2dSet;
int  tex2d_load(Tex2dSet *s, const char *path);
void tex2d_free(Tex2dSet *s);
/* Explicit, non-nested scope containing ONLY tex2d_draw_entry calls.
   End before any other draw, GU state change, or resource release. */
void tex2d_batch_begin(void);
void tex2d_batch_end(void);
/* DC 640x480 screen entry {x, y, w, h, pivot, ARGB, uv rect} scaled to the PSP screen (x 0.75, y 0.5667) */
void tex2d_draw_entry(const Tex2d *t, float x, float y, float w, float h, float pu, float pv, unsigned argb, float u0, float v0, float u1, float v1);
/* Uniformly scaled HUD group; generic backgrounds retain the full canvas. */
static inline void tex2d_draw_hud(const Tex2d *t,HudAnchor anchor,float x,float y,float w,float h,float pu,float pv,unsigned argb,float u0,float v0,float u1,float v1)
{
    if(anchor==HUD_FULL)tex2d_draw_entry(t,x,y,w,h,pu,pv,argb,u0,v0,u1,v1);
    else tex2d_draw_entry(t,hud_x(x,anchor)/.75f,y,w*HUD_X_RATIO,h,pu,pv,argb,u0,v0,u1,v1);
}
#endif
