#ifndef JGR_ENDING_FLOW_H
#define JGR_ENDING_FLOW_H
/* Source task 9ff, 0ee8c8 / 0ee974 / 0eea20 / 0eed60 / 0eee18.
   The task/resource adapter is deliberately separate: a request is NOT
   successful playback or loading. No story flags or saves are written here. */
typedef enum {
    END_WAIT,END_STOP_AUDIO,END_LOAD_MOVIE25,END_START_MOVIE,END_RELEASE_MOVIE,
    END_LOAD_GAME,END_LOAD_STAGE,END_LOAD_MISSION,END_CREATE_STAGE,END_LOAD_SKATER,
    END_LOAD_STAFF,END_START_STAFF,END_RELEASE_STAGE,END_LOAD_MOVIE26,
    END_LOAD_SAVE,END_SAVE_YES,END_SAVE_NO,END_RELEASE_SAVE,END_DONE
} EndingRequest;
typedef struct {int phase,sub,yes;float previous_axis;} EndingFlow;
typedef struct {
    int loader_busy,movie_busy,stage_busy,staff_busy;
    unsigned pressed,analog_mask;float axis;
} EndingInputs;
void ending_flow_init(EndingFlow *);
EndingRequest ending_flow_step(EndingFlow *,const EndingInputs *);

/* Original staff card slide (100b94 / 100c0c / 101334). The renderer's
   four entry directions and two halves per GRF image are retained. */
typedef struct {
    int phase,kind,hold,buffer,page,pattern;
    float offset,step,target;
} StaffCard;
void staff_card_init(StaffCard *);
int staff_card_start(StaffCard *,int kind,int hold,int buffer,int page,unsigned random15);
void staff_card_step(StaffCard *);
/* Camera viewport cropping applied by 10118c: width, height, center X/Y,
   horizontal and vertical multipliers. */
void staff_card_viewport(const StaffCard *,float out[6]);

typedef struct {
    int phase,pc,buffer,page[3],next_image,load_image,load_buffer,error;
} StaffTimeline;
void staff_timeline_init(StaffTimeline *);
/* 02812c only; call once per original staff callback, after initial
   images 0/1/19 are loaded. Outputs 1=load original GRF image, 2=start
   card, 0=wait. Passing loader_busy keeps original asynchronous barriers. */
int staff_timeline_step(StaffTimeline *,int clock,int loader_busy,StaffCard *,unsigned random15);
#endif
