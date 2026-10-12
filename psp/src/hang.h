#ifndef JGR_HANG_H
#define JGR_HANG_H
enum {HANG_FRAME=1,HANG_RESUME,HANG_PROFILE,HANG_LOG_IO,HANG_INPUT,
      HANG_ROUTE_IO,HANG_PLAYER_AUDIO_IO,HANG_GE_WAIT,HANG_VBLANK,HANG_EXIT,HANG_DIAGNOSTIC,
      HANG_GRAFFITI,HANG_WORLD_BIND,HANG_CAMERA_VIEW,
      HANG_CAPTURE_OPEN,HANG_CAPTURE_COPY,HANG_CAPTURE_WRITE,HANG_CAPTURE_CLOSE,
      HANG_PLAYER_WORLD,HANG_PLAYER_SIM,HANG_PLAYER_CONTACTS,HANG_PLAYER_CAMERA,
      HANG_STREET_PROP_CONTACTS,
      HANG_LOGIC=0x100,HANG_DRAW=0x200};
#if defined(__PSP__) || defined(JGR_HANG_HOST_TEST)
typedef struct {
    unsigned serial,phase,frame,stage,state,armed,sleeping,power_epoch;
    unsigned capture_bytes,capture_total;
    unsigned player_tick,player_zone,player_movement,player_position[3];
} HangProgress;
extern volatile HangProgress hang_progress;
void hang_player_context(unsigned tick,int zone,int movement,const float position[3]);
/* Main-thread breadcrumbs are scalar stores only: no clock call or I/O. */
static inline unsigned hang_mark(unsigned phase)
{unsigned old=hang_progress.phase;hang_progress.phase=phase;hang_progress.serial++;return old;}
static inline void hang_capture_start(unsigned total)
{hang_progress.capture_bytes=0;hang_progress.capture_total=total;}
static inline void hang_capture_written(unsigned bytes)
{hang_progress.capture_bytes+=bytes;}
static inline void hang_frame(unsigned frame,unsigned stage,unsigned state)
{hang_progress.frame=frame;hang_progress.stage=stage;hang_progress.state=state;hang_progress.armed=1;hang_mark(HANG_FRAME);}
void hang_power(int sleeping);
int hang_init(void);
void hang_shutdown(void);
#else
static inline void hang_player_context(unsigned t,int z,int m,const float p[3])
{(void)t;(void)z;(void)m;(void)p;}
static inline unsigned hang_mark(unsigned phase){(void)phase;return 0;}
static inline void hang_capture_start(unsigned total){(void)total;}
static inline void hang_capture_written(unsigned bytes){(void)bytes;}
static inline void hang_frame(unsigned f,unsigned s,unsigned t){(void)f;(void)s;(void)t;}
#endif
#endif
