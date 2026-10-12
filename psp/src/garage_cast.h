#ifndef GARAGE_CAST_H
#define GARAGE_CAST_H
#include "charanim.h"
#include "tex2d.h"
typedef struct {
    unsigned slot,color;
    float x,y,w,h,u0,v0,u1,v1;
} GarageQuad;
typedef struct {
    int primary,secondary;
    float x,y,z;int yaw;unsigned frames[2];
} GarageResident;
typedef struct {
    unsigned rng;
    float remainder,room_frame,camera;
    int camera_mode,menu,special,started;
    unsigned char motion[15],draw_motion[15];
    float frame[15];
    float pet_frame[2]; /* 049298 dog and 0493cc story prop use separate clocks */
} GarageCast;
typedef struct {
    int current,previous,moving;
    float eye,target,remainder;
} GarageSelection;
typedef struct {float point[6];int roll,fov;} GarageDepartureKey;
typedef struct {
    int resident,phase,draw_camera;
    float frame,remainder,room_frame;
} GarageDeparture;
/* 026234 draw kernel. Phase 1 -> 3 -> 4; the camera samples the old
   motion clock before the selected primary AND modifier trees advance. */
int garage_departure_begin(GarageDeparture *,int resident);
int garage_departure_voice(int resident,unsigned tick);
int garage_pet_variant(const unsigned char *save); /* -1 absent, 0 normal, 1 returned */
int garage_pet_prop_visible(const unsigned char *save);
void garage_pet_draw(const GarageCast *,const unsigned char *save,Chars *bank);
void garage_departure_step(GarageDeparture *,GarageCast *);
void garage_departure_update(GarageDeparture *,GarageCast *,float dt);
int garage_departure_view(const GarageDeparture *,float *eye,float *target,float *fov,int *roll);
void garage_departure_shadows(const GarageDeparture *,Chars *,const float view[16]);
void garage_departure_draw(const GarageDeparture *,Chars *);
void garage_selection_init(GarageSelection *s,int selection);
void garage_selection_move(GarageSelection *s,int selection);
void garage_selection_step(GarageSelection *s,int fast);
void garage_selection_update(GarageSelection *s,int fast,float dt);
int garage_selection_display(const GarageSelection *s);
void garage_selection_view(const GarageSelection *s,float *eye,float *target,float *fov);
int garage_selection_visible(const unsigned char *save,int selected,int resident);
void garage_selection_cast_step(GarageCast *g,const unsigned char *save,int selected,unsigned room_frames);
void garage_selection_scene_update(GarageSelection *s,GarageCast *g,const unsigned char *save,int fast,unsigned room_frames,float dt);
void garage_selection_draw(const GarageCast *g,const unsigned char *save,int selected,Chars *bank);
int garage_special_character(int selection);
void garage_special_view(const GarageSelection *s,float *eye,float *target,float *fov);
void garage_special_scene_update(GarageSelection *s,GarageCast *g,const unsigned char *save,unsigned room_frames,float dt);
void garage_special_prop_draw(Chars *bank);
void garage_room_draw(Chars *bank,float frame);
void garage_room_draw_pass(Chars *bank,float frame,int pass,const float planes[6][4]);
void garage_cast_reset(GarageCast *g); /* preserves this controller's RNG */
void garage_cast_step(GarageCast *g,const unsigned char *save,unsigned room_frames);
void garage_cast_update(GarageCast *g,const unsigned char *save,unsigned room_frames,float dt);
void garage_cast_select(GarageCast *g,const unsigned char *save,int menu,int direction);
int garage_cast_menu_next(const unsigned char *save,int menu,int direction);
int garage_cast_menu_entry(const unsigned char *save,int menu);
void garage_cast_view(const GarageCast *g,float *eye,float *target,float *fov);
int garage_cast_character(int selection);
void garage_cast_character_view(int selection,float *eye,float *target,float *fov);
void garage_cast_frustum(const float planes[6][4]);
void garage_cast_draw(const GarageCast *g,const unsigned char *save,Chars *bank);
int garage_cast_assets_load(Chars *bank);
void garage_cast_assets_free(Chars *bank);
enum { GARAGE_SHADOW_ROOM=-1, GARAGE_SHADOW_INVITATION=-2 };
/* Original modifier ownership: room/special residents, ordinary selection
   exceptions, and no invitation modifiers (04bdfc). Awards have no volume. */
int garage_cast_shadow_visible(const unsigned char *save,int selected,int resident);
void garage_cast_shadows(const GarageCast *,const unsigned char *save,int selected,Chars *bank,const float view[16]);
void garage_cast_ui(const Tex2dSet *tex,int character); /* -1: menu bars only */
#endif
