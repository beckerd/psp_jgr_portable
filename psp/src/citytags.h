#ifndef CITYTAGS_H
#define CITYTAGS_H
#include "graffiti.h"
#include "object_graffiti.h"
typedef struct {
    const GfoTemplate **mesh;unsigned count;
    unsigned char *image;
    unsigned short *art[3],*mask;
    int worldspace;unsigned mask_index;
} CityTags;
/* All input data is checked before replacing the currently loaded bank. */
int citytags_bind(CityTags *,const void *,unsigned,const Graffiti *,unsigned stage);
int citytags_load(CityTags *,const Graffiti *,unsigned stage,const unsigned char selected[3]);
int citytags_select_mask(CityTags *,unsigned mission_art);
void citytags_free(CityTags *);
/* Bake static local positions into the owned runtime image, with the same
   rounded transform as draw. No extra vertex allocation or asset edits. */
void citytags_prepare(CityTags *,const Graffiti *);
#include "object_view.h"
#include "tex2d.h"
void citytags_draw(const CityTags *,const Graffiti *);
void citytags_draw_view(const CityTags *,const Graffiti *,const ObjectView *);
/* STAFF's original 095380 bit 8 binds the live credits-card buffers to
   both graffiti texture slots. Borrow them only for this draw. */
void citytags_draw_staff(const CityTags *,const Graffiti *,const Tex2dSet cards[3]);
#endif
