#include "ge_submit.h"
#include <string.h>
#include "garage_ui.h"
#include "garage_cast.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_garage_ui.inc"
#else
#include "generated/source_data/garage_ui.inc"
#endif

int garage_ui_neighbor(const unsigned char *save,int selection,int direction)
{
    if(!save || (unsigned)selection>=15 || !direction)return -1;
    /* 025924 / 024f04: cycle selection order, skipping absent residents. */
    for(int i=0;i<15;i++) {
        selection=(selection+(direction>0 ? 1:14))%15;
        int resident=UI_ORDER[selection];
        if(save[0x32c+resident]==1)return resident;
    }
    return -1;
}
const float *garage_ui_stats(int resident)
{return (unsigned)resident<15 ? UI_STATS[resident]:NULL;}
const char *garage_ui_mission(int entry)
{return (unsigned)entry<24 ? UI_MISSIONS[entry]:NULL;}
const char *garage_ui_chapter(int chapter)
{return (unsigned)chapter<3 ? UI_CHAPTERS[chapter]:NULL;}
const char *garage_ui_menu_prompt(int menu)
{return (unsigned)menu<7 ? UI_MENU_PROMPTS[menu]:NULL;}
int garage_ui_marker(int entry,int selected,float root[16])
{
    if((unsigned)entry>=24)return 0;
    garage_ui_marker_at(UI_MARKS[entry][0],UI_MARKS[entry][1],selected,root);return 1;
}
void garage_ui_marker_at(float x,float y,int selected,float root[16])
{
    /* 0239aa -> 103404/103450: screen-centered translation at z=-500,
       selected marker z+=2 and scale=11.25; ordinary scale=7.5. */
    memset(root,0,16*sizeof *root);
    root[0]=root[5]=root[10]=selected ? 11.25f:7.5f;root[15]=1;
    root[3]=x-320.f;root[7]=240.f-y;
    root[11]=selected ? -498.f:-500.f;
}

#ifndef JGR_GARAGE_HOST_TEST
#include <pspgu.h>
#include "gfx.h"
#include "font.h"
#include "list2d.h"
#define SX .75f
#define SY (272.f/480.f)
static void ui_list(const Tex2dSet *tex,int list,float x,float y,int right)
{
    if(!tex || (unsigned)list>=sizeof UI_LISTS/sizeof *UI_LISTS)return;
    unsigned start=UI_LISTS[list][0],count=UI_LISTS[list][1];
    if(right && count)x+=UI_NEXT_X-UI_QUADS[start].x;
    for(unsigned i=start;i<start+count;i++) {
        const GarageQuad *q=UI_QUADS+i;
        if(q->slot<(unsigned)tex->n)tex2d_draw_entry(tex->t+q->slot,x+q->x,y+q->y,q->w,q->h,0,0,q->color,q->u0,q->v0,q->u1,q->v1);
    }
}
void garage_ui_menu(const Tex2dSet *tex,int menu)
{
    if((unsigned)menu>=7)return;
    ui_list(tex,19+menu,0,0,0);ui_list(tex,26+menu,0,0,0);
    ui_list(tex,33+menu,0,0,0);ui_list(tex,40,0,0,0);
    char prompt[128];strncpy(prompt,UI_MENU_PROMPTS[menu],sizeof prompt-1);prompt[sizeof prompt-1]=0;
    /* Keep the original wording/font with the PSP's confirm button. */
    char *button=strstr(prompt,"\"A\"");if(button)button[1]='X';
    int width=font_real_source_width(prompt);
    font_real_draw_xy(((640-width)/2)*SX,409.f*SY,SX,SY,0xfff0f0f0,prompt);
}
void garage_ui_selection(const Tex2dSet *tex,const unsigned char *save,int selection,int special,int stats)
{
    if((unsigned)selection>=(special ? 2u:15u))return;
    int resident=special ? garage_special_character(selection):UI_ORDER[selection];
    int prev=special ? garage_special_character(1-selection):garage_ui_neighbor(save,selection,-1);
    int next=special ? prev:garage_ui_neighbor(save,selection,1);
    ui_list(tex,prev,0,0,0);ui_list(tex,next,0,0,1);
    if(!stats)return; /* 025924 hides ratings while the camera is moving. */
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_garage_ui_c__colors_0.inc"
#else

#include "generated/source_data/garage_ui_c__colors_0.inc"

#endif

    for(int i=0;i<3;i++) {
        font_real_draw_xy(UI_STAT_POS[i][0]*SX,UI_STAT_POS[i][1]*SY,SX,SY,colors[i],UI_STAT_NAMES[i]);
        gfx_quad(524.f*SX,(301.f+i*32.f)*SY,UI_STATS[resident][i]*SX,18.f*SY,0xff00dc00);
    }
}
static void basic_text(int x,int y,const char *s,const char *glyphs,const void *palette,const unsigned char *pixels)
{
    /* 024006 selects FONTBASIC, then 0c0d54 draws it at scale .75. All
       source texels survive; compacting unused glyph cells saves RAM. */
    typedef struct {float u,v;unsigned color;float x,y,z;} V;
    V *v=sceGuGetMemory(strlen(s)*2*sizeof *v);int n=0;
    for(int i=0;s[i];i++) {
        const char *p=strchr(glyphs,s[i]);if(!p)continue;
        int k=p-glyphs,u=k%21*12,t=k/21*24;
        /* 0c0fb0 truncates each source advance (12*.75 = 9). */
        float left=(x+i*9)*SX,top=y*SY;
        v[n++]=(V){u,t,0xff000000,left,top,0};
        v[n++]=(V){u+12,t+24,0xff000000,left+9*SX,top+18*SY,0};
    }
    sceGuEnable(GU_TEXTURE_2D);sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuClutLoad(2,palette);
    sceGuTexMode(GU_PSM_T4,0,0,0);sceGuTexImage(0,256,64,256,pixels);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    if(n)ge_submit_aux(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,n,0,v);
    sceGuDisable(GU_TEXTURE_2D);
}
static void street_caption(const Tex2dSet *tex,float x,float y,const char *name,const char *prompt,
                            const char *glyphs,const void *palette,const unsigned char *pixels)
{
    int left=x<320.f,lower=y>240.f;
    ui_list(tex,15+(left ? 0:2)+(lower ? 0:1),x,y,0);
    basic_text((int)(x+(left ? 148.f:-148.f)-(int)(strlen(name)*9)/2),
               (int)(y+(lower ? -82.f:82.f)-9.f),name,glyphs,palette,pixels);
    int w=font_real_source_width(prompt);
    font_real_draw_xy(((640-w)/2)*SX,409.f*SY,SX,SY,0xfff0f0f0,prompt);
}
void garage_ui_street(const Tex2dSet *tex,int entry)
{
    if((unsigned)entry>=24)return;
    street_caption(tex,UI_MARKS[entry][0],UI_MARKS[entry][1],UI_MISSIONS[entry],UI_PROMPTS[entry],
                   UI_BASIC_CHARS,UI_BASIC_CLUT,UI_BASIC_PIXELS);
}
void garage_ui_grind(const Tex2dSet *tex,const GrindMap *map,int entry)
{
    if(!map || (unsigned)entry>=9)return;
    List2d title={.n=1,.sx=1,.sy=1,.e=map->title};List2dSet lists={.n=1,.l=&title};
    list2d_draw(&lists,0,tex,0,0);
    const GrindMapEntry *e=map->entry+entry;
    street_caption(tex,e->x,e->y,e->name,e->prompt,map->glyphs,map->palette,map->pixels);
}
#endif
