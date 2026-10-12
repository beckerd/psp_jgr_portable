#ifndef JGR_MOVIE_ROUTE_H
#define JGR_MOVIE_ROUTE_H
/* 0518c4 / 051aec / 051c50. Called AFTER the result destructor 036ef8.
   -1 = no DEMO; -2 = impossible source context (the original spins).
   This selects movies; it never earns clears, changes flags or writes saves. */
static inline int movie_for_result(int mid,const unsigned char *save)
{
    unsigned mask=0;
    for(unsigned i=0;i<14;i++)if(save[0x33b+i])mask|=1u<<i;
    if(save[0x350])mask|=0x4000;
    int route=save[0x35b];
    switch(mid) {
    case 0:return 0;
    case 1:return 1;
    case 2:return (mask&0x80) && (mask&0x800) ? 3:2;
    case 3:return 20;
    case 4:case 8:case 12: {
        int n=!!(mask&0x10)+!!(mask&0x100)+!!(mask&0x1000);
        return n ? 20+n:-2;
    }
    case 5:return route==2 ? 6:4;
    case 6:return route==2 ? 7:5;
    case 7:return (route==2 ? 10:8)+!!(mask&0x800);
    case 9:return route==1 ? 12:13;
    case 10:return route==1 ? 14:15;
    case 11:return (route==1 ? 16:18)+!!(mask&0x80);
    case 13:return 24;
    case 15:return 31;
    case 21:return 25;
    case 22:return 32;
    case 27:return 33;
    default:return -1;
    }
}
/* 015164 / 0153a4: prompt takes precedence; skipped movies abort a chain.
   Fresh-game movie 29 returns to mode 8 (lessons), not the garage. */
enum {MOVIE_ROUTE_RETURN=-1,MOVIE_ROUTE_SAVE=-2};
int movie_next(int id);
unsigned movie_flags(int id);
int movie_exit_route(int id,int skipped);
#endif
