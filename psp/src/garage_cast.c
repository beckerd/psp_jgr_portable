#include <string.h>
#include "charpose.h"
static const Chars *room_cache_bank;static unsigned char room_static_bones[(CHAR_RENDER_MAX_BONES+7)/8]; /* garage room static-bone cache */
#include <math.h>
#include "garage_cast.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_garage_cast.inc"
#else
#include "generated/source_data/garage_cast.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_garage_departure.inc"
#else
#include "generated/source_data/garage_departure.inc"
#endif

int garage_departure_voice(int resident,unsigned tick)
{
    /* 025044 case 4 -> 0211e0(2,program) -> SOSB bank 1. Awards
       have no departure; their table entries are not character voices. */
    return (unsigned)resident<15 && DEPART_FRAMES[resident] &&
        tick==GARAGE_VOICE_TICK[resident] ? (int)GARAGE_VOICE_PROGRAM[resident]:-1;
}
int garage_pet_variant(const unsigned char *save)
{
    /* 049298: the ambient pet is distinct from playable Pots. */
    if(save[0x33a]==1)return -1;
    if(save[0x35b]!=1)return 0;
    return save[0x346]==1 ? 1:-1;
}
int garage_pet_prop_visible(const unsigned char *save)
{ return save[0x35b]==2 && save[0x341]!=1; } /* 0493cc */
static void garage_pet_step(GarageCast *g,const unsigned char *save)
{
    int pet=garage_pet_variant(save);
    if(pet>=0){g->pet_frame[0]+=1.f;if(g->pet_frame[0]>=GARAGE_PET_FRAMES[pet])g->pet_frame[0]-=GARAGE_PET_FRAMES[pet];}
    if(garage_pet_prop_visible(save)){g->pet_frame[1]+=1.f;if(g->pet_frame[1]>=GARAGE_PET_FRAMES[2])g->pet_frame[1]-=GARAGE_PET_FRAMES[2];}
}
int garage_departure_begin(GarageDeparture *d,int resident)
{
    memset(d,0,sizeof *d);
    if((unsigned)resident>=15 || !DEPART_FRAMES[resident] || !DEPART_CAMERA_FRAMES[resident])return 0;
    d->resident=resident;d->phase=1;return 1;
}
void garage_departure_step(GarageDeparture *d,GarageCast *g)
{
    (void)g;
    if(!d->phase || (unsigned)d->resident>=15)return;
    unsigned n=DEPART_CAMERA_FRAMES[d->resident];if(!n)return;
    d->draw_camera=d->frame<n ? (int)d->frame:(int)n-1;
    /* 026234 uses a separate original room clock, with the same source
       test-before-increment policy as ordinary character selection. */
    if(d->room_frame<99.f)d->room_frame+=1.f;else d->room_frame=0;
    if(d->phase==1 || d->phase==3) {
        int first=d->phase==1;d->frame+=1.f;
        float last=(float)(DEPART_FRAMES[d->resident]-1);
        int ended=d->frame>last;if(ended)d->frame=last;
        d->phase=first ? 3:ended ? 4:3;
    }
}
void garage_departure_update(GarageDeparture *d,GarageCast *g,float dt)
{
    if(!isfinite(dt) || dt<0)return;
    d->remainder+=dt;
    while(d->remainder>=1.f/30.f){d->remainder-=1.f/30.f;garage_departure_step(d,g);}
}
int garage_departure_view(const GarageDeparture *d,float *eye,float *target,float *fov,int *roll)
{
    if(!d || !d->phase || (unsigned)d->resident>=15 || !DEPART_CAMERA_FRAMES[d->resident])return 0;
    unsigned k=(unsigned)d->draw_camera,n=DEPART_CAMERA_FRAMES[d->resident];if(k>=n)k=n-1;
    const GarageDepartureKey *key=DEPART_CAMERAS[d->resident]+k;
    memcpy(eye,key->point,12);memcpy(target,key->point+3,12);
    *fov=key->fov*(6.283185307f/65536.f);*roll=key->roll;return 1;
}

static int special_camera(const unsigned char *s)
{
    return s[0x33e]==1 && s[0x32f]==1 && s[0x356]!=1 &&
           !(s[8]|s[9]|s[10]|s[11]);
}
int garage_cast_menu_entry(const unsigned char *save,int menu)
{
    /* 021a4c case 0: returning to STREET selects the photo during the
       Combo/Cube chapter. Preserve other carousel destinations. */
    return menu==1 || menu==2 ? (special_camera(save) ? 1:2):menu;
}
int garage_cast_character(int selection) { return GARAGE_SELECT_ORDER[selection%15]; }
int garage_cast_menu_next(const unsigned char *save,int menu,int direction)
{
    /* 021a4c case 2: order of the two skip tests is significant. */
    int special=special_camera(save);
    menu=(menu+(direction>0 ? 1 : 6))%7;
    if (direction>0) {
        if (special) { if (menu==2) menu=3; }
        else if (menu==1) menu=2;
        if (menu==3) menu=4;
    } else {
        if (menu==3) menu=2;
        if (special) { if (menu==2) menu=1; }
        else if (menu==1) menu=0;
    }
    return menu;
}
void garage_cast_character_view(int selection,float *eye,float *target,float *fov)
{
    memcpy(eye,GARAGE_SELECT_CAMERA[selection%15],12);
    memcpy(target,GARAGE_SELECT_CAMERA[selection%15]+3,12);
    *fov=GARAGE_SELECT_FOV*(6.283185307f/65536.f);
}
void garage_selection_init(GarageSelection *s,int selection)
{
    memset(s,0,sizeof *s);s->current=s->previous=selection;
}
void garage_selection_move(GarageSelection *s,int selection)
{
    if (s->moving || (unsigned)selection>=15) return;
    s->previous=s->current;s->current=selection;s->moving=1;
    s->eye=s->target=0.f;
}
void garage_selection_step(GarageSelection *s,int fast)
{
    if (!s->moving) return;
    /* 025044 case 3, verified in garage_transition_trace.csv. Target is
       updated BEFORE eye. Reaching 60 still draws the final transition
       frame; the following source update resets the counters/state. */
    float step=fast ? 3.f : 1.f;
    if (s->target<15.f && s->eye<15.f) s->target=fminf(15.f,s->target+step);
    if (s->eye<60.f) s->eye=fminf(60.f,s->eye+step);
    else { s->moving=0;s->previous=s->current;s->eye=s->target=0.f; }
}
void garage_selection_update(GarageSelection *s,int fast,float dt)
{
    s->remainder+=dt;
    while (s->remainder>=1.f/30.f) {
        s->remainder-=1.f/30.f;garage_selection_step(s,fast);
    }
}
int garage_selection_display(const GarageSelection *s)
{
    /* 025924 switches the selected actor/name at eye counter 30, not 60. */
    return s->current!=s->previous && s->eye<30.f ? s->previous : s->current;
}
void garage_selection_view(const GarageSelection *s,float *eye,float *target,float *fov)
{
    const float *old=GARAGE_SELECT_CAMERA[s->previous],*next=GARAGE_SELECT_CAMERA[s->current];
    if (s->current==s->previous) { garage_cast_character_view(s->current,eye,target,fov);return; }
    /* 025924 / 025aa8: original single-precision factors, no easing curve. */
    float e=s->eye*0x1.111112p-6f,t=s->target*0x1.111112p-4f;
    for (int k=0;k<3;k++) { eye[k]=(next[k]-old[k])*e+old[k];target[k]=(next[k+3]-old[k+3])*t+old[k+3]; }
    *fov=GARAGE_SELECT_FOV*(6.283185307f/65536.f);
}
int garage_special_character(int selection)
{ return (unsigned)selection<2 ? GARAGE_SPECIAL_CHAR[selection] : -1; }
void garage_special_view(const GarageSelection *s,float *eye,float *target,float *fov)
{
    /* 0f63f0 has the same 60/15 eye/target clocks as 025924, but
       different original camera objects and only two selectable actors. */
    const float *old=GARAGE_SPECIAL_CAMERA[s->previous&1],*next=GARAGE_SPECIAL_CAMERA[s->current&1];
    float e=s->eye*0x1.111112p-6f,t=s->target*0x1.111112p-4f;
    for(int k=0;k<3;k++) {
        eye[k]=(next[k]-old[k])*e+old[k];
        target[k]=(next[k+3]-old[k+3])*t+old[k+3];
    }
    *fov=GARAGE_SELECT_FOV*(6.283185307f/65536.f);
}
void garage_cast_select(GarageCast *g,const unsigned char *save,int menu,int direction)
{
    g->special=special_camera(save);g->menu=menu;
    const float *stops=g->special ? GARAGE_STOPS_1 : GARAGE_STOPS_0;
    if (!direction || !g->started) {
        g->camera=stops[menu];g->camera_mode=1;g->started=1;
    } else {
        /* 022834 / 02284c: rewind from zero starts at the path's end. */
        if (direction<0 && g->camera==stops[0]) g->camera=stops[7];
        g->camera_mode=direction>0 ? 2 : 3;
    }
}
void garage_cast_view(const GarageCast *g,float *eye,float *target,float *fov)
{
    int n=g->special ? 120 : 150,k=(int)g->camera;
    if (k<0) k=0;
    if (k>=n) k=n-1;
    const float *c=g->special ? GARAGE_CAMERA_1[k] : GARAGE_CAMERA_0[k];
    memcpy(eye,c,12);memcpy(target,c+3,12);
    *fov=(g->special ? GARAGE_CAMERA_FOV_1 : GARAGE_CAMERA_FOV_0)*(6.283185307f/65536.f);
}
void garage_cast_reset(GarageCast *g)
{
    unsigned rng=g->rng;memset(g,0,sizeof *g);g->rng=rng;
}
void garage_cast_step(GarageCast *g,const unsigned char *save,unsigned room_frames)
{
    garage_pet_step(g,save);
    if (!g->started) garage_cast_select(g,save,2,0);
    /* 0221b8 advances the ROOM motion, not the menu camera. */
    g->room_frame+=1.f;
    if (room_frames<2 || g->room_frame>=(float)(room_frames-1)) g->room_frame=0.f;
    /* 0228d4: one source camera key per tick, then hold at the menu stop. */
    const float *stops=g->special ? GARAGE_STOPS_1 : GARAGE_STOPS_0;
    if (g->camera_mode==2) {
        g->camera+=1.f;
        float end=stops[g->menu ? g->menu : 7];
        if (g->camera>=end) { g->camera=stops[g->menu];g->camera_mode=1; }
    } else if (g->camera_mode==3) {
        g->camera-=1.f;
        if (g->camera<=stops[g->menu]) { g->camera=stops[g->menu];g->camera_mode=1; }
    }
    for (int i=0;i<15;i++) {
        if (save[0x32c+i]!=1) continue;
        unsigned current=g->motion[i],n=GARAGE_RESIDENTS[i].frames[current];
        g->draw_motion[i]=current;
        if (!n) continue;
        g->frame[i]+=1.f; /* 0bdf30(rate 1, oneshot 1) */
        if (g->frame[i]>(float)(n-1)) {
            /* 013a3c RNG, then strict (rand/32768)<0.75 in 02263c.
               The source draws the OUTGOING clip at frame zero on this tick. */
            g->rng=g->rng*0x41c64e6du+0x3039u;
            g->motion[i]=((g->rng>>16)&0x7fff)>=24576;
            g->frame[i]=0.f;
        }
    }
}
void garage_cast_update(GarageCast *g,const unsigned char *save,unsigned camera_frames,float dt)
{
    g->remainder+=dt;
    while (g->remainder>=1.f/30.f) {
        g->remainder-=1.f/30.f;garage_cast_step(g,save,camera_frames);
    }
}
int garage_selection_visible(const unsigned char *save,int selected,int resident)
{
    if ((unsigned)resident>=15 || save[0x32c+resident]!=1) return 0;
    if (resident==selected) return 1;
    /* 025924: these residents obstruct the selected character's source view.
       Hidden residents do NOT consume animation ticks or random choices. */
    if (selected==0 && save[0x333]==1 && (resident==8 || resident==9)) return 0;
    if (selected==9 && resident==3) return 0;
    return 1;
}
int garage_cast_shadow_visible(const unsigned char *save,int selected,int resident)
{
    if(!save || (unsigned)resident>=15 || !GARAGE_RESIDENTS[resident].frames[0] ||
       save[0x32c+resident]!=1 || selected<GARAGE_SHADOW_ROOM)return 0;
    /* 02611e..02613e: selecting resident 9 draws other residents with a
       NULL secondary tree, except resident 8. The selected resident's own
       path at 025f54..025f6c still supplies its secondary tree. This is NOT
       an animation/primary-tree visibility rule. */
    if(selected==9 && resident!=8 && resident!=9)return 0;
    return selected==GARAGE_SHADOW_ROOM || garage_selection_visible(save,selected,resident);
}
static void selection_resident_step(GarageCast *g,int i,int selected)
{
    unsigned current=g->motion[i],n=GARAGE_RESIDENTS[i].frames[current];
    g->draw_motion[i]=current;
    if (!n) return;
    g->frame[i]+=1.f;
    if (g->frame[i]>(float)(n-1)) {
        g->rng=g->rng*0x41c64e6du+0x3039u;
        g->motion[i]=((g->rng>>16)&0x7fff)>=24576;
        g->frame[i]=0.f;
        /* The selected path retains its local outgoing motion pointer.
           The other-resident path reloads 105d48 AFTER making the choice. */
        if (i!=selected) g->draw_motion[i]=g->motion[i];
    }
}
void garage_selection_cast_step(GarageCast *g,const unsigned char *save,int selected,unsigned room_frames)
{
    garage_pet_step(g,save);
    /* 025924 tests the old ROOM frame before incrementing, unlike 0221b8. */
    if (room_frames>1 && g->room_frame<(float)(room_frames-1)) g->room_frame+=1.f;
    else g->room_frame=0.f;
    if ((unsigned)selected<15 && garage_selection_visible(save,selected,selected))
        selection_resident_step(g,selected,selected);
    for (int i=0;i<15;i++) if (i!=selected && garage_selection_visible(save,selected,i))
        selection_resident_step(g,i,selected);
}
void garage_selection_scene_update(GarageSelection *s,GarageCast *g,const unsigned char *save,int fast,unsigned room_frames,float dt)
{
    /* Advance source update THEN source draw for each tick. Batching all camera
       steps first would change the selected resident/RNG at the halfway tick. */
    s->remainder+=dt;
    while (s->remainder>=1.f/30.f) {
        s->remainder-=1.f/30.f;garage_selection_step(s,fast);
        garage_selection_cast_step(g,save,garage_cast_character(garage_selection_display(s)),room_frames);
    }
}
void garage_special_scene_update(GarageSelection *s,GarageCast *g,const unsigned char *save,unsigned room_frames,float dt)
{
    s->remainder+=dt;
    while(s->remainder>=1.f/30.f) {
        s->remainder-=1.f/30.f;
        garage_selection_step(s,0); /* 0f5eec: no L-trigger acceleration. */
        /* 0f63f0 ROOM tests before increment; 0f6b3c draws every resident
           in slot order and retains each outgoing motion on its loop tick. */
        if(room_frames>1 && g->room_frame<(float)(room_frames-1))g->room_frame+=1.f;
        else g->room_frame=0.f;
        for(int i=0;i<15;i++)if(save[0x32c+i]==1)selection_resident_step(g,i,i);
    }
}
#ifndef JGR_GARAGE_HOST_TEST
#include "garage_shadow.h"
#include <stdio.h>
static GarageShadow resident_shadows;
void garage_cast_frustum(const float planes[6][4])
{chars_world_frustum(planes);garage_shadow_frustum(planes);}
int garage_cast_assets_load(Chars *bank)
{
    int err=chars_load(bank,"garage.jgc");if(err)return err;
    err=garage_shadow_load(&resident_shadows,"garageshadow.bin");
    if(err){chars_free(bank);printf("GARAGE shadow asset load failed: %d\n",err);}
    return err;
}
void garage_cast_assets_free(Chars *bank){garage_shadow_free(&resident_shadows);chars_free(bank);room_cache_bank=NULL;}
void garage_cast_shadows(const GarageCast *g,const unsigned char *save,int selected,Chars *bank,const float view[16])
{
    if(!resident_shadows.header)return;
    garage_shadow_begin();
    for(int i=0;i<15;i++)if(garage_cast_shadow_visible(save,selected,i)) {
        const GarageResident *r=GARAGE_RESIDENTS+i;
        float yaw=r->yaw*(6.283185307f/65536.f),c=cosf(yaw),s=sinf(yaw);
        const float root[16]={c,0,s,r->x,0,1,0,r->y,-s,0,c,r->z,0,0,0,1};
        garage_shadow_resident(&resident_shadows,bank,i,g->draw_motion[i],g->frame[i],root,view,garage_shadow_submit,NULL);
    }
    garage_shadow_end(view);
}
void garage_departure_shadows(const GarageDeparture *d,Chars *bank,const float view[16])
{
    if(!d->phase || !resident_shadows.header)return;
    /* 0264da..026538 deliberately uses placement slot ZERO for every
       confirmed resident. The authored departure camera targets this spot. */
    const GarageResident *r=GARAGE_RESIDENTS;
    float yaw=r->yaw*(6.283185307f/65536.f),c=cosf(yaw),s=sinf(yaw);
    const float root[16]={c,0,s,r->x,0,1,0,r->y,-s,0,c,r->z,0,0,0,1};
    garage_shadow_begin();
    garage_shadow_resident(&resident_shadows,bank,d->resident,2,d->frame,root,view,garage_shadow_submit,NULL);
    garage_shadow_end(view);
}
void garage_departure_draw(const GarageDeparture *d,Chars *bank)
{
    if(!d->phase || (unsigned)d->resident>=15)return;
    const GarageResident *r=GARAGE_RESIDENTS;
    chars_draw_one(bank,GARAGE_RESIDENTS[d->resident].primary,r->x,r->y,r->z,
        r->yaw*(6.283185307f/65536.f),2,d->frame);
}
static void draw_list(const Tex2dSet *tex,int list)
{
    unsigned start=GARAGE_LISTS[list][0],count=GARAGE_LISTS[list][1];
    for (unsigned i=start;i<start+count;i++) {
        const GarageQuad *q=&GARAGE_QUADS[i];
        if (q->slot<(unsigned)tex->n)
            tex2d_draw_entry(&tex->t[q->slot],q->x,q->y,q->w,q->h,0,0,q->color,q->u0,q->v0,q->u1,q->v1);
    }
}
void garage_cast_ui(const Tex2dSet *tex,int character)
{
    draw_list(tex,0);
    if (character>=0 && character<15) {
        draw_list(tex,1);draw_list(tex,2);draw_list(tex,3+character);
    }
}
static void draw_resident(const GarageCast *g,int i,Chars *bank)
{
        const GarageResident *r=&GARAGE_RESIDENTS[i];
        if (!r->frames[0]) {
            /* Slots 10..13 are awards, not cel-shaded skaters. In particular,
               the three award plaques have inverse culling and alpha blending
               (source strip flags 0x0b), not black character outline shells. */
            float yaw=r->yaw*(6.283185307f/65536.f),c=cosf(yaw),s=sinf(yaw);
            float xf[16]={c,0,s,r->x,0,1,0,r->y,-s,0,c,r->z,0,0,0,1};
            chars_draw_effect(bank,r->primary,xf,0.f,1.f);
            return;
        }
        /* The secondary source modifier hierarchy is submitted after room
           receiver depth, before the non-receiving resident colour pass. */
        chars_draw_one(bank,r->primary,r->x,r->y,r->z,r->yaw*(6.283185307f/65536.f),
                       g->draw_motion[i],g->frame[i]);
}
void garage_special_prop_draw(Chars *bank)
{
    /* 0f63f0 draws the authored 800264 NJS object with texture list
       10e2b4; it is not one of the fifteen animated resident trees. */
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    chars_draw_effect(bank,GARAGE_SPECIAL_PROP,identity,0.f,1.f);
}
void garage_pet_draw(const GarageCast *g,const unsigned char *save,Chars *bank)
{
    int pet=garage_pet_variant(save);
    if(pet>=0)chars_draw_one(bank,GARAGE_PET_MODELS[pet],
        GARAGE_PET_POSITION[0]+.2f,GARAGE_PET_POSITION[1],GARAGE_PET_POSITION[2]-4.f,
        GARAGE_PET_YAW*(6.283185307f/65536.f),0,g->pet_frame[0]);
    if(garage_pet_prop_visible(save)) {
        const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        chars_draw_material(bank,GARAGE_PET_MODELS[2],identity,NULL,0,g->pet_frame[1],1.f,NULL);
    }
}
void garage_room_draw_pass(Chars *bank,float frame,int pass,const float planes[6][4])
{
    /* 0221b8 / 025924 / 04bdfc: ordinary Ninja room, not a PLAY actor.
       Strip bit 0 means ignore-light, not reverse-winding outline hull. */
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    /* Both the floor decal (84f840, slot 18) and opaque floor/walls can
       cross behind the selection cameras. Clip BOTH passes before GE's
       coordinate rejection. Fully inside batches retain GE skinning. */
    if(room_cache_bank!=bank) {
        /* Skin the room's never-moving bones once; the room motion only
           animates a few parts. Same lighting, guard codes and clipping. */
        unsigned vertices=0,bones=chars_static_bones(bank,0,0,room_static_bones,&vertices);
        int result=chars_static_cache_prepare(bank,room_static_bones,vertices);
        printf("GARAGE_ROOM_CACHE static_bones=%u vertices=%u bytes=%u result=%d\n",bones,vertices,vertices*36u,result);
        room_cache_bank=bank;
    }
    chars_static_cache_scope(1);
    chars_draw_material_pass_clipped(bank,0,identity,0,frame,pass,planes);
    chars_static_cache_scope(0);
}
void garage_room_draw(Chars *bank,float frame){garage_room_draw_pass(bank,frame,-1,NULL);}
void garage_cast_draw(const GarageCast *g,const unsigned char *save,Chars *bank)
{
    for (int i=0;i<15;i++) if (save[0x32c+i]==1) draw_resident(g,i,bank);
}
void garage_selection_draw(const GarageCast *g,const unsigned char *save,int selected,Chars *bank)
{
    if ((unsigned)selected<15 && garage_selection_visible(save,selected,selected)) draw_resident(g,selected,bank);
    for (int i=0;i<15;i++) if (i!=selected && garage_selection_visible(save,selected,i)) draw_resident(g,i,bank);
}
#endif
