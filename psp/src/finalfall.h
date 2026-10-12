#ifndef JGR_FINALFALL_H
#define JGR_FINALFALL_H
/* The stage-owned branch of 0e51a4 and its 020de8 fade child. */
typedef struct { int phase,fade_phase,visible;float alpha,step; } FinalFall;
/* One original stage tick; returns 1 only at 01b2c0's respawn call. */
int finalfall_stage(FinalFall *,float player_y,int cinematic);
void finalfall_fade(FinalFall *);
#endif
