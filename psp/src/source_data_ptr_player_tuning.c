/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {
    int grab_damage;float grab_stun_ticks,grab_recover,grab_mash;
    int health_max,can_capacity,health_low; /* original +00 / +0c / +1c */
    float vmax, vmax_dash, vmax_stumble, vmax_stumble_dash, vmax_contact, vmax_contact_dash;
    float stop_speed, max_speed, dash_coast;
    int uphill_limit; float uphill_speed; int uphill_dead; float ramp_uphill_speed;
    int downhill_limit; float downhill_speed; int downhill_dead; float ramp_downhill_speed;
    float ramp_align_speed; int ramp_align_slow,ramp_align_fast;
    int back_angle;
    float turn_dash_a, turn_dash_b, turn_a, turn_b;
    float ramp_turn_dash_a, ramp_turn_dash_b, ramp_turn_a, ramp_turn_b;
    float jump_h; int jump_ticks; float jump_boost, land_k, jump_max_speed, stumble_bounce;
    float fall_release, rise_fraction; int jump_min;
    float fall_max, air_min_speed, air_brake;
    int air_dead, air_turn, air_back_angle;
    float ground_up, ground_down, shallow_down, air_up;
    float trick_speed;
    float body_radius, body_ground[3], body_air[3];
    int recovery_ticks, wall_damage, fall_damage, water_damage, vehicle_damage,bullet_damage,fire_damage;
    int explosion_damage,taban_damage;float damage_jump_h;
    float wall_radius,wall_distance,wall_up,wall_down,body_wall[3];
    float ramp_min_vy, ramp_launch_speed; int ramp_launch_pitch;
    float rail_up,rail_down,body_rail[3];
} CharTuning;


#include "source_data_pointers.h"
CharTuning PLAYER_TUNING[16] __asm__("jgr_ptr_player_tuning_PLAYER_TUNING") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {856847183,64,5632,16,PLAYER_TUNING},
};
int source_data_ptr_player_tuning_load(void){return source_data_pointers_load_file("source_data_ptr_player_tuning.jsd",sections,1u,5696u,0x02155afbu,0u);}
#endif
