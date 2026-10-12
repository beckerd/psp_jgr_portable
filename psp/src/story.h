/* Story progress: the DC save block at 0x8c10f0b4 (bytes). Mission clears with ids <32 set flag[0x33b + mission id] (FUN_8c036ef8); the STREET screen
   (garage task 0x21c init FUN_8c022a8c) derives which of its 24 map entries are selectable from these flags. */
#ifndef JGR_STORY_H
#define JGR_STORY_H
extern unsigned char story[0x400];
void story_reset(void);
int story_chapter_card_take(void); /* 021490/a01: next unseen chapter card, or -1 */
void story_mission_cleared(int mid, int chr, int stage, int kind, int rank); /* FUN_8c036ef8: flags, graffiti ranks/percentages and rewards */
int  story_notification_take(void); /* FUN_8c04e50c task 0x22: newest character/graffiti unlock, or -1 */
/* 2c2db8: mission-local acquisition count, never a count of save flags. */
void story_mission_souls_reset(void);
int story_mission_souls(void);
void story_graffiti_acquired(int id,int unlock); /* 04e50c / 04e568 */
void story_entries(unsigned char avail[24]);    /* FUN_8c022a8c: 0 = selectable, 2 = hidden */
int  story_entry_stage(int entry, int *chapter, int *special);   /* FUN_8c022e94 selection switch: entry -> (stage, chapter, screen+0x94 special) */
void story_grind_entries(unsigned char avail[9]); /* 0f6d48: two story missions, six Jet missions and return to Tokyo-to. */
int story_grind_entry_stage(int entry,int *chapter,int *special); /* 0f6e9c; entry 8 changes map, not mission. */
int  story_chapter_file(int stage, int chapter);                /* chapter record {17, index} -> mission-data file index (E_MISSIONDATA order) */
int  story_special_stage(void); /* 0f5e0c: stage 4 until Bantam is cleared, then stage 5; chapter 0. */
const char *story_file_name(int file);                          /* file index -> mission name (S1M01..) */
/* 038518 + 0369dc: result EVENT and requested task-6 mode. This describes
   the source route; it does NOT imply the EVENT is implemented. */
typedef struct { int branch, scene, mode, character; } StoryResultRoute;
StoryResultRoute story_result_route(int mid, int character_mode, int kind, int new_record);
/* 038184 / 0383b4: save-layout score records; returns insertion place
   0..4, or 5 when no Jet record qualifies. No disk I/O. */
int story_record_result(int mid,int player,int stage,int kind,int rank,int total,int commit);
int story_result_destination(int stage,int mid,int character_mode,int kind,int new_record);
/* 036ef8 is a result-task destructor, including intermediate lessons.
   Retire a successful phase once, before replacing its mission image.
   Fixtures can run this same boundary with writes disabled. */
int story_phase_retire(int *retired, int writable, int phase,
                       int mid, int character_mode, int stage, int kind, int rank);
int  story_save(const char *path);
/* Last save operation/errno, captured before cleanup can overwrite errno. */
const char *story_save_operation(void);
int story_save_errno(void);
int  story_load(const char *path);
#endif
