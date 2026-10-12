#ifndef JGR_NPC_OCCLUSION_H
#define JGR_NPC_OCCLUSION_H
enum { NPC_OCCLUSION_OFF=0, NPC_OCCLUSION_CONSERVATIVE=1,
       NPC_OCCLUSION_AGGRESSIVE=2, NPC_OCCLUSION_PROXIES=3 };
#define NPC_OCCLUSION_MAX_PROXIES 16u
/* Render-only counters; the source simulation/audio rules remain separate. */
typedef struct {
    unsigned frames,enabled_frames,candidates,tested,hidden,kept;
    unsigned prepare_skipped,draw_skipped,query_us,occluders;
    unsigned late_tested,late_hidden;
    unsigned aggressive_frames,rays,ray_blocked,ray_uncertain,ray_unavailable;
    unsigned ray_triangles,ray_steps;
    unsigned proxy_frames,proxy_tests,proxy_blocked;
} NpcOcclusionProfile;
void npcs_occlusion_mode(int mode);
int npcs_occlusion_enabled(void); /* NPC_OCCLUSION_* mode */
void npcs_occlusion_totals(NpcOcclusionProfile *out);
void npcs_occlusion_profile(NpcOcclusionProfile *out);
/* Charge camera/placed-blocker preparation to the same whole-pass timer. */
void npcs_occlusion_preparation_us(unsigned elapsed);
#endif
