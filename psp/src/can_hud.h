#ifndef JGR_CAN_HUD_H
#define JGR_CAN_HUD_H
#include <stdint.h>
/* 04e04c +6c..90; 04e784 +6c..b4. Keep the original operation order. */
typedef struct {int phase;float origin[3];int target,previous,displayed,mode,full;uint32_t color;} CanHud;
typedef struct {
    int phase,pad;
    float force[3],delta[3],velocity[3],position[3],target[3];
    uint32_t rotation,heading;
    int dead;
} CanFlight;
void can_hud_init(CanHud *,int result_mode);
int can_hud_tick(CanHud *,int *inventory,int capacity); /* child count: 0/1/5 */
void can_flight_init(CanFlight *,float screen_x,float screen_y,unsigned degrees);
int can_flight_tick(CanFlight *,float (*sine)(int)); /* arrival copies current HUD target */
void can_flight_model(const CanFlight *,float matrix[16],float (*sine)(int));
void can_hud_view_light(const float view[16],const float direction[3],float result[3]);
typedef struct CanFlightTask {CanFlight flight;struct CanFlightTask *next;unsigned health_kind;} CanFlightTask;
typedef struct {CanHud counter;CanFlightTask *children;unsigned error;} CanHudPlay;
void can_hud_play_free(CanHudPlay *);
void can_hud_play_health(CanHudPlay *,unsigned kind,float screen_x,float screen_y);
void can_hud_play_tick(CanHudPlay *,int *inventory,int capacity,float screen_x,float screen_y,float (*sine)(int));
#endif
