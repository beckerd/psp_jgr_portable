#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>
int audio_load_countdown(void);
int audio_countdown_voice(int reference); /* 179..182, negative stops */
int audio_message_prepare(int reference); /* E_SND_MUSEN 0..36; prepare during the alert */
int audio_message_request(int reference); /* Queue radio I/O; collect services completion. */
int audio_message_voice(int reference); /* task 56 -> 02d4c8 after 31 alert ticks */
void audio_free_message(void);
void audio_message_info(unsigned *position,int *active,int *error);
void audio_message_collect(void);
void audio_free_countdown(void);
int audio_load_movie_voice(int reference);
void audio_free_movie_voice(void);
void audio_start_movie_voice(void);
int audio_movie_voice_ready(void);
void audio_movie_voice_info(unsigned *position,int *active,int *error);

void audio_init(const char *music);
int audio_shutdown(void); /* true only after ME is fenced and resources freed */
void audio_set_music(int on);
void audio_pause_music(int paused); /* preserve the ADX playback cursor */
void audio_music_volume(int level); /* source menu's 0..16 selector */
void audio_music_entry(int entry); /* 02eabc -> 0e86cc stream class */
void audio_set_mono(int mono);
void audio_reverb_enable(int enabled); /* explicit dry diagnostic; normal init enables original DSP */
typedef struct {
    unsigned callbacks, total_us, max_us;
    /* Detail totals cover one in 32 callbacks, not the entire interval. */
    unsigned detail_callbacks, detail_frames, dry_us, reverb_us, output_us, service_us;
    unsigned lock_us,max_lock_us,max_gap_us,late_callbacks;
    unsigned worker_us; /* sampled full ME job elapsed, including yielded wait */
} AudioProfile;
void audio_profile_read(AudioProfile *out); /* read/reset; does not change audio */
typedef struct {
    int enabled,load_error,boot_status,ping_error,quarantined,verify_status;
    unsigned verify_samples,verify_us;
    int mix_verify_status;unsigned mix_verify_samples,mix_verify_us,workspace_bytes,sequence_capacity;
    int full_callback,mix_fail_kind,mix_fail_result;
    unsigned mix_fail_wait_us,mix_fail_timeout_us,mix_recoveries,mix_reference_us;
    unsigned ping_us,jobs,samples,fallbacks,timeouts,wait_us,max_wait_us,submit_us,polls;
    unsigned suspends,resumes,resume_errors;
} AudioMeProfile;
void audio_me_profile_read(AudioMeProfile *out);
enum { AUDIO_ME_OFF, AUDIO_ME_REVERB, AUDIO_ME_FULL };
int audio_me_status(void); /* last completed callback; does not reset counters */
void audio_note_suspend(void);
void audio_note_resume(void);
void audio_player_resume(void); /* main thread: retire cached-sample file, keep PCM/loops */
void audio_note_exit(void);
typedef struct {
    int enabled,paused,stream_class;
    unsigned gain_q15,frames,nonzero_frames,underruns,input_peak;
} MusicProfile;
/* Read/reset interval counters; do not touch transport, volume or pause. */
void audio_music_profile(MusicProfile *out);
/* Gameplay bank 4/6: independent original SOSP voices and animation loops. */
int audio_load_player(int character);
/* Zero preloads all 19 common effects and 16 selected-character clips in
   audio_load_player; playback/resume require no file I/O. Nonzero shares a
   cache target between effects/clips; pin live and suspended-loop samples. */
void audio_player_cache(unsigned bytes);
typedef struct {
    unsigned bytes,peak_bytes,loads,evictions,read_us,max_read_us;
    /* Bank/sample payload + part tables, excluding allocator overhead. */
    unsigned resident_bytes,resident_samples,cached; /* cached = budget enabled */
} AudioPlayerCacheInfo;
void audio_player_cache_info(AudioPlayerCacheInfo *out);
void audio_free_player(void);
void audio_player_stop(void);
void audio_player_suspend(int suspended); /* retire one-shots; restart retained loops on resume */
int audio_player_sound(int kind,int program); /* PLAYER_SOUND_* in player_sound.h */
/* Original +75c/+7d4 points against the last published camera listener. */
void audio_player_position(const float body[3],const float head[3],const float listener[3],int yaw,
                           int (*atan_bam)(float,float));
void audio_player_info(unsigned starts[5],unsigned *errors,unsigned *active);
/* Source world banks: generation-safe sound task tokens, not Ped pointers.
   Negative references select bank17; traffic program0 uses sequence API. */
int audio_enemy_sound(int reference,const float position[3]);
/* Loading thread, with gameplay owners stopped. Bits are enemy task kinds (including
   aliases 24/25/30/32). Only relevant attack cues become resident; voices keep
   their bounded cache. Partial failure leaves ordinary lazy playback live. */
int audio_preload_enemy_attacks(uint64_t kinds);
typedef struct {
    unsigned pinned_bytes,pinned_samples,warm_samples,failures,sequence_bytes;
    unsigned planned_cues,missing_cues;int last_error;
} AudioEnemyPreloadInfo;
void audio_enemy_preload_info(AudioEnemyPreloadInfo *out);
void audio_enemy_chain_offer(const float position[3]);
void audio_enemy_chain_tick(int reset);
void audio_enemy_mine_offer(const float position[3]);
unsigned audio_enemy_gun_offer(unsigned owner,int reference,const float position[3]);
/* reference -1 updates an existing actor voice without starting a cue. */
unsigned audio_enemy_voice_offer(unsigned owner,int reference,const float position[3]);
void audio_enemy_held_tick(int reset);
void audio_free_enemies(void);
int audio_load_world(int stage);
void audio_free_world(void);
unsigned audio_world_sound(int reference,const float position[3]);
unsigned audio_world_common(int program,const float position[3]); /* Original bank4 prop cue. */
unsigned audio_world_sequence(int program,const float position[3]);
unsigned audio_world_npc(int type,const float position[3]);
void audio_world_stop(unsigned token);
int audio_world_finished(unsigned token);
void audio_world_position(unsigned token,const float position[3]);
void audio_world_listener(const float position[3],int yaw,int (*atan_bam)(float,float));
void audio_world_tick(void); /* one original 30 Hz manager update */
void audio_world_suspend(int suspended);
int audio_world_blocked(void);
void audio_world_info(unsigned out[5]); /* starts, steals, errors, owners, live owners */
/* Original bank-0 SYS_SE sequences and bank-2 English garage invitation.
   Separate handles from BGM, EVENT speech and ending narration. */
int audio_load_menu(void);
void audio_free_menu(void);
int audio_menu_sound(int program);
int audio_garage_voice(int program); /* negative stops the owned voice */
int audio_garage_selection_voice(int program); /* source SOSB bank 1, same owner */
int audio_load_garage_selection(void);
void audio_free_garage_selection(void);
void audio_menu_info(unsigned *notes,unsigned *errors,unsigned *position,int *active);

/* sound effects: sfx.bin (tools/jgs_sfx.py, decoded OSB programs, 22050 Hz mono s16); up to 6 voices mixed in the callback */
int  audio_load_sfx(const char *path);
void audio_play_sfx(int id, float vol);
/* Source EVENT actor voice: bank 4 / bank 6, mutually replacing cues. */
int audio_load_scene_banks(int character);
/* Preload only script-referenced cues, retaining their original indices. */
int audio_load_scene_cues(int character,unsigned primary_refs,unsigned recruit_refs);
void audio_free_scene_banks(void);
int audio_scene_voice(int reference);
/* Result/failure task owns its bank-6 speech across EVENT retirement. */
int audio_load_hud_voice(int character);
int audio_hud_voice(int program); /* 10 failure / 11 result; negative stops */
void audio_free_hud_voice(void);
/* Secondary PLAY actor (EVENT 4d), independent of the playable skater. */
int audio_load_recruit_bank(int character);
int audio_load_recruit_cues(int character,unsigned refs);
int audio_recruit_voice(int reference);
void audio_recruit_voice_info(unsigned *position,int *active);
/* EVENT_6198 slot 0: 36/reference 2 -> bank 18/program 4 -> ADX 29/2.
   Independent of BGM and PLAY voices. Spatial/DSP fidelity is separate. */
int audio_load_final_voice(void);
void audio_free_final_voice(void);
int audio_final_voice(int reference);
void audio_final_voice_levels(int volume,int pan);
void audio_final_voice_info(unsigned *samples,unsigned *position,unsigned *hash,int *active);
/* Independent EVENT object slots: SOSB ref 6; SMSB ref 4/slot 2 and
   ref 3/slot 3. Other sequence/effect references fail explicitly. */
int audio_load_final_objects(void);
void audio_free_final_objects(void);
int audio_final_object(unsigned slot,int reference);
void audio_final_object_info(unsigned slot,unsigned *position,int *active);
void audio_final_sequence_info(unsigned *notes,unsigned *errors,unsigned *voices);
/* Source volume/pan -> MANATEE SOSP defaults/tables -> dry AICA gains.
   This levels entry point is for SOSB, not SMSB controllers or DSP. */
void audio_final_object_levels(unsigned slot,int volume,int pan);
/* Gameplay-owned bank-12 programs; EVENT release cannot stop them. */
int audio_load_final_attacks(void);
void audio_free_final_attacks(void);
int audio_final_attack(unsigned owner,int program);
void audio_final_attack_info(unsigned owner,unsigned *position,int *active);
/* 0ed15c gameplay Goji owner, independent of EVENT narration.
   reference >=0 starts, -1 follows/polls, -2 stops; returns live=1. */
int audio_final_boss_voice(int reference,const float position[3]);
#endif
