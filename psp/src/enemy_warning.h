#ifndef JGR_ENEMY_WARNING_H
#define JGR_ENEMY_WARNING_H
typedef struct {float scale,frame,head[3];int kind;} EnemyWarning;
/* 0447f4: six source ticks to grow/shrink; warning takes precedence over
   Onishima's report flag. Keep the last kind while shrinking. */
static inline void enemy_warning_tick(EnemyWarning *w,int report,int bullet,float frames)
{
    if(report || bullet){w->kind=!!bullet;w->scale+=1.f/6.f;if(w->scale>1)w->scale=1;}
    else {w->scale-=1.f/6.f;if(w->scale<0)w->scale=0;}
    if(frames>0){w->frame+=1;if(w->frame>=frames)w->frame-=frames;}else w->frame=0;
}
#endif
