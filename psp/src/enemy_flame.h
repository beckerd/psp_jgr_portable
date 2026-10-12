#ifndef ENEMY_FLAME_H
#define ENEMY_FLAME_H
/* The two permanent hand children of Bomb variant 23 (07bea8).
   0eadc4/0eae78: independent eight-image clocks, two mesh variants,
   and five simulation ticks of growth before attribute-29 contact. */
typedef struct {unsigned char frame[2],mesh[2];float scale;unsigned ready;} EnemyHandFlames;
static inline void enemy_hand_flames_tick(EnemyHandFlames *f,unsigned (*random15)(void *),void *context)
{
    if(!f->ready) {
        for(int i=0;i<2;i++) {
            unsigned r=random15?random15(context):16384;
            f->frame[i]=(r*8)>>15;f->mesh[i]=r>=16384;
        }
        f->ready=1;f->scale=0;return;
    }
    if(f->scale<1.f){f->scale+=.2f;if(f->scale>=1.f){f->scale=1.f;f->ready=2;}}
    for(int i=0;i<2;i++)if(++f->frame[i]>7) {
        f->frame[i]=0;f->mesh[i]=(random15?random15(context):16384)>=16384;
    }
}
#endif
