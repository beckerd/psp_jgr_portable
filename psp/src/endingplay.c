#include "diagnostics.h"
#include "ge_submit.h"
#include "endingplay.h"
#include "ending_assets_check.h"
#include "gfx.h"
#include "font.h"
#include "audio.h"
#include "bgm.h"
#include "audio_mix.h"
#include "movie_shadow.h"
#include "stream.h"
#include "charpose.h"
#include "finalboss.h"
#include "ge_projection.h"
#include <pspkernel.h>
#include <pspgu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if JGR_PORTABLE_ASSETS
#include "source_data_staff_draw.inc"
#else
#include "generated/source_data/staff_draw.inc"
#endif
#include "staff_route.inc"
static const int models[16]={4,6,3,2,1,7,10,0,15,8,13,11,12,5,9,14};
static CharPose movie_pose[CHAR_RENDER_MAX_BONES];
static unsigned movie_render_us[4];
void endingplay_render_profile(unsigned out[4])
{memcpy(out,movie_render_us,sizeof movie_render_us);memset(movie_render_us,0,sizeof movie_render_us);}
#include "movie_light.inc"
static unsigned char *read_image(const char *name,unsigned *length)
{
    FILE *f=fopen(name,"rb");if(!f)return NULL;
    unsigned char *b=NULL;long n=0;
    if(!fseek(f,0,SEEK_END) && (n=ftell(f))>0 && n<0x1000000) {
        rewind(f);b=malloc(n);if(b && fread(b,1,n,f)!=(unsigned)n){free(b);b=NULL;}
    }
    fclose(f);*length=b ? n:0;return b;
}
void endingplay_init(EndingPlay *e,int character)
{memset(e,0,sizeof *e);ending_flow_init(&e->flow);e->active=1;e->character=character&15;e->model=models[e->character];e->random=1;}
int endingplay_begin_movie(EndingPlay *e,int movie)
{
    if((unsigned)movie>=35)return 0;
    endingplay_init(e,0);e->single_movie=movie+1;
    e->flow.phase=1;e->flow.sub=2;return 1;
}
int endingplay_inspect_movie(EndingPlay *e,int movie){return endingplay_begin_movie(e,movie);}
static void release_movie(EndingPlay *e)
{audio_free_movie_voice();chars_free(&e->actors);movie_source_free(&e->movie_source);
 for(int i=0;i<3;i++){free(e->graffiti[i]);e->graffiti[i]=NULL;}memset(&e->movie,0,sizeof e->movie);}
void endingplay_free(EndingPlay *e)
{release_movie(e);staff_route_free(&e->staff_route);chars_free(&e->spray_actor);chars_free(&e->sky);free(e->staff_image);for(int i=0;i<3;i++)tex2d_free(e->cards+i);memset(e,0,sizeof *e);chars_scene_lighting(NULL,NULL);chars_external_texture(NULL,0,0);}
void endingplay_resume(EndingPlay *e)
{title_route_resume(&e->staff_route.route);}
static int load_movie(EndingPlay *e,int id,const unsigned char *save)
{
    release_movie(e);char path[48];unsigned len=0;
    e->load_step=1;e->load_error=0;
    if(e->single_movie)snprintf(path,sizeof path,"movie%02d.bin",id);
    else snprintf(path,sizeof path,"ending%d.bin",id);
    e->load_error=movie_source_load(&e->movie_source,path);if(e->load_error)return 0;
    MovieSource *source=&e->movie_source;
    if(!(source->image ? movie_bind(&e->movie,source->image,source->length,id):
         movie_bind_pages(&e->movie,source->paged.pages,source->length,id))){e->load_error=-5;return 0;}
    e->load_step=2;
    if(e->single_movie)snprintf(path,sizeof path,"movie%02d.jgc",id);
    else snprintf(path,sizeof path,"ending%d.jgc",id);
    e->load_error=chars_load(&e->actors,path);if(e->load_error)return 0;
    e->prepared_light_vertices=movie_prepare_lighting(&e->movie,&e->actors);
    e->load_step=3;
    /* 051774 scans every segment, then 095380 loads only the referenced
       selected sizes. 09dbd4 binds S/M/XL for flags 40/80/100. */
    unsigned used=0;const Scene *s=&e->movie.source;uint32_t segments=scene_word(s,0x8c800004);
    for(int i=0;i<e->movie.count;i++) {
        uint32_t desc=scene_word(s,segments+i*4),actors=scene_word(s,desc+8);
        for(unsigned j=0;j<256;j++){uint32_t a=scene_word(s,actors+j*4);if(!a)break;used|=scene_word(s,a+12);}
    }
    for(unsigned size=0;size<3;size++)if(used&(0x40u<<size)) {
        unsigned count=(128u<<size)*128;
        if(!save || save[0x3e5+size]>=34){e->load_error=-1;return 0;}
        snprintf(path,sizeof path,"grf/%c%02u.bin","sml"[size],save[0x3e5+size]);
        e->graffiti[size]=(unsigned short *)read_image(path,&len);
        if(!e->graffiti[size] || len!=count*2){e->load_error=-2;return 0;}
        for(unsigned i=0;i<count;i++){unsigned c=e->graffiti[size][i];e->graffiti[size][i]=(c&0xf0f0)|((c>>8)&15)|((c&15)<<8);}
        sceKernelDcacheWritebackRange(e->graffiti[size],len);
    }
    e->load_step=0;return 1;
}
static int load_card(EndingPlay *e,int image,int buffer)
{
    e->load_step=4;e->load_error=0;e->load_card_image=image;e->load_card_buffer=buffer;
    if((unsigned)image>=20 || (unsigned)buffer>=3){e->load_error=-4;return 0;}
    char path[32];snprintf(path,sizeof path,"staff%02d.bin",image);
    tex2d_free(e->cards+buffer);e->load_error=tex2d_load(e->cards+buffer,path);
    if(!e->load_error && e->cards[buffer].n!=1)e->load_error=-4;
    if(e->load_error)return 0;
    e->load_step=0;return 1;
}
static int staff_sample_apply(EndingPlay *e,Chars *chars)
{
    StaffRoute *r=&e->staff_route;TitleSample *s=&e->staff_sample;
    unsigned old_frame=r->frame,had_frame=r->have_frame;
    if(!staff_route_sample(r,(unsigned)e->replay.row,(unsigned)e->replay.tick,s))return 0;
    Player *p=&e->player;const StaffRouteSpray *spray=r->spray;
    p->x=s->pos[0];p->y=s->pos[1];p->z=s->pos[2];
    p->yaw=(s->angle[1]+32768)*(6.283185307f/65536.f);
    p->state=s->state;p->visual_valid=1;memcpy(p->visual_angles,s->angle,sizeof s->angle);
    p->grinding=s->state==3;p->onground=s->state==1 || p->grinding;
    p->speed=s->effects.speed;p->logical_motion=s->effects.motion;
    p->frame=s->effects.frame;p->motion_flags=s->effects.flags;
    p->mirrored=s->effects.mirrored;p->grind_pose=s->effects.grind_pose;
    p->dash_held=s->effects.dash;p->boost=s->effects.boost;p->reset_query=0;
    memcpy(p->pose,s->pose,sizeof s->pose);p->pose_valid=1;
    p->pose_bank=chars;p->pose_model=e->model;p->overlay_motion=-1;p->overlay_blend=0;p->logic_tick=s->tick;
    p->small_spray_active=spray->active;p->spray_variant=spray->variant;
    p->small_spray_ref=spray->ref;
    p->small_spray_frame=spray->pose_frame+(spray->active ? r->frame-spray->frame:0);
    memcpy(p->small_spray_target,spray->target,sizeof spray->target);
    e->ordinary=s->camera;
    if(!had_frame || old_frame!=r->frame) {
        player_effects_tick(p,&s->effects);
        if(spray->variant)e->spray_ticks++;
    }
    while(r->next_graffiti<r->ngraffiti && r->graffiti[r->next_graffiti].frame<=r->frame) {
        const StaffRouteGraffiti *cue=r->graffiti+r->next_graffiti++;
        Graffiti *g=e->staff_graffiti;Spot *spot=NULL;
        if(!g)return 0;
        for(int i=0;i<g->n;i++)if(g->spots[i].area==(int)cue->area && g->spots[i].idx==(int)cue->index) {
            if(spot)return 0;
            spot=g->spots+i;
        }
        if(!spot)return 0;
        if(cue->started && !spot->started)e->spray_entries++;
        if(cue->stroke>spot->stroke)e->spray_strokes+=(int)(cue->stroke-spot->stroke);
        spot->stroke=cue->stroke;spot->reveal=cue->reveal;spot->complete=cue->complete;
        spot->started=cue->started;spot->revealing=cue->revealing;
        if(cue->complete && !spot->tagged){spot->tagged=1;g->tagged++;}
    }
    return 1;
}
static void staff_tick(EndingPlay *e,const Collision *col,Chars *chars)
{
    (void)col;
    Graffiti *g=e->staff_graffiti;
    int ready=e->replay.startup==3;
    int spawned=staff_replay_step(&e->replay);
    if(e->replay.error){e->error=20;return;}
    if(e->replay.load_request>=0 && !load_card(e,e->replay.load_request,e->replay.startup))e->error=21;
    if(e->error)return;
    /* The PSP file read is synchronous; do not forge the Dreamcast drive's
       observed 9-tick latency. Still execute all original loader barriers,
       and do not advance card choreography before phase 3 begins. */
    if(!ready){e->clock=e->replay.clock;return;}
    /* 0c3568 increments the clouds by ten BAM units per original draw.
       The inherited pre-ending sky phase is not yet exported by the host. */
    e->sky_yaw+=10;
    if(spawned) {
        if(g)graffiti_reset(g); /* outgoing task-19 owns its GF children */
        const CtrlStream *s=&e->replay.stream;
        player_init(&e->player,s->x,s->y,s->z);player_set_character(&e->player,e->character);
        player_bind_animation(&e->player,chars,e->model);
        e->player.yaw=s->yaw*(6.283185307f/65536.f)+3.141592654f;e->player.speed=s->speed;
        e->player.onground=1;e->player.state=1;player_cam_reset(&e->player);
        e->player.reset_query=1; /* original 01aeb0 initialization */
        memset(&e->follow,0,sizeof e->follow);e->follow.state=4;
        memset(&e->follow_query,0,sizeof e->follow_query);memset(&e->ordinary,0,sizeof e->ordinary);
        if(g){
            /* Every staff stream requests character 16: 018fb0 resolves
               the selected skater and initializes +3c8 to +3cc. */
            g->can_capacity=e->player.tun->can_capacity;g->cancount=g->can_capacity;
            g->actor=player_spray_actor;g->actor_context=&e->player;g->hud_sine=player_fsca_sin;
        }
        if(e->spray_loaded)e->player.spray_bank=&e->spray_actor;
        e->player_buttons=0;
        printf("ENDING staff spawn=%d character=%d x=%.6f y=%.6f z=%.6f\n",e->replay.row-1,e->character,s->x,s->y,s->z);
    }
    if(e->replay.phase==1) {
        if(!staff_sample_apply(e,chars)){e->load_step=5;e->load_error=-4;e->error=25;return;}
    }
    unsigned next_random=e->random,value=finalboss_random15(&next_random);
    int request=staff_timeline_step(&e->timeline,e->clock,0,&e->card,value);
    if(request==1 && !load_card(e,e->timeline.load_image,e->timeline.load_buffer))e->error=21;
    if(request==2){e->cards_shown++;e->random=next_random;printf("ENDING staff card=%d clock=%d\n",e->cards_shown,e->clock);}
    staff_card_step(&e->card);e->clock++;
    if(e->timeline.error)e->error=22;
    if(e->replay.done)e->staff_busy=0;
}
static void tick_movie(EndingPlay *e,unsigned pressed)
{
    Movie *m=&e->movie;movie_step(m,audio_movie_voice_ready());
    if(pressed&8)movie_skip(m);
    if(m->audio_request>=0 && audio_load_movie_voice(m->audio_request))e->error=7;
    if(m->audio_start)audio_start_movie_voice();
    if(m->audio_stop){audio_free_movie_voice();audio_menu_sound(3);}
    if(m->music_request>=0 && source_audio_bgm_class(bgm_current())!=26){bgm_play(m->music_request);audio_set_music(1);}
    if(m->error)e->error=3;
}
EndingRequest endingplay_step(EndingPlay *e,const Collision *col,Chars *chars,const unsigned char *save,unsigned pressed,unsigned analog,float axis)
{
    if(!e->active || e->error)return END_WAIT;
    if(e->single_movie) {
        /* Caller selects either the real campaign route or the no-save
           inspector. Asset playback itself never changes story progress. */
        if(!e->movie_source.length && !load_movie(e,e->single_movie-1,save)){e->error=1;return END_WAIT;}
        tick_movie(e,pressed);
        if(e->movie.done){e->active=0;return END_DONE;}
        return END_WAIT;
    }
    EndingInputs in={.movie_busy=e->movie_source.length && !e->movie.done,.staff_busy=e->staff_busy,.pressed=pressed,.analog_mask=analog,.axis=axis};
    int previous_yes=e->flow.yes;
    EndingRequest request=ending_flow_step(&e->flow,&in);e->last_request=request;
    if(previous_yes!=e->flow.yes)audio_menu_sound(0);
    if(request==END_SAVE_YES || request==END_SAVE_NO)audio_menu_sound(1);
    /* 0ee8c8 -> 02e8c8 starts ENDING once, before movie 25. The old
       kernel request name predates recovery of that helper's body. */
    if(request==END_STOP_AUDIO){bgm_play(4);audio_set_music(1);}
    if(request==END_LOAD_MOVIE25 || request==END_LOAD_MOVIE26) {
        if(!load_movie(e,request==END_LOAD_MOVIE25 ? 25:26,save))e->error=1;
    }
    if(request==END_RELEASE_MOVIE)release_movie(e);
    if(request==END_LOAD_STAFF) {
        unsigned n=0;e->staff_image=read_image("staffroll.bin",&n);
        if(!staff_replay_bind(&e->replay,e->staff_image,n) || !load_card(e,0,0))e->error=2;
        if(e->error)return request;
        if(chars_load(&e->sky,"staffsky.jgc") || e->sky.h->n_chars!=2 || e->sky.h->n_tex!=1)e->error=23;
        char path[48];snprintf(path,sizeof path,"spraymesh%02d.jgc",e->model);
        if(chars_load(&e->spray_actor,path) || chars_share_motions(&e->spray_actor,chars,e->model) || player_load_spray_cameras())e->error=24;
        else e->spray_loaded=1;
        if(!e->error) {
            char metadata[48];snprintf(path,sizeof path,"staffroute%02d.bin",e->character);
            snprintf(metadata,sizeof metadata,"staffroute%02d_meta.bin",e->character);
            e->load_step=5;e->load_error=staff_route_load(&e->staff_route,path,metadata,(unsigned)e->character);
            if(e->load_error)e->error=25;else e->load_step=0;
        }
        staff_timeline_init(&e->timeline);staff_card_init(&e->card);e->clock=0;
    }
    if(request==END_START_STAFF)e->staff_busy=1;
    if(request==END_RELEASE_STAGE){e->staff_graffiti=NULL;staff_route_free(&e->staff_route);chars_free(&e->spray_actor);e->spray_loaded=0;chars_free(&e->sky);free(e->staff_image);e->staff_image=NULL;for(int i=0;i<3;i++)tex2d_free(e->cards+i);}
    if(request==END_SAVE_YES)e->save_yes=1;
    if(request==END_SAVE_NO)e->save_yes=0;
    if(request==END_DONE)e->active=0;
    if(e->staff_busy)staff_tick(e,col,chars);
    if(e->movie_source.length && e->flow.sub==2 && (e->flow.phase==1 || e->flow.phase==3)) {
        tick_movie(e,pressed);
    }
    return request;
}
void endingplay_view(const EndingPlay *e,SceneCamera *camera)
{
    if(e->staff_busy && e->staff_route.metadata)*camera=e->ordinary;
    else if(e->staff_busy){float pos[3]={e->player.x,e->player.y,e->player.z};staff_replay_camera_target(&e->replay,pos,e->camera_body,camera);
        /* 0c8948 kind 0 copies the ordinary player camera, not the
           otherwise-unused camera record beside it. */
        if(!camera->active)*camera=e->ordinary;}
    else movie_camera(&e->movie,camera);
}
void endingplay_draw_sky(EndingPlay *e,const SceneCamera *camera,const float planes[6][4])
{
    if(!e->sky.h)return;
    const float scale[3]={1,1,1};int32_t angle[3]={0,0,0};float root[16];
    /* 0c3568 -> 280994 clears view translation, not view rotation. Using
       the eye as world origin is the same operation in the PSP view basis. */
    player_scene_matrix(camera->pos,angle,scale,root);
    chars_draw_background(&e->sky,0,root,planes);
    angle[1]=(int32_t)e->sky_yaw;player_scene_matrix(camera->pos,angle,scale,root);
    chars_draw_background(&e->sky,1,root,planes);
}
void endingplay_draw_movie(EndingPlay *e)
{
    Movie *m=&e->movie;if(!e->actors.h || m->phase<14)return;
    /* Render-thread-only scratch: cached RAM, never submitted to the GE.
       Reading poses back from uncached sceGuGetMemory was very expensive
       on hardware. Its budget is deducted from gfx.c's frame arena. */
    CharPose *pose=movie_pose;
    _Static_assert(sizeof movie_pose==512u*44u,"update CHAR_CPU_WORK_BYTES");
    const Scene *s=&m->source;CharLight light[2]={0};uint32_t lp=scene_word(s,m->descriptor+4);
    for(int i=0;i<2;i++) {
        uint32_t p=scene_word(s,lp+4*i);scene_read(s,p,light+i,i ? 28:24);
    }
    chars_scene_lighting(light,light+1);
    /* The close heart-graffiti and Poison Jam refinery cameras cross huge
       scenery triangles. Clip batches at the GE guard band like the PVR;
       batches wholly inside it retain hardware skinning. */
    int clip_wall=(m->movie==1 && m->segment==4) ||
        (m->movie==4 && m->segment==4) || (m->movie==6 && m->segment==3);
    float guard[6][4],visible[6][4];
    /* Movie banks contain the whole set and cast, including actors behind
       the camera. Cull using their current posed bounds before cel shading.
       The source camera remains live; no distance or animation cutoff. */
    {SceneCamera camera;float view[16];movie_camera(m,&camera);
        player_camera_matrix(camera.pos,camera.forward,camera.roll,view);
        ge_camera_guard_planes(view,player_camera_focal(camera.fov,640,1),ge_movie_near(m->movie,m->segment,camera.near_clip),camera.far_clip,guard);
        memcpy(visible,guard,sizeof visible);
        for(unsigned p=0;p<4;p++)for(unsigned j=0;j<4;j++)visible[p][j]=4.f*guard[p][j]+3.f*view[8+j];
        chars_world_frustum(visible);chars_batch_frustum(visible);}
    float root[16];for(int j=0;j<16;j++)root[j]=(j%5==0);
    int has_shadow=0;
    for(int i=0;i<m->model_count;i++)has_shadow|=scene_word(s,movie_actor(m,i)+12)==8;
    /* PVR modifiers affect opaque receivers, not cel actors. Preserve
       original order within each list, then draw translucency and cel. */
    for(int pass=0;pass<3;pass++) {
    for(int i=0;i<m->model_count;i++) {
        unsigned model=m->model0+i;if(model>=e->actors.h->n_chars){e->error=4;break;}
        uint32_t p=movie_actor(m,i),motion=scene_word(s,p+4),flags=scene_word(s,p+12);
        if(flags==8 || (pass==2 ? !(flags&2):(flags&2)))continue;
        const JgcChar *c=e->actors.chars+model;
        if(c->nbones>CHAR_RENDER_MAX_BONES){e->error=5;break;}
        if(!(flags&2)) {
            /* Do not resample/pose an opaque actor for an empty translucent
               list (or vice versa). Keep source order for nonempty lists. */
            int present=0;
            for(unsigned j=0;j<c->nbones && !present;j++) {
                const JgcBone *b=e->actors.bones+c->bone0+j;
                for(unsigned k=0;k<b->nbatch;k++)if(!!(e->actors.batch[b->batch0+k].flags&1)==pass){present=1;break;}
            }
            if(!present)continue;
        }
        unsigned started=JGR_PROFILE_CLOCK();
        float f=movie_actor_frame(m,p);
        for(unsigned j=0;j<c->nbones;j++) {
            chars_sample_pose(&e->actors,c,-1,0,j,pose+j);
            if(motion && !scene_sample_njs_nodes(s,motion,f,j,(flags&2)!=0,c->nbones,pose+j)){e->error=6;break;}
        }
        unsigned sampled=JGR_PROFILE_CLOCK();movie_render_us[0]+=sampled-started;
        if(flags&0x1c0){unsigned size=flags&0x40 ? 0:flags&0x80 ? 1:2;chars_external_texture(e->graffiti[size],128u<<size,128);}
        if(flags&2)chars_draw_one_pose(&e->actors,model,root,pose,-1,0,0);
        else if(clip_wall)chars_draw_material_pose_clipped(&e->actors,model,root,pose,pass,guard);
        else chars_draw_material_pass(&e->actors,model,root,pose,0,0,1,NULL,pass);
        chars_external_texture(NULL,0,0);
        movie_render_us[flags&2 ? 2:1]+=JGR_PROFILE_CLOCK()-sampled;
    }
    if(pass==0 && has_shadow) {
        unsigned started=JGR_PROFILE_CLOCK();
        SceneCamera camera;movie_camera(m,&camera);float view[16];
        player_camera_matrix(camera.pos,camera.forward,camera.roll,view);
        garage_shadow_begin();
        for(int i=0;i<m->model_count;i++)if(movie_shadow_actor(m,&e->actors,i,view,garage_shadow_submit,NULL)<0){e->error=8;break;}
        garage_shadow_end(view);
        movie_render_us[3]+=JGR_PROFILE_CLOCK()-started;
    }
    }
    chars_scene_lighting(NULL,NULL);
    chars_world_frustum(NULL);
    chars_batch_frustum(NULL);
    gfx_mode2d();
    float top=m->letterbox*70*(272.f/480.f),bottom=m->letterbox*90*(272.f/480.f);
    gfx_quad(0,0,480,top,0xff000000);gfx_quad(0,272-bottom,480,bottom,0xff000000);
    char text[256];int x,y;movie_caption(m,text,&x,&y);
    if(text[0]){char *line=text;while(line && *line){char *next=strstr(line,"$n");if(next){*next=0;next+=2;}
        int cx=x<0 ? (640-(int)strlen(line)*12)/2:x;
        font_real_draw_xy(cx*.75f,y*(272.f/480.f),.75f,272.f/480.f,0xffffffff,line);y+=24;line=next;}}
    if(m->fade>0)gfx_quad(0,0,480,272,((unsigned)(m->fade*255))<<24);
}
void endingplay_draw_player(EndingPlay *e,Chars *chars)
{
    int model=e->model;
    if(e->player.spray_variant && e->spray_loaded){chars=&e->spray_actor;model=0;}
    float root[16];player_root_matrix(&e->player,root);
    if(e->player.pose_valid)chars_draw_one_pose(chars,model,root,e->player.pose,e->player.overlay_motion,e->player.overlay_frame,e->player.overlay_blend);
    else chars_draw_one_xf(chars,model,root,e->player.motion,e->player.frame);
}
typedef struct {float u,v;unsigned color;float x,y,z;} CardVertex;
static void card_quad(const Tex2d *t,const float xy[8],const unsigned short uv[8],float dx,float dy,unsigned color)
{
    static const int order[6]={0,1,2,0,2,3};CardVertex *v=sceGuGetMemory(sizeof *v*6);
    for(int i=0;i<6;i++){int k=order[i];v[i]=(CardVertex){uv ? uv[2*k]/255.f*t->w:0,uv ? uv[2*k+1]/255.f*t->h:0,color,(xy[2*k]+dx)*.75f,(xy[2*k+1]+dy)*(272.f/480.f),0};}
    if(t){sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_4444,0,0,0);sceGuTexImage(0,t->w,t->h,t->w,t->px);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);}
    else sceGuDisable(GU_TEXTURE_2D);
    ge_submit_aux(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,6,NULL,v);
}
void endingplay_draw_cards(EndingPlay *e)
{
    const StaffCard *c=&e->card;if(c->kind<0)return;gfx_mode2d();
    float dx=c->kind<2 ? c->offset:0,dy=c->kind<2 ? 0:c->offset;
    const unsigned short *pattern=c->kind<2 ? STAFF_UV_PATTERN_X[c->pattern]:STAFF_UV_PATTERN_Y[c->pattern];
    const unsigned short *page=c->kind<2 ? STAFF_UV_PAGE_X[c->page]:STAFF_UV_PAGE_Y[c->page];
    card_quad(&e->cards[2].t[0],STAFF_XY[c->kind][0],pattern,dx,dy,0xffffffff);
    if(c->kind>=2)card_quad(&e->cards[2].t[0],STAFF_XY[c->kind][0],pattern,dx+512,dy,0xffffffff);
    /* Black backing and narrow edge are source 21c0e0 vertex colours. */
    card_quad(NULL,STAFF_XY[c->kind][2],NULL,dx,dy,0xff000000);
    card_quad(&e->cards[c->buffer].t[0],STAFF_XY[c->kind][1],page,dx,dy,0xffffffff);
    sceGuDisable(GU_TEXTURE_2D);
}
