/* Four screen quadrants share one 480x272 render target. Each is resolved
   to 240x136, giving four samples per screen pixel without a full-size
   supersampled framebuffer. The stage allocator excludes this 544 KiB
   only during the title replay; gameplay gets its entire texture cache.

   Enlarge the existing background with nearest sampling before drawing
   the logo. The linear resolve then leaves uncovered background pixels
   unchanged and preserves the logo's translucent panel/texture alpha.
   No stencil-as-alpha approximation or separately blended outline. */
#define TITLE_TARGET_BYTES (BUF_WIDTH*SCR_H*4u)
#ifndef JGR_TITLE_SUPERSAMPLE
#define JGR_TITLE_SUPERSAMPLE 1
#endif
void gfx_title_smoothing(int enabled)
{
    gfx_wait_idle();
    unsigned size=sceGeEdramGetSize();
    unsigned first=BUF_WIDTH*SCR_H*10u;
    title_target=enabled && JGR_TITLE_SUPERSAMPLE && size>=first+TITLE_TARGET_BYTES ?
        (size-TITLE_TARGET_BYTES)&~8191u:0;
}
unsigned gfx_title_tiles(void){return title_target ? 4:1;}

typedef struct {short u,v,x,y,z;} TitleBlitVertex;
static void title_blit(unsigned texture,int sx,int sy,int sw,int sh,
                       int dx,int dy,int dw,int dh,int filter)
{
    gfx_mode2d();
    sceGuDisable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_STENCIL_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888,0,0,0);
    /* Only rows 0..271 are addressed. Linear textures require a power of
       two declaration, not storage for unused rows; the last sampled
       8-row cache block ends at row 271, inside the reserved target. */
    sceGuTexImage(0,512,512,BUF_WIDTH,(void *)((uintptr_t)sceGeEdramGetAddr()+texture));
    sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);
    sceGuTexFilter(filter,filter);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    sceGuTexScale(1.f,1.f);sceGuTexOffset(0.f,0.f);
    sceGuTexSync();sceGuTexFlush();
    /* Short strips keep the linear render target within the texture cache. */
    int strips=(dw+31)/32;
    TitleBlitVertex *v=sceGuGetMemory(strips*2*sizeof *v);
    for(int i=0;i<strips;i++) {
        int x=i*32,end=x+32<dw ? x+32:dw;
        v[2*i]=(TitleBlitVertex){sx+x*sw/dw,sy,dx+x,dy,0};
        v[2*i+1]=(TitleBlitVertex){sx+end*sw/dw,sy+sh,dx+end,dy+dh,0};
    }
    ge_submit_aux(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,strips*2,NULL,v);
}
void gfx_title_tile_begin(unsigned tile)
{
    if(!title_target){sceGuClear(GU_DEPTH_BUFFER_BIT);return;}
    unsigned x=tile&1,y=tile>>1;
    sceGuTexSync();
    sceGuDrawBufferList(GU_PSM_8888,(void *)(uintptr_t)title_target,BUF_WIDTH);
    title_blit((unsigned)(uintptr_t)draw_buffer,x*(SCR_W/2),y*(SCR_H/2),SCR_W/2,SCR_H/2,
               0,0,SCR_W,SCR_H,GU_NEAREST);
    gfx_mode3d();sceGuClear(GU_DEPTH_BUFFER_BIT);
    sceGuOffset(2048-SCR_W+x*SCR_W,2048-SCR_H+y*SCR_H);
    sceGuViewport(2048,2048,SCR_W*2,SCR_H*2);
}
void gfx_title_tile_end(unsigned tile)
{
    if(!title_target)return;
    sceGuTexSync();
    sceGuDrawBufferList(GU_PSM_8888,draw_buffer,BUF_WIDTH);
    sceGuOffset(2048-SCR_W/2,2048-SCR_H/2);
    sceGuViewport(2048,2048,SCR_W,SCR_H);
    title_blit(title_target,0,0,SCR_W,SCR_H,
               (tile&1)*(SCR_W/2),(tile>>1)*(SCR_H/2),SCR_W/2,SCR_H/2,GU_LINEAR);
    gfx_mode3d();
}
