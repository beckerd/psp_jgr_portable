#ifndef JGR_PATHWORLD_H
#define JGR_PATHWORLD_H
#include "pathobject.h"
#include "pathcoll.h"
#include "source_body.h"
#include "car_pull.h"

/* Task 8a8 owner. PTH1 and Collision are borrowed and must outlive this world.
   Body B remains an actor volume, NEVER a col_probe/col_body triangle mesh.
   Registration/drawing/audio consumers are explicit; no incomplete normal
   mission may silently claim to provide them. */
#define PATHWORLD_CAPACITY 64
/* Index hints only; the render sampler verifies each current key bracket.
   Five bones cover ordinary vehicles without reserving a 64-bone pose per
   task. Larger authored models use the same sampler without hints. */
#define PATHWORLD_RENDER_HINT_BONES 5
typedef struct {
    PathObject task;
    uint32_t generation, motion, node[2], subject, model[2], pulse_bone[2];
    int active, surface, near_surface, near_model, close_draw, sound_program;
    unsigned nvertices,ntri,steps;
    float *vertices;
    ColTri *geometry[2];unsigned buffer;
    float body[24],body_previous[24];int body_valid;
    float body_center[3],body_old_center[3],body_bounds[4];
    unsigned body_flags,slip_source;
    float body_frame,body_old_frame,body_coarse[4];
    int body_dirty,body_old_animated;
    float body_cached_frame;int body_cached_animated;
    float render_radius[2];unsigned render_bounds_valid;
    uint32_t render_key_hint[PATHWORLD_RENDER_HINT_BONES][3];
    float grip_matrix[12],grip_previous[12];int grip_valid;
} PathInstance;
typedef struct {
    Scene source;
    const unsigned char *bank;size_t length;
    unsigned stage,ntypes,npaths,types,paths,count,clock;
    const unsigned char *grips; /* borrowed GRP1 authored planes/anchors */
    int error,defer_bodies;
    Collision *collision;
    PathInstance slots[PATHWORLD_CAPACITY];
    unsigned order[PATHWORLD_CAPACITY]; /* creation traversal, independent of slot reuse */
    void *context;
    /* Called before freeing task-owned data. Clear event+28 only if it still
       equals handle: chains/retries must not clear a newer event owner. */
    void (*released)(void *,unsigned event,unsigned handle);
    void (*integrated)(void *,const PathInstance *);
    void (*sound_input)(void *,const PathInstance *,PathObjectInput *);
    void (*sound_output)(void *,const PathInstance *,unsigned requests,unsigned sound);
} PathWorld;
int pathworld_bind(PathWorld *,const void *,size_t,Collision *);
int pathworld_bind_grips(PathWorld *,const void *,size_t);
int pathworld_grip_query(void *,const float point[3],CarPullContact *);
void pathworld_reset(PathWorld *); /* retains bank, callbacks and generations */
unsigned pathworld_spawn(PathWorld *,unsigned event,const unsigned args[7]);
void pathworld_control(PathWorld *,unsigned handle,int opcode);
const PathInstance *pathworld_find(const PathWorld *,unsigned handle);
/* Exactly one original task tick, not a render update. Input position is
   sampled at the OLD frame; colliders use the ADVANCED frame. */
int pathworld_tick(PathWorld *,const float player[3],const float camera[3]);
typedef struct { unsigned ticks,objects,root_us,body_us,surface_us,grip_us,total_us,body_prepared; } PathWorldProfile;
void pathworld_profile_read(PathWorldProfile *);
void pathworld_profile_totals(PathWorldProfile *); /* current interval; does not reset */
/* Materialize the exact current/prior boxes after a conservative gate. */
int pathworld_body_prepare(PathWorld *,unsigned handle);
typedef struct {unsigned handle;float distance;SourceActorContact contact;} PathBodyHit;
/* Ordered, generation-checked handles valid for this sample (not a temporal
   cache). Bounds contain all submitted sphere centres +/- twice their radii. */
unsigned pathworld_body_candidates(const PathWorld *,const float low[3],const float high[3],unsigned handles[PATHWORLD_CAPACITY]);
int pathworld_candidate_contacts(const PathWorld *,const unsigned *handles,unsigned count,
    const float sphere[4],const float center[3],const float previous[3],unsigned flags,
    unsigned layer,unsigned accepts,PathBodyHit *,unsigned capacity);
/* Original box/sphere dispatch and per-recipient contact payload. The caller
   supplies an actual source actor sphere/center/history; not guessed player
   capsule dimensions. Complete capsule/slip dispatch is a separate consumer. */
int pathworld_sphere_contacts(const PathWorld *,const float sphere[4],
    const float center[3],const float previous[3],unsigned flags,
    unsigned layer,unsigned accepts,PathBodyHit *,unsigned capacity);
#endif
