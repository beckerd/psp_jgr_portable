#ifndef JGR_ENEMY_MINE_H
#define JGR_ENEMY_MINE_H
/* 0afcfc..0afdec. A mine has no visible mesh; its source task warns for
   60 logic ticks, explodes once, then unlinks. Nearby explosions can
   bypass the countdown (0afe78), including after the player moves away. */
static inline int enemy_mine_tick(unsigned char *state,unsigned char *timer,
                                  unsigned flags,float distance2,int chain)
{
    switch(*state) {
    case 0:
        if(chain)*state=2;
        else if(distance2<=100.f*100.f || (flags&0x1000))*state=1;
        else if(flags&0x10)*state=3;
        break;
    case 1:
        if(chain || ++*timer>=60)*state=2;
        else return 1; /* held sound slot 7 */
        break;
    case 2:*state=3;return 2;
    case 3:*state=4;break;
    }
    return 0;
}
#endif
