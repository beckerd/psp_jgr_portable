#ifndef FONT_H
#define FONT_H

int font_width(const char *s, int scale);
void font_draw(float x, float y, int scale, unsigned int color, const char *s);
void font_draw_center(float cx, float y, int scale, unsigned int color, const char *s);
/* the game's main font (E_FONTMAIN atlas, tools/jgs_font.py -> fontmain.bin): 12x24 cells, cell = character code, 42 per row
   (font descriptor 0x8c1cdca4, glyph UVs FUN_8c0c16a8); drawn at DC size x 0.75 for the PSP screen */
int  font_real_load(const char *path);
int font_real_load_wide(const char *path);
int font_real_wide_ready(void);
void font_real_free_wide(void);
float font_real_width(const char *s, float scale);
int font_real_source_width(const char *s); /* original 640x480 pixels, no PSP scale */
void font_real_draw(float x, float y, float scale, unsigned int color, const char *s);
/* Explicit destination pixels per original font texel, for source 640x480 UI. */
void font_real_draw_xy(float x, float y, float sx, float sy, unsigned int color, const char *s);
void font_real_draw_center(float cx, float y, float scale, unsigned int color, const char *s);

#endif
