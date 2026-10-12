#ifndef JGR_FINALEMITTER_H
#define JGR_FINALEMITTER_H
#include <stdint.h>
/* Original 0ebb08 / 0ebc50 linked attack, separate from the launcher pose.
   The owning world supplies previous-query contacts before each 30-Hz tick. */
typedef struct {
    int state;int32_t angle[3];uint32_t flags,age,cadence;
    int texture,head_texture,first,last;
    float position[30][3],direction[30][3],size[30],alpha[30],scale[30];
    unsigned char variant[30];
} FinalEmitter;
typedef struct {uint32_t spheres;int line;float point[3];} FinalEmitterContact;
enum {FINAL_EMITTER_REGISTER=1,FINAL_EMITTER_LINE=2,
      FINAL_EMITTER_STOP_VOICE=4,FINAL_EMITTER_DELETE=8};
void finalemitter_init(FinalEmitter *);
unsigned finalemitter_step(FinalEmitter *,const float attachment[6],unsigned duration,
                           const FinalEmitterContact *,uint32_t *random);
/* 0ebd00 writes one shared source sphere radius. 0b70ca COPIES it
   without applying the registration scale. The next update uses this
   previous-draw radius for EVERY registered sphere, not per-particle radii. */
float finalemitter_draw_radius(const FinalEmitter *);
/* 0ebd00's ordered source meshes, billboarding and texture mutations.
   Callback receives the compact bank texture, not the original list index. */
typedef void (*FinalEmitterDraw)(void *,int,const float [16],float,unsigned);
int finalemitter_render(const FinalEmitter *,const float forward[3],FinalEmitterDraw,void *);
#endif
