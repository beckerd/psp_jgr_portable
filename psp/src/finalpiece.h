#ifndef FINALPIECE_H
#define FINALPIECE_H
typedef struct {
    int frame,old_frame,collapse_frame,tag_ticks,collapsing,deleted;
    /* 0e9368 registers collision BEFORE composing this tick's override. */
    int collision_frame,collision_old_frame,collision_collapse_frame,collision_collapsing;
} FinalPiece;
/* 0e9368 after graffiti +14c completion; no guessed delay or gravity. */
int finalpiece_tick(FinalPiece *,int tagged,int cinematic,int phase);
#endif
