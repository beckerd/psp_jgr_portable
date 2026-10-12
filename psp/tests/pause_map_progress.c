/* Source-only progression tests. Map artwork/text storage and drawing are
   inert: these tests exercise production selection and objective visibility. */
#include <assert.h>
#include "../src/pause_map.h"
#define JGR_SOURCE_DATA_PTR_PAUSE_MAP_ENTRIES_INC
static PauseMapEntry PAUSE_MAPS[6][6];
static unsigned PAUSE_MAP_COUNTS[6];
static int PAUSE_MAP_ICONS[9];
static const char *PAUSE_MAP_TEXT[4];
static char PAUSE_MAP_DIGITS[11][3];
static float PAUSE_MAP_TEXT_XY[4];
#include "../src/pause_map.c"

int chars_load(Chars *c,const char *p){(void)c;(void)p;return -1;}
void chars_free(Chars *c){(void)c;}
void chars_scene_lighting(const CharLight *a,const CharLight *b){(void)a;(void)b;}
void chars_hud_depth(float a,float b){(void)a;(void)b;}
void chars_draw_material(Chars *c,int i,const float *m,const struct CharPose *p,
                         int motion,float f,float alpha,const unsigned short *t)
{(void)c;(void)i;(void)m;(void)p;(void)motion;(void)f;(void)alpha;(void)t;}
void font_real_draw_xy(float x,float y,float sx,float sy,unsigned c,const char *s)
{(void)x;(void)y;(void)sx;(void)sy;(void)c;(void)s;}

static void check(PauseMap *p,int area,const Graffiti *g,int region,int layout,int visible)
{
    pause_map_select(p,area,g);
    assert(p->region==region);
    assert(p->layout==layout);
    if(!g)return;
    int count=0;
    for(int i=0;i<g->n;i++){
        const Spot *s=g->spots+i;
        count+=pause_map_tag_visible(p,s->area,s->active && s->required,s->tagged || s->complete);
    }
    assert(count==visible);
}

static void progression(int stage,int chapter,int sections)
{
    /* Two synthetic targets per section, plus optional/inactive graffiti.
       Visit every completion combination and every section in that state. */
    Spot spots[12]={0};
    Graffiti g={.spots=spots,.n=2*sections+2};
    PauseMap p={.stage=stage,.chapter=chapter,.kind=0};
    int overview=stage==2 ? 5:4;
    for(int mask=0;mask<(1<<sections);mask++){
        int remaining=0;
        for(int i=0;i<2*sections;i++){
            spots[i]=(Spot){.area=i/2+1,.active=1,.required=1,
                .tagged=!!(mask&(1<<(i/2)))};
            remaining+=!spots[i].tagged;
        }
        for(int area=1;area<=sections;area++){
            spots[2*sections]=(Spot){.area=area,.active=1};
            spots[2*sections+1]=(Spot){.area=area,.required=1};
            int cleared=!!(mask&(1<<(area-1)));
            int region=cleared && remaining ? overview:area;
            check(&p,area,&g,region,region,cleared ? remaining:2);
            /* Entering an unfinished area must restore its local map even
               when the same PauseMap last displayed the overview. */
            for(int next=1;next<=sections;next++)if(!(mask&(1<<(next-1))))
                check(&p,next,&g,next,next,2);
            if(cleared){
                /* One outstanding stroke keeps the local map. Its visual
                   completion must work before the tagged counter updates. */
                Spot *last=&spots[(area-1)*2];
                last->tagged=0;
                check(&p,area,&g,area,area,1);
                last->complete=1;
                check(&p,area,&g,region,region,remaining);
                last->complete=0;last->tagged=1;
            }
        }
        /* Shibuya's connector already uses the overview; Benten's empty
           connector uses it only while other targets remain. */
        if(stage==0)check(&p,0,&g,overview,overview,remaining);
        if(stage==2)check(&p,0,&g,remaining ? overview:0,remaining ? overview:0,remaining);
    }
    /* Retrying resets progress without retaining a cleared-area latch. */
    for(int i=0;i<2*sections;i++)spots[i].tagged=spots[i].complete=0;
    check(&p,1,&g,1,1,2);
}

static void scope(void)
{
    Spot spots[2]={{.area=1,.active=1,.required=1,.tagged=1},
                   {.area=2,.active=1,.required=1}};
    Graffiti g={.spots=spots,.n=2};
    PauseMap p={.stage=0,.chapter=4,.kind=0};
    check(&p,1,&g,4,4,1);
    /* Optional/inactive targets elsewhere must not trigger an overview. */
    spots[1].required=0;check(&p,1,&g,1,1,0);
    spots[1].required=1;spots[1].active=0;check(&p,1,&g,1,1,0);
    spots[1].active=1;
    check(&p,1,NULL,1,1,0);
    p.kind=1;check(&p,1,&g,1,1,0);
    p.kind=8;check(&p,1,&g,4,4,1);
    p.kind=0;p.chapter=2;check(&p,1,&g,1,3,0);
    p.chapter=32;check(&p,1,&g,1,1,0);
    p.stage=1;p.chapter=4;check(&p,1,&g,1,1,0);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    switch(argv[1][0]){
    case 'e':progression(0,4,3);break; /* Explosion */
    case 'f':progression(1,8,3);break; /* Fight or Flight */
    case 'b':progression(2,12,4);break; /* Benten Burning */
    case 's':scope();break;
    default:assert(0);
    }
    return 0;
}
