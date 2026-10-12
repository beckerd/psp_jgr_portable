#ifndef JGR_ENDINGPLAY_H
#define JGR_ENDINGPLAY_H
#include "ending_flow.h"
#include "movie.h"
#include "movie_source.h"
#include "staff_replay.h"
#include "staff_route.h"
#include "player.h"
#include "follow_camera.h"
#include "tex2d.h"
typedef struct {
    EndingFlow flow;Movie movie;StaffReplay replay;
    Chars actors,sky;MovieSource movie_source;unsigned char *staff_image;unsigned short *graffiti[3];
    Tex2dSet cards[3];StaffTimeline timeline;StaffCard card;
    Player player;int model,character,staff_busy,error,active,clock,last_request,save_yes;
    FollowCamera follow;FollowInput follow_query;SceneCamera ordinary;
    const Collision *staff_collision;float camera_body[3];
    Graffiti *staff_graffiti; /* borrowed stage GF tasks, retired before stage unload */
    Chars spray_actor;int spray_loaded,spray_ticks,spray_strokes,spray_entries;
    StaffRoute staff_route;TitleSample staff_sample;
    float remainder;unsigned edges,random,player_buttons,sky_yaw;int cards_shown;
    int single_movie; /* id+1; caller owns campaign routing / no-save inspection */
    int load_step,load_error; /* script=1, model=2, selected graffiti=3, staff card=4 */
    int load_card_image,load_card_buffer;
    unsigned prepared_light_vertices;
} EndingPlay;
void endingplay_init(EndingPlay *,int character);
int endingplay_inspect_movie(EndingPlay *,int movie);
int endingplay_begin_movie(EndingPlay *,int movie);
void endingplay_free(EndingPlay *);
void endingplay_resume(EndingPlay *);
/* Stage I/O remains the caller's existing level loader; failures are fatal. */
EndingRequest endingplay_step(EndingPlay *,const Collision *,Chars *,const unsigned char *save,unsigned pressed,unsigned analog,float axis);
void endingplay_draw_movie(EndingPlay *);
void endingplay_render_profile(unsigned out[4]); /* sample, ordinary, cel, shadow microseconds */
void endingplay_draw_cards(EndingPlay *);
void endingplay_draw_player(EndingPlay *,Chars *);
void endingplay_view(const EndingPlay *,SceneCamera *);
void endingplay_draw_sky(EndingPlay *,const SceneCamera *,const float planes[6][4]);
#endif
