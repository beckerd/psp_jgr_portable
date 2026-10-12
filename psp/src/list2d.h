/* 2D image lists (DC FUN_8c0b09b4 / FUN_8c0b0ad4): 'L2D1' files from tools/jgs_2dlist.py */
#ifndef LIST2D_H
#define LIST2D_H
#include "tex2d.h"
typedef struct { unsigned n; float x, y, z; int rot; float sx, sy; const unsigned char *e; } List2d;
typedef struct { unsigned n; List2d *l; unsigned char *data; } List2dSet;
int  list2d_load(List2dSet *s, const char *path);
void list2d_free(List2dSet *s);
/* draw list idx with the list's own offset plus (ox, oy); slots index tex->t[]; entries are drawn in file order (DC z sorts per frame) */
void list2d_draw(const List2dSet *s, int idx, const Tex2dSet *tex, float ox, float oy);
void list2d_draw_hud(const List2dSet *,int,const Tex2dSet *,float,float,HudAnchor);
/* draw one entry of a list with a colour override (0 = keep the entry colour) */
void list2d_draw_entry(const List2dSet *s, int idx, int ent, const Tex2dSet *tex, float ox, float oy, unsigned argb);
#endif
