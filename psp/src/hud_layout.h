#ifndef JGR_HUD_LAYOUT_H
#define JGR_HUD_LAYOUT_H
/* HUD coordinates use uniform height-based scale, then an edge
   translation. Explicit anchors keep each multipart JGR task together. */
typedef enum {HUD_LEFT,HUD_CENTRE,HUD_RIGHT,HUD_FULL} HudAnchor;
#define HUD_SCALE (272.f/480.f)
#define HUD_ASPECT (480.f/272.f)
#define HUD_X_RATIO ((640.f/480.f)/HUD_ASPECT)
static inline float hud_x(float x,HudAnchor anchor)
{
    if(anchor==HUD_FULL)return x*(480.f/640.f);
    float spare=480.f-640.f*HUD_SCALE;
    return x*HUD_SCALE+(anchor==HUD_LEFT ? 0:anchor==HUD_RIGHT ? spare:spare*.5f);
}
static inline float hud_y(float y){return y*HUD_SCALE;}
/* Input is a source 640x480 perspective matrix; preserve source Y/depth.
   Row-major X clip coefficients are scaled, then translated by clip W. */
static inline void hud_projection(float p[16],HudAnchor anchor)
{
    if(anchor==HUD_FULL)return;
    float shift=anchor==HUD_LEFT ? HUD_X_RATIO-1:anchor==HUD_RIGHT ? 1-HUD_X_RATIO:0;
    for(int k=0;k<4;k++)p[k]=p[k]*HUD_X_RATIO+shift*p[12+k];
}
#endif
