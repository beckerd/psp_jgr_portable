#include "ge_submit.h"
#include <pspgu.h>
#include <pspgum.h>
#include "hud.h"
#include "tex2d.h"
#include "font.h"
#include "hud_text.h"
#include "rival_counter.h"
#include <stdio.h>
#include <string.h>
typedef struct {int slot;float x,y,w,h,u0,v0,u1,v1;} HudEntry;
#if JGR_PORTABLE_ASSETS
#include "source_data_game_hud_entries.inc"
#else
#include "generated/source_data/game_hud_entries.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_trick_hud_entries.inc"
#else
#include "generated/source_data/trick_hud_entries.inc"
#endif
static Tex2dSet textures;
int hud_draw_rival_counter(const GfoProgress *progress,const float root[16],const float view[16])
{
    int index=rival_counter_index(progress);
    if(index<0 || textures.n!=2)return 0;
    /* 019cfc clears camera rotation, translates (5,-20,0), then
       01b7f4 draws 3x6 world-unit quads from GAME GBIX 0x203. */
    float p[3];for(int k=0;k<3;k++)p[k]=view[k*4]*root[3]+view[k*4+1]*root[7]+view[k*4+2]*root[11]+view[k*4+3];
    if(p[2]>=-1.f)return 0;
    typedef struct {float u,v;unsigned color;float x,y,z;} Vertex;
    Vertex *v=sceGuGetMemory((index==9 ? 8:4)*sizeof *v);
    const Tex2d *t=textures.t+1;
    for(int digit=0;digit<(index==9 ? 2:1);digit++) {
        int cell=digit ? 0:index;
        float u=(cell%4)*.25f,w=(cell/4)*.32f;
        for(int j=0;j<4;j++)v[digit*4+j]=(Vertex){u+(j&1)*.25f,w+(j>>1)*.32f,
            0xffc0c0c0,p[0]+5.f+(j&1)*3.f-digit*3.f,p[1]+20.f-(j>>1)*6.f,p[2]};
    }
    ScePspFMatrix4 identity={0},matrix;float *a=(float *)&identity;for(int j=0;j<4;j++)a[j*5]=1;
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGuSetMatrix(GU_MODEL,&identity);sceGuSetMatrix(GU_VIEW,&identity);
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_4444,0,0,0);sceGuTexImage(0,t->w,t->h,t->w,t->px);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    sceGuEnable(GU_DEPTH_TEST);sceGuDepthMask(GU_TRUE);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    for(int digit=0;digit<(index==9 ? 2:1);digit++)
        ge_submit_aux(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,4,0,v+digit*4);
    a=(float *)&matrix;for(int r=0;r<4;r++)for(int c=0;c<4;c++)a[c*4+r]=view[r*4+c];sceGuSetMatrix(GU_VIEW,&matrix);
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);sceGuEnable(GU_CULL_FACE);sceGuDisable(GU_TEXTURE_2D);
    return index==9 ? 2:1;
}
int hud_load(const char *path)
{
    tex2d_free(&textures);
    /* This bank has exactly two complete textures. The shared legacy
       TEX1 loader alone does not validate every payload boundary. */
    FILE *f=fopen(path,"rb");if(!f)return -1;
    unsigned header[2];long end=0;int ok=fread(header,1,8,f)==8 && !memcmp(header,"TEX1",4) && header[1]==2;
    if(ok){ok=!fseek(f,0,SEEK_END);end=ftell(f);ok=ok && end>=8 && !fseek(f,8,SEEK_SET);}
    for(int i=0;ok && i<2;i++) {
        unsigned short dim[2];ok=fread(dim,1,4,f)==4 && dim[0] && dim[1] && dim[0]<=512 && dim[1]<=512;
        if(ok){long bytes=(long)dim[0]*dim[1]*2;ok=bytes<=end-ftell(f) && !fseek(f,bytes,SEEK_CUR);}
    }
    ok=ok && ftell(f)==end;fclose(f);if(!ok)return -1;
    if(tex2d_load(&textures,path) || textures.n!=2){tex2d_free(&textures);return -1;}
    return 0;
}
static void sprite(const HudEntry *e,float x,float y,float scale,unsigned color,HudAnchor anchor)
{
    if(textures.n!=2)return;
    /* 0b0c58 cancels the rotation pivot at zero rotation. Entries here
       all have zero rotation: x/y are top-left, not sprite centres. */
    tex2d_draw_hud(textures.t+e->slot,anchor,x+e->x*scale,y+e->y*scale,e->w*scale,e->h*scale,
        0,0,color,e->u0,e->v0,e->u1,e->v1);
}
static void entry(int id,float x,float y,float scale,unsigned color,HudAnchor anchor)
{if((unsigned)id<37)sprite(HUD_ENTRIES+id,x,y,scale,color,anchor);}
static float trick_digits(unsigned value,float x,float y)
{
    /* 04f8e8, font 1: left aligned; font 0 total-score layout differs. */
    unsigned char digits[10];int n=0;
    do {digits[n++]=value%10;value/=10;}while(value);
    tex2d_batch_begin();
    while(n){entry(16+digits[--n],x,y,1,0xffffffff,HUD_LEFT);x+=HUD_ENTRIES[16].w;}
    tex2d_batch_end();
    return x;
}
void hud_draw_tricks(const TrickHud *h)
{
    /* 0b375c appends new children. Task IDs are reused, so sorting by
       ID alone would put an older overlapping number over a newer one. */
    unsigned previous=0;
    for(int draw=0;draw<15;draw++){
        const TrickTask *t=NULL;
        for(int i=0;i<15;i++){
            const TrickTask *q=&h->task[i];
            if(q->type && !q->deleted && q->order>previous && (!t || q->order<t->order))t=q;
        }
        if(!t)break;
        previous=t->order;
        if(t->type==2){
            float x=trick_digits(t->value,t->x,t->y);
            if(t->kind>1){
                for(unsigned i=0;i<sizeof TRICK_MULTIPLY/sizeof *TRICK_MULTIPLY;i++)sprite(TRICK_MULTIPLY+i,x,t->y,1,0xffffffff,HUD_LEFT);
                trick_digits(t->kind,x+TRICK_MULTIPLY[0].w,t->y);
            }
        }else{
            char bonus[100];const char *text;
            if(t->type==3){trick_hud_bonus_text(bonus,t->value);text=bonus;}
            else text=trick_hud_name(t->kind,t->value);
            if(text)font_real_draw_xy(hud_x((int)t->x,HUD_LEFT),hud_y((int)t->y),HUD_SCALE,HUD_SCALE,
                t->type==3 ? 0xff009cf7:0xffffffff,text);
        }
    }
}
/* 04fb64 type 1, sign 1: GAME entry header 8c9ca65c, reached through
   182a6c -> 182a54. This is already in the yellow-number atlas. */
static const HudEntry SPRAY_SCORE_PLUS={1,0,0,32,40,.5f,.625f,.75f,.9375f};
static void spray_score_number(unsigned value,float x,float y,int right)
{
    unsigned char digits[10];int n=0;
    do {digits[n++]=value%10;value/=10;}while(value);
    if(right) {
        /* 04f9ac subtracts the zero glyph's width before each digit,
           including the narrower 4 and 8 glyphs. */
        for(int i=0;i<n;i++) {
            x-=HUD_ENTRIES[16].w;
            entry(16+digits[i],x,y,1,0xffffffff,HUD_LEFT);
        }
    } else while(n) {
        entry(16+digits[--n],x,y,1,0xffffffff,HUD_LEFT);
        x+=HUD_ENTRIES[16].w;
    }
}
void hud_draw_spray_score(const SprayScore *h)
{
    if(!h || textures.n!=2)return;
    if((h->deleted || !(h->settled+h->pending)) && !spray_score_flights_alive(h))return;
    tex2d_batch_begin();
    if(!h->deleted) {
        int rows=h->settled+h->pending;if(rows>10)rows=10;
        for(int i=0;i<rows;i++) {
            float y=96.f+32.f*i;
            spray_score_number((unsigned)h->base[i],200.f,y,1);
            if(i>=h->settled && h->bonus[i]) {
                sprite(&SPRAY_SCORE_PLUS,200.f,y,1,0xffffffff,HUD_LEFT);
                spray_score_number((unsigned)h->bonus[i],200.f+SPRAY_SCORE_PLUS.w,y,0);
            }
        }
    }
    /* Original children are created in cue order 1,2,0. Draw-only: the
       flight and score clocks advance in gameplay, never in rendering. */
    static const unsigned order[3]={1,2,0};
    for(int i=0;i<3;i++) {
        const SprayScoreFlight *f=h->flight+order[i];
        if(!f->alive)continue;
        float fraction=f->life/20.f;
        if(fraction<0)fraction=0;else if(fraction>1)fraction=1;
        /* Preserve the source trajectory while moving its PSP anchor
           from the centred cue to the left tally. This avoids a jump
           when the child reaches the persistent row. */
        float offset=(hud_x(0,HUD_CENTRE)-hud_x(0,HUD_LEFT))*fraction/HUD_SCALE;
        spray_score_number((unsigned)f->amount,f->x+offset,f->y,1);
    }
    tex2d_batch_end();
}
static void glyphs(const HudGlyph *g,int n,HudAnchor anchor)
{tex2d_batch_begin();for(int i=0;i<n;i++)entry(g[i].entry,g[i].x,g[i].y,g[i].scale,0xffffffff,anchor);tex2d_batch_end();}
static void bar(int value,unsigned argb)
{
    typedef struct {unsigned color;float x,y,z;} Vertex;
    float xy[8];health_hud_bar(value,xy);
    unsigned color=(argb&0xff00ff00)|((argb>>16)&255)|((argb&255)<<16);
    Vertex *v=sceGuGetMemory(4*sizeof *v);
    for(int i=0;i<4;i++)v[i]=(Vertex){color,hud_x(xy[i*2],HUD_LEFT),hud_y(xy[i*2+1]),0};
    sceGuDisable(GU_TEXTURE_2D);
    ge_submit_aux(GU_TRIANGLE_STRIP,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,4,0,v);
}
void hud_draw_timer(const TimerHud *h,unsigned flags)
{
    if(flags&HUD_HIDE_TIME)return;
    float x,y;HudGlyph glyph[16];timer_hud_position(h,&x,&y);
    int n=hud_timer_glyphs(h->ticks,x,y,1,glyph);glyphs(glyph,n,h->mode==2 ? HUD_CENTRE:HUD_RIGHT);
}
void hud_draw_game(const HealthHud *health,const CanHud *can,unsigned flags,int score,int ticks,float (*sine)(int))
{
    HudGlyph g[16];int n;
    if(!(flags&HUD_HIDE_HEALTH)) {
        /* These atlas pieces overlap at fractional screen coordinates.
           Keep their draw boundaries: merging changes edge blending. */
        for(int i=0;i<4;i++)entry(i,0,0,1,0xffffffff,HUD_LEFT);
        bar(health->maximum,0xff25273b);bar(health->displayed,health_hud_color(health,sine));
    }
    if(!(flags&HUD_HIDE_SCORE)){n=hud_score_glyphs(score,200,65,1,g);glyphs(g,n,HUD_LEFT);}
    if(!(flags&HUD_HIDE_TIME)){n=hud_timer_glyphs(ticks,600,32,1,g);glyphs(g,n,HUD_RIGHT);}
    if(!(flags&HUD_HIDE_CANS)) {
        /* Original 04e1e0 reads displayed inventory, not the live actor.
           Full inventory uses solid red (0c0d54 supplies scale 1, not a
           blink flag); the under-capacity tint is queued by 04e04c. */
        int cans=can->displayed;if(cans<0)cans=0;
        if(cans<10){tex2d_batch_begin();for(int i=0;i<cans;i++)entry(5,50+(i%5)*18,448-28*(1+i/5),1,0xffffffff,HUD_LEFT);tex2d_batch_end();}
        else {
            entry(4,50,397,1,0xffffffff,HUD_LEFT);
            /* 04e1e0 -> 02f394 -> 0c0d48: ordinary text, not the
               stylised score font. 04e04c grades white to red. */
            unsigned tint=can->full ? 0xffff0000u:can->color;
            unsigned abgr=(tint&0xff00ff00)|((tint>>16)&255)|((tint&255)<<16);
            char count[24];hud_number(count,cans);
            font_real_draw_xy(hud_x(84,HUD_LEFT),hud_y(424),HUD_SCALE,HUD_SCALE,abgr,count);
        }
    }
}
void hud_draw_notice(const HudNotice *h)
{
    if(!h->visible)return;
    char number[24],text[128];hud_number(number,h->id);
    snprintf(text,sizeof text,HUD_NOTICE_TEXT[0],number);
    float x=hud_x(hud_notice_x(h->draw_clock),HUD_RIGHT);
    /* 04e1e0: half horizontal scale, full height; yellow, no backdrop. */
    font_real_draw_xy(x,hud_y(400),HUD_SCALE*.5f,HUD_SCALE,0xff00ffffu,text);
    font_real_draw_xy(x+(int)(strlen(text)*6)*HUD_SCALE,hud_y(400),HUD_SCALE,HUD_SCALE,0xff00ffffu,HUD_NOTICE_TEXT[1]);
}
typedef struct {unsigned flags,color;float x,y,w,h,u0,v0,u1,v1;} MessageEntry;
#if JGR_PORTABLE_ASSETS
#include "source_data_message_hud_entries.inc"
#else
#include "generated/source_data/message_hud_entries.inc"
#endif
static void message_list(unsigned list)
{
    if(textures.n!=2)return;
    tex2d_batch_begin();
    for(unsigned i=MESSAGE_RANGES[list];i<MESSAGE_RANGES[list+1];i++){
        const MessageEntry *e=MESSAGE_ENTRIES+i;
        if(e->y+e->h<0 || e->y>480)continue;
        tex2d_draw_hud(textures.t,HUD_CENTRE,e->x,e->y,e->w,e->h,0,0,e->color,
            e->flags&16 ? e->u1:e->u0,e->flags&8 ? e->v1:e->v0,
            e->flags&16 ? e->u0:e->u1,e->flags&8 ? e->v0:e->v1);
    }
    tex2d_batch_end();
}
void hud_draw_message(const MessageHud *h)
{
    if(!h->visible)return;
    if(h->phase==4){if(h->alert%15<8)message_list(2);message_list(1);}
    else if(h->phase>=5 && h->phase<=7){
        message_list(0);message_list(1);message_list(2);
        /* 035e18 -> 27d07a uses PVR's inclusive 32-pixel tile rectangle. */
        /* PSPGU takes width/height, not right/bottom coordinates. */
        int left=(int)hud_x(192,HUD_CENTRE),right=(int)hud_x(480,HUD_CENTRE);
        sceGuScissor(left,0,right-left,272);
        font_real_draw_xy(hud_x((int)h->x,HUD_CENTRE),hud_y((int)h->y),HUD_SCALE,HUD_SCALE,0xffffffff,h->text);
        sceGuScissor(0,0,480,272);
    }
}
