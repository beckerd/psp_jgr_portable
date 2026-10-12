#include "record_book.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>
typedef struct {int model,frames;} BookModel;
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_record_book.inc"
#else
#include "generated/source_data/record_book.inc"
#endif
#define COUNT(a) ((int)(sizeof(a)/sizeof(*(a))))
void record_options_init(RecordOptions *s,const unsigned char *save)
{memset(s,0,sizeof *s);s->sound_mode=save[0x35c]!=0;s->rumble=save[0x35d]!=0;s->sound=-1;}
void record_options_close(const RecordOptions *s,unsigned char *save)
{save[0x35c]=s->sound_mode;save[0x35d]=s->rumble;} /* 090fcc, also parent START deletion */
int record_options_step(RecordOptions *s,unsigned b,int rumble_available,unsigned char *save)
{
    s->sound=-1;
    if(s->phase==0){s->phase=1;s->sound=0;}
    else if(s->phase==2){
        /* 0907cc: A is deliberately inert and takes priority over B. */
        if(b&4)return 0;
        if(b&2){s->phase=3;s->sound=0;}
        else if(b&0xc0){
            if(!s->cursor){s->sound_mode=!s->sound_mode;save[0x35c]=s->sound_mode;}
            else {s->rumble=!s->rumble;save[0x35d]=s->rumble;}
            s->sound=0;
        }else if(!rumble_available)s->cursor=0;
        else if(b&0x30){s->cursor=!s->cursor;s->sound=0;}
    }else if(s->phase==3){record_options_close(s,save);return 1;}
    return 0;
}
void record_options_pose_step(RecordOptions *s)
{if(s->phase==1 && s->frame>=30)s->phase=2;s->frame+=1;}
void record_book_init(RecordBook *s){memset(s,0,sizeof *s);s->sound=-1;}
int record_book_step(RecordBook *s,unsigned b,int child_done)
{
    int route=0;s->sound=-1;
    switch(s->phase){
    case 0:s->fade=1;s->fade_step=-.04f;s->phase=1;s->frame=0;break;
    case 1:if(!s->fade_step)s->phase=4;s->frame=0;break;
    case 2:
        if(b&4){s->phase=s->cursor==0 ? 3:s->cursor==1 ? 7:8;s->sound=1;}
        else if(b&10){s->fade=0;s->fade_step=.04f;s->phase=9;s->sound=3;}
        else if(b&16){s->cursor=(s->cursor+2)%3;s->sound=0;}
        else if(b&32){s->cursor=(s->cursor+1)%3;s->sound=0;}
        break;
    case 3:case 7:case 8:
        if(!s->child){s->child=s->phase==3 ? 1:s->phase==7 ? 2:3;route=s->child;}
        else if(child_done){s->child=0;s->phase=5;s->frame=0;}
        else if(b&8){s->fade=0;s->fade_step=.04f;s->phase=10;s->sound=3;}
        break;
    case 9:if(!s->fade_step)s->phase=11;break;
    case 10:if(!s->fade_step){s->child=0;s->phase=11;}break;
    case 11:route=4;break;
    }
    s->frame+=1;
    if(s->fade_step){s->fade+=s->fade_step;
        if(s->fade<0){s->fade=0;s->fade_step=0;}
        if(s->fade>1){s->fade=1;s->fade_step=0;}}
    return route;
}
void record_book_pose_step(RecordBook *s)
{
    /* 028b94 returns the last animated entry's state, stopping when a
       motion-list cell is null. Both authored entry animations are 30. */
    if((s->phase==4 || s->phase==5) && s->frame>=30)s->phase=2;
}
void record_browser_init(RecordBrowser *s)
{memset(s,0,sizeof *s);s->speed=1;s->sound=-1;}
int record_browser_step(RecordBrowser *s,unsigned b,int expanded)
{
    s->sound=-1;
    switch(s->phase){
    case 0:s->phase=5;break;
    case 1:{
        int old=s->cursor>>2,max=expanded ? 20:12;
        if(b&2){s->phase=4;s->sound=3;}
        else if(b&(16|32)){
            s->speed=1;
            if(b&16){if(--s->cursor<0){s->cursor=max-1;if(expanded)s->speed=2;}}
            else if(++s->cursor==max){s->cursor=0;if(expanded)s->speed=2;}
            s->sound=0;
        }
        if(expanded && (s->cursor>>2)!=old){s->target=RECORD_SCROLL[s->cursor>>2];
            if(s->scroll>s->target)s->phase=2;else if(s->scroll<s->target)s->phase=3;}
        break;}
    case 2:s->scroll-=s->speed;if(s->scroll<=s->target){s->scroll=s->target;s->phase=1;}break;
    case 3:s->scroll+=s->speed;if(s->scroll>=s->target){s->scroll=s->target;s->phase=1;}break;
    case 4:return 1;
    }
    return 0;
}
void record_browser_pose_step(RecordBrowser *s)
{
    if((s->phase==0 || s->phase==5) && s->frame>=30)s->phase=1;
    s->frame+=1;
}
int record_browser_rows(int cursor)
{return cursor<0 || cursor>=20 ? 0:cursor==0 || cursor==4 ? 4:5;}
unsigned record_browser_offset(int cursor,int row)
{return row<0 || row>=record_browser_rows(cursor) ? 0:12+cursor*40+row*8;}
int record_browser_title(int cursor,int row,const unsigned char *save)
{
    if(cursor<0 || cursor>=20 || cursor%4 || row<0 || row>=record_browser_rows(cursor))return -1;
    int mid=cursor==12 ? 22:cursor==16 ? 27:cursor+row+1;
    unsigned loop;memcpy(&loop,save+8,4);
    if(save[0x33b+mid]!=1 && !loop)return -1;
    if(cursor>=12)return row==0 ? (cursor==12 ? 63:64):-1;
    return cursor+row+8;
}
int record_browser_value(int kind,unsigned score,char *out,unsigned capacity)
{
    if(capacity<32)return 0;
    unsigned n=0;
    if(kind==1){
        /* 0901b2: missing time is the source dash. Nonzero times use the
           original LAP prefix and float32 digit extraction, not minutes. */
        if(!score){out[0]=(char)0x81;out[1]=(char)0x5b;out[2]=0;return 1;}
        memcpy(out,"\x82\x6b\x82\x60\x82\x6f",6);n=6;
        float value=(float)score/100000.f;
        for(int k=0;k<13;k+=2){
            if(k==8){out[n++]='.';k--;continue;}
            if(value<1){out[n++]=' ';out[n++]=' ';}
            else {
                if(value>=4294967296.f){out[0]=0;return 0;} /* invalid saved time */
                out[n++]=(char)0x82;out[n++]=(char)(0x4f+(unsigned)value%10);
            }
            value*=10.f;
        }
    }else{
        char digits[16];snprintf(digits,sizeof digits,"%u",score);
        for(const char *p=digits;*p;p++){out[n++]=(char)0x82;out[n++]=(char)(0x4f+*p-'0');}
    }
    out[n]=0;return 1;
}
#ifndef JGR_GARAGE_HOST_TEST
#include "font.h"
#include "gfx.h"
#include "charpose.h"
static void model_draw(Chars *bank,BookModel item,float frame,int animated,float y,float z)
{
    const float xf[16]={1,0,0,0,0,1,0,y,0,0,1,z,0,0,0,1};
    CharPose pose[64];const JgcChar *ch=bank->chars+item.model;
    if(ch->nbones>64)return;
    if(!animated || !item.frames)for(unsigned i=0;i<ch->nbones;i++)chars_sample_pose(bank,ch,-1,0,i,pose+i);
    chars_draw_material(bank,item.model,xf,animated && item.frames ? NULL:pose,0,frame,1,NULL);
}
static void list_draw(Chars *bank,const BookModel *list,int n,float frame,int mode,int selected,int first,int count,float y,float z)
{
    for(int i=first;i<n && i<first+count;i++){
        BookModel item=list[i];
        if(mode==1 && !item.frames)break; /* 028b94 */
        int animated=item.frames && (mode==0 || (mode==1 && frame<item.frames) || ((mode==2 || mode==3) && i==selected));
        model_draw(bank,item,frame,animated,y,z+(mode==3 && animated ? .25f:0));
    }
}
#define DRAW(name,mode,selected,first,count,y) list_draw(bank,name,COUNT(name),s->frame,mode,selected,first,count,y,0)
void record_book_draw(RecordBook *s,Chars *bank)
{
    if(s->phase!=0 && s->phase!=1 && s->phase!=4)DRAW(BOOK_BACK,0,0,0,32,0);
    if(s->phase==2){DRAW(BOOK_STATIC,0,0,0,32,0);DRAW(BOOK_SELECT,2,s->cursor,0,32,0);}
    if(s->phase==4)DRAW(BOOK_OPEN,1,0,0,32,0);
    if(s->phase==5)DRAW(BOOK_RETURN,1,0,0,32,0);
}
void record_options_draw(RecordOptions *s,Chars *bank,int rumble_available)
{
    /* 090cfa/090d9a: fr4=0, fr5=0, fr6=2 passed to 103404.
       This is a +2 Z translation, not Y. Moving Y instead leaves the
       value backplates behind OPTION_BACK while their child text survives.
       Vibration values are conditional on an attached Puru Puru pack.
       PSP has none, so retain its saved preference but omit those values. */
#define OPTION_DRAW(name,mode,selected,frame) list_draw(bank,name,COUNT(name),frame,mode,selected,0,32,0,2)
    if(s->phase==1){OPTION_DRAW(OPTION_OPEN,1,0,s->frame);OPTION_DRAW(OPTION_SOUND_OPEN,1,0,s->frame);OPTION_DRAW(OPTION_VALUE_OPEN,1,0,s->frame);
        if(rumble_available){OPTION_DRAW(OPTION_RUMBLE_OPEN,1,0,s->frame);OPTION_DRAW(OPTION_RUMBLE_VALUE_OPEN,1,0,s->frame);}}
    if(s->phase==2){DRAW(OPTION_BACK,0,0,0,32,0);OPTION_DRAW(OPTION_SELECT,2,s->cursor,s->frame);
        /* 090de8/090e16, 090e56/090e84 pass zero time for the
           inactive setting's motions, not its unanimated bind pose. */
        float sound_frame=s->cursor==0 ? s->frame:0,rumble_frame=s->cursor==1 ? s->frame:0;
        if(s->sound_mode)OPTION_DRAW(OPTION_SOUND1,0,0,sound_frame);else OPTION_DRAW(OPTION_SOUND0,0,0,sound_frame);
        if(rumble_available){if(s->rumble)OPTION_DRAW(OPTION_RUMBLE1,0,0,rumble_frame);else OPTION_DRAW(OPTION_RUMBLE0,0,0,rumble_frame);}}
#undef OPTION_DRAW
}
static void label(float x,float y,float scale,unsigned color,const char *text)
{
    /* 08fda4 uses the original font and a one-DC-pixel shadow. */
    font_real_draw_xy((x+1)*.75f,(y+1)*(272.f/480.f),scale*.75f,scale*(272.f/480.f),0xff000000,text);
    font_real_draw_xy(x*.75f,y*(272.f/480.f),scale*.75f,scale*(272.f/480.f),color,text);
}
static void record_text(const RecordBrowser *s,const unsigned char *save)
{
    int city=s->cursor/4,kind=s->cursor%4;char value[64];
    label(120,90,.8f,0xffffa200,RECORD_TEXT[58+city]);
    snprintf(value,sizeof value,"%u",save[0x3e0+city]);
    label(310-font_real_width(value,.8f)/.75f,90,.8f,0xffffa200,value);
    
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_mixed_record_book_c__place_0.inc"
#else

#include "generated/source_data/record_book_c__place_0.inc"

#endif

    for(int i=0;i<record_browser_rows(s->cursor);i++){
        /* 090108 stops after the sole cleared mission in these cities. */
        if(!kind && city>=3 && i && record_browser_title(s->cursor,0,save)>=0)break;
        unsigned at=record_browser_offset(s->cursor,i),score;memcpy(&score,save+at,4);
        const float *xy=kind ? RECORD_JET_XY+i*2:RECORD_STORY_XY+i*2;
        const float *sc=kind ? RECORD_JET_SCALE+i*4:RECORD_STORY_SCALE;
        unsigned rank=save[at+4];float x=xy[0],y=xy[1];
        if(!kind){int text=record_browser_title(s->cursor,i,save);
            if(text>=0)label(x,y,sc[0],0xffffa200,RECORD_TEXT[text]);}
        else label(x,y,sc[0],0xffffa200,place[i]);
        if(score && rank<6){unsigned c=RECORD_COLOR[rank];c=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);
            label(x+(kind ? (i==0 ? 195:193):195),y+(kind ? -2:0),kind ? (i==0 ? 1:.8f):.8f,c,RECORD_TEXT[52+rank]);}
        char wide[32];record_browser_value(kind,score,wide,sizeof wide);
        const float *off=kind ? RECORD_SCORE_XY+i*2:RECORD_STORY_SCORE_XY;
        float right=x+off[0]+(kind ? RECORD_SCORE_RIGHT[i]:134);
        label(right-font_real_width(wide,sc[2])/.75f+(kind==1 ? 36:0),y+off[1],sc[2],0xff00f0f0,wide);
        if(kind!=1){const float *pts=kind ? RECORD_PTS_XY+i*2:RECORD_STORY_PTS_XY;
            label(x+pts[0],y+pts[1],sc[1],0xff00f0f0,"\x82\x90");
            label(x+pts[0]+12,y+pts[1],sc[1],0xff00f0f0,"\x82\x94");
            label(x+pts[0]+24,y+pts[1],sc[1],0xff00f0f0,"\x82\x93");}
    }
}
void record_browser_draw(RecordBrowser *s,Chars *bank,const unsigned char *save)
{
    if(s->phase==0 || s->phase==5){DRAW(RECORD_OPEN,1,0,0,32,0);return;}
    if(s->phase==4)return;
    int city=s->cursor>>2,expanded=save[0x33e]==1 || save[8] || save[9] || save[10] || save[11];
    DRAW(RECORD_BACK,0,0,0,32,0);
    if(s->cursor==0 || s->cursor==4)DRAW(RECORD_STORY,3,city,0,32,0);
    else DRAW(RECORD_JET,3,city,0,32,0);
    int page=expanded ? RECORD_PAGE[city]:0;
    if(s->phase==2 || s->phase==3){DRAW(RECORD_CITY,3,city,0,32,s->scroll);DRAW(RECORD_KIND,3,s->cursor,0,32,s->scroll);}
    else {DRAW(RECORD_CITY,3,city,page,3,s->scroll);DRAW(RECORD_KIND,3,s->cursor,page*4,12,s->scroll);}
    if(expanded && page>0)model_draw(bank,RECORD_DOWN,s->frame,1,0,0);
    if(expanded && page<2)model_draw(bank,RECORD_UP,s->frame,1,0,0);
    for(int i=0;i<record_browser_rows(s->cursor);i++){
        if(s->cursor>=12 && !(s->cursor%4) && i && record_browser_title(s->cursor,0,save)>=0)break;
        unsigned at=record_browser_offset(s->cursor,i),score;memcpy(&score,save+at,4);
        if(score && save[at+5]<COUNT(RECORD_PORTRAIT)){
            const float xf[16]={.91f,0,0,RECORD_PORTRAIT_XY[i*2],0,.91f,0,RECORD_PORTRAIT_XY[i*2+1],0,0,.91f,0,0,0,0,1};
            chars_draw_material(bank,RECORD_PORTRAIT[save[at+5]].model,xf,NULL,-1,0,1,NULL);
        }
    }
    gfx_mode2d();record_text(s,save);
}
#endif
