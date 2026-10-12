#include "portable_native_tables.h"
#include "source_data_io.h"
#include <stdio.h>
#if JGR_PORTABLE_ASSETS
/* Only replay dependencies: the full startup loader also requires generated
 * replay cue tables, which cannot exist while this phase is running. */
#define REPLAY_TABLES(X) \
 X(ptr_player_tuning) X(ptr_player_motion) X(ptr_player_body) \
 X(motmap) X(player_fsca) X(player_sound_tables) X(player_effects) \
 X(mixed_player_c__ground_types_0) X(mixed_player_c__shallow_0) \
 X(mixed_player_c__grind_motions_0) X(mixed_collision_source_h__table_0) \
 X(spraytab) X(mixed_spray_c__skills_0) X(mixed_graffiti_game_c__tone_0) \
 X(mixed_spray_score_h__cue_x_0) \
 X(benten_portals) X(shibuya_portals) X(kogane_portals) X(kogane_water_data) \
 X(ptr_trick_hud_tables) X(ptr_stage_camera) X(ptr_eventnames) \
 X(mixed_mission_c__kind_flags_0) X(mixed_mission_c__result_class_0) \
 X(scene_tables) X(scene_command_program) X(mixed_scene_c__colours_0) X(scene_dialogue_data)
#define DECLARE(name) int source_data_##name##_load(void);
REPLAY_TABLES(DECLARE)
#undef DECLARE
static const struct {const char *name;int (*load)(void);} tables[]={
#define ENTRY(name) {"source_data_" #name ".jsd",source_data_##name##_load},
REPLAY_TABLES(ENTRY)
#undef ENTRY
};
#undef REPLAY_TABLES
#endif
int jgr_native_tables_load(JgrTableDependency dependency,void *user,
                           char *message,size_t bytes)
{
    if(message && bytes)message[0]=0;
#if JGR_PORTABLE_ASSETS
    if(source_data_io_begin(dependency,user)){
        if(message && bytes)snprintf(message,bytes,"Native replay dependency resolver unavailable");
        return -1;
    }
    int result=0;
    for(unsigned i=0;i<sizeof tables/sizeof tables[0];i++){
        int status=tables[i].load();
        if(status<0){
            if(message && bytes)snprintf(message,bytes,"Native replay table %s: %d",tables[i].name,status);
            result=-1;break;
        }
    }
    source_data_io_end();return result;
#else
    (void)dependency;(void)user;
    if(message && bytes)snprintf(message,bytes,"Native setup requires external source tables");
    return -1;
#endif
}
