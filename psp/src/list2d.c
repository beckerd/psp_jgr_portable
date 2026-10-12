#include <pspkernel.h>
#include <stdlib.h>
#include <string.h>
#include "list2d.h"
static unsigned rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static float rdf(const unsigned char *p) { unsigned u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
int list2d_load(List2dSet *s, const char *path)
{
    memset(s, 0, sizeof *s);
    SceUID f = sceIoOpen(path, PSP_O_RDONLY, 0777); if (f < 0) return -1;
    int sz = sceIoLseek(f, 0, PSP_SEEK_END); sceIoLseek(f, 0, PSP_SEEK_SET);
    s->data = malloc(sz); if (!s->data) { sceIoClose(f); return -1; }
    sceIoRead(f, s->data, sz); sceIoClose(f);
    if (memcmp(s->data, "L2D1", 4)) { free(s->data); s->data = 0; return -1; }
    s->n = rd32(s->data + 4); s->l = calloc(s->n, sizeof(List2d)); const unsigned char *p = s->data + 8;
    for (unsigned i = 0; i < s->n; i++) {
        List2d *l = &s->l[i]; l->n = rd32(p); l->x = rdf(p + 4); l->y = rdf(p + 8); l->z = rdf(p + 12); l->rot = (int)rd32(p + 16); l->sx = rdf(p + 20); l->sy = rdf(p + 24);
        l->e = p + 28; p += 28 + l->n * 0x40;
    }
    return 0;
}
void list2d_free(List2dSet *s) { free(s->l); free(s->data); memset(s, 0, sizeof *s); }
static void draw_entry(const List2dSet *s, int idx, int ent, const Tex2dSet *tex, float ox, float oy, unsigned argb,HudAnchor anchor)
{
    if (!s || idx < 0 || idx >= (int)s->n) return; const List2d *l = &s->l[idx]; if (ent < 0 || ent >= (int)l->n) return;
    const unsigned char *e = l->e + ent * 0x40; unsigned slot = e[0] | (e[1] << 8);
    if (slot >= (unsigned)tex->n) return;
    float x = rdf(e + 4) * l->sx, y = rdf(e + 8) * l->sy, w = rdf(e + 12) * l->sx, h = rdf(e + 16) * l->sy;
    unsigned col = argb ? argb : rd32(e + 0x20);
    /* the pivot (e+0x18) is the rotation/scale centre, not a position offset: the 640x480 backgrounds sit at (0,0) */
    tex2d_draw_hud(&tex->t[slot],anchor,ox + l->x + x, oy + l->y + y, w, h, 0.f, 0.f, col, rdf(e + 0x30), rdf(e + 0x34), rdf(e + 0x38), rdf(e + 0x3c));
}
void list2d_draw_entry(const List2dSet *s,int idx,int ent,const Tex2dSet *tex,float ox,float oy,unsigned argb)
{draw_entry(s,idx,ent,tex,ox,oy,argb,HUD_FULL);}
void list2d_draw(const List2dSet *s, int idx, const Tex2dSet *tex, float ox, float oy)
{list2d_draw_hud(s,idx,tex,ox,oy,HUD_FULL);}
void list2d_draw_hud(const List2dSet *s,int idx,const Tex2dSet *tex,float ox,float oy,HudAnchor anchor)
{
    if (!s || idx < 0 || idx >= (int)s->n) return;
    tex2d_batch_begin();
    for (unsigned i = 0; i < s->l[idx].n; i++) draw_entry(s,idx,(int)i,tex,ox,oy,0,anchor);
    tex2d_batch_end();
}
