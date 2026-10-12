/* Reuse one native credits world for every cast. The temporary stream contains
 * typed native inputs and edits, never executable bytes or observed frames. */
#include "portable_native_staff_pose.h"
#include "portable_native_player.h"
#include "source_probe.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum {STATE_DRIVER,STATE_EDIT,STATE_MOTION,STATE_INITIAL};
typedef struct {unsigned offset,kind,is_float;} PoseField;
#define FIELD(member,kind,real) {offsetof(Player,member),kind,real}
#define DRIVER(member) FIELD(member,STATE_DRIVER,0)
#define REAL(member) FIELD(member,STATE_DRIVER,1)
#define EDIT(member) FIELD(member,STATE_EDIT,0)
#define EDIT_REAL(member) FIELD(member,STATE_EDIT,1)
static const PoseField fields[]={
 REAL(x),REAL(y),REAL(z),REAL(yaw),REAL(speed),DRIVER(state),DRIVER(sub),DRIVER(logic_tick),
 DRIVER(onground),DRIVER(grinding),REAL(gn[0]),REAL(gn[1]),REAL(gn[2]),REAL(ground_y),
 DRIVER(ramp_active),DRIVER(ramp_rx),DRIVER(ramp_rz),DRIVER(pitch),DRIVER(ground_heading),
 DRIVER(health),DRIVER(stumble_ticks),DRIVER(boost),DRIVER(dash_held),DRIVER(surf),REAL(skate_rate),REAL(skate_lean),
 DRIVER(spray_mode),DRIVER(spray_active),DRIVER(small_spray_active),DRIVER(small_spray_ref),
 REAL(small_spray_frame),REAL(small_spray_target[0]),REAL(small_spray_target[1]),REAL(small_spray_target[2]),
 DRIVER(pose_overlays_blocked),DRIVER(grind_pose),DRIVER(sound_contact_override),
 REAL(portal_translation[0]),REAL(portal_translation[1]),REAL(portal_translation[2]),
 REAL(shallow_point[0]),REAL(shallow_point[1]),REAL(shallow_point[2]),
 DRIVER(visual_angles[0]),DRIVER(visual_angles[1]),DRIVER(visual_angles[2]),DRIVER(visual_valid),
 FIELD(logical_motion,STATE_MOTION,0),FIELD(spray_variant,STATE_INITIAL,0),
 EDIT_REAL(frame),EDIT_REAL(skate_dash),EDIT_REAL(skate_boost),EDIT_REAL(entry_weight),
 EDIT_REAL(coast_blend),EDIT(mirrored),EDIT(motion_phase),EDIT_REAL(motion_blend),EDIT_REAL(motion_hold),
 EDIT_REAL(overlay_blend),EDIT(overlay_logical),EDIT_REAL(overlay_frame),EDIT(overlay_ticks),EDIT(overlay_mirror)
};
#undef EDIT_REAL
#undef EDIT
#undef REAL
#undef DRIVER
#undef FIELD
#define FIELD_COUNT (sizeof fields/sizeof *fields)
#define MASK_COUNT ((FIELD_COUNT+31u)/32u)
#define STREAM_LIMIT (30000u*4u+64u)
enum {EVENT_BEGIN=1,EVENT_BEFORE,EVENT_AFTER,EVENT_FRAME};
typedef struct {float position[3],direction[3],near_clip,far_clip;int32_t roll,fov;unsigned part,cut,tracks_body;} PoseCamera;
struct JgrNativeStaffPoseRecorder {
 FILE *stream;Player *player;unsigned failed,finished,events,frames,parts,part,pending;
 uint32_t previous[FIELD_COUNT];
};
static uint32_t word(const unsigned char *p)
{return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(unsigned char *p,uint32_t v)
{p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static int write_word(FILE *f,uint32_t v)
{unsigned char bytes[4];put(bytes,v);return fwrite(bytes,1,4,f)==4;}
typedef struct {FILE *file;unsigned position,bytes;unsigned char buffer[65536];} PoseReader;
static int read_word(PoseReader *r,uint32_t *v)
{
 if(r->position==r->bytes){r->position=0;r->bytes=(unsigned)fread(r->buffer,1,sizeof r->buffer,r->file);}
 if(r->bytes-r->position<4)return 0;
 *v=word(r->buffer+r->position);r->position+=4;return 1;
}
static int snapshot(const Player *p,uint32_t state[FIELD_COUNT])
{
 for(unsigned i=0;i<FIELD_COUNT;i++) {
  memcpy(state+i,(const unsigned char *)p+fields[i].offset,4);
  if(fields[i].is_float && (state[i]&0x7f800000u)==0x7f800000u)return 0;
 }
 return 1;
}
static int record_state(JgrNativeStaffPoseRecorder *r,const Player *p,unsigned kind,const PoseCamera *camera)
{
 uint32_t state[FIELD_COUNT],mask[MASK_COUNT]={0};
 if(!r || r->failed || r->finished || r->events==STREAM_LIMIT || !snapshot(p,state))goto fail;
 for(unsigned i=0;i<FIELD_COUNT;i++)if(state[i]!=r->previous[i])mask[i/32]|=1u<<(i%32);
 unsigned char bytes[4*(1+MASK_COUNT+FIELD_COUNT)+sizeof(PoseCamera)];unsigned count=0;
 put(bytes+count,kind);count+=4;
 for(unsigned i=0;i<MASK_COUNT;i++){put(bytes+count,mask[i]);count+=4;}
 for(unsigned i=0;i<FIELD_COUNT;i++)if((mask[i/32]>>(i%32))&1u){put(bytes+count,state[i]);count+=4;}
 if(camera){uint32_t words[sizeof *camera/4];memcpy(words,camera,sizeof words);for(unsigned i=0;i<sizeof words/4;i++){put(bytes+count,words[i]);count+=4;}}
 if(fwrite(bytes,1,count,r->stream)!=count)goto fail;
 memcpy(r->previous,state,sizeof state);r->events++;return 1;
 fail:if(r)r->failed=1;return 0;
}
static int observe(void *context,Player *p,int completed)
{
 JgrNativeStaffPoseRecorder *r=context;
 if(!r || p!=r->player || r->pending!=(unsigned)completed){if(r)r->failed=1;return 0;}
 if(!record_state(r,p,completed?EVENT_AFTER:EVENT_BEFORE,NULL))return 0;
 r->pending=!completed;return 1;
}
JgrNativeStaffPoseRecorder *jgr_native_staff_pose_record_create(FILE *f)
{
 if(!f || ftell(f)!=0)return NULL;
 JgrNativeStaffPoseRecorder *r=calloc(1,sizeof *r);if(!r)return NULL;
 unsigned char header[24]={0};memcpy(header,"NCS1",4);put(header+4,1);put(header+8,FIELD_COUNT);
 if(fwrite(header,1,sizeof header,f)!=sizeof header){free(r);return NULL;}r->stream=f;return r;
}
int jgr_native_staff_pose_record_begin(JgrNativeStaffPoseRecorder *r,Player *p,unsigned part)
{
 if(!r || !p || r->failed || r->finished || r->pending || part>65535 || r->parts>=32 ||
    (r->parts && part!=r->part+1) || !write_word(r->stream,EVENT_BEGIN) || !write_word(r->stream,part)) {
  if(r)r->failed=1;return 0;
 }
 r->player=p;r->part=part;r->parts++;r->events++;memset(r->previous,0,sizeof r->previous);
 jgr_native_player_observe(p,observe,r);return 1;
}
int jgr_native_staff_pose_record_frame(JgrNativeStaffPoseRecorder *r,const Player *p,
 const SceneCamera *c,unsigned part,unsigned cut,unsigned tracks)
{
 if(!r || !p || !c || !r->parts || part!=r->part || r->pending || r->frames>=30000 || cut>1 || tracks>1){if(r)r->failed=1;return 0;}
 PoseCamera camera;memcpy(camera.position,c->pos,12);memcpy(camera.direction,c->forward,12);
 camera.near_clip=c->near_clip;camera.far_clip=c->far_clip;camera.roll=c->roll;camera.fov=c->fov;
 camera.part=part;camera.cut=cut;camera.tracks_body=tracks;
 if(!record_state(r,p,EVENT_FRAME,&camera))return 0;r->frames++;return 1;
}
int jgr_native_staff_pose_record_finish(JgrNativeStaffPoseRecorder *r)
{
 long end;
 if(!r || r->failed || r->finished || r->pending || !r->frames || fflush(r->stream) || (end=ftell(r->stream))<24 || fseek(r->stream,12,SEEK_SET) ||
    !write_word(r->stream,r->events) || !write_word(r->stream,r->frames) || !write_word(r->stream,r->parts) || fflush(r->stream) || fseek(r->stream,end,SEEK_SET)){if(r)r->failed=1;return 0;}
 r->finished=1;if(r->player)jgr_native_player_observe(r->player,NULL,NULL);return 1;
}
void jgr_native_staff_pose_record_destroy(JgrNativeStaffPoseRecorder *r)
{if(r){if(r->player)jgr_native_player_observe(r->player,NULL,NULL);free(r);}}
static void apply(Player *p,const uint32_t *state,const uint32_t *previous,int initial,int drivers)
{
 for(unsigned i=0;i<FIELD_COUNT;i++) {
  unsigned kind=fields[i].kind;
  if((drivers && kind==STATE_DRIVER) || (!drivers && kind==STATE_EDIT && (initial || state[i]!=previous[i])) ||
     (initial && kind==STATE_INITIAL))memcpy((unsigned char *)p+fields[i].offset,state+i,4);
 }
}
static int logical_motion(const uint32_t *state)
{for(unsigned i=0;i<FIELD_COUNT;i++)if(fields[i].kind==STATE_MOTION)return (int32_t)state[i];return -1;}
static int valid_motion(const Player *p)
{return jgr_native_player_motion_valid(p->logical_motion,p->overlay_logical,p->overlay_ticks);}
static void resolve_pose(Player *p)
{
 const Chars *bank=p->spray_variant&&p->spray_bank?p->spray_bank:p->pose_bank;
 const JgcChar *ch=bank->chars+(bank==p->spray_bank?0:p->pose_model);
 for(unsigned i=0;i<20;i++) {
  CharPose *pose=p->pose+i;const JgcBone *bind=bank->bones+ch->bone0+i;
  if(!(pose->keyed&1))memcpy(pose->pos,bind->pos,12);
  if(!(pose->keyed&2))memcpy(pose->ang,bind->ang,12);
  if(!(pose->keyed&4))memcpy(pose->scl,bind->scl,12);
 }
}
int jgr_native_staff_pose_replay(const JgrNativeConfig *config,FILE *f,Chars *normal,Chars *spray,
 unsigned model,unsigned character,JgrNativeStaffPoseSample sample,void *user)
{
 unsigned char header[24];Player *p=NULL;PoseReader *reader=NULL;int result=0;unsigned frames=0,parts=0,part=0,reset=1,pending=0,have_after=0;
 uint32_t state[FIELD_COUNT]={0},previous[FIELD_COUNT]={0};
 if(!f || !normal || !spray || !normal->h || !normal->chars || !normal->bones ||
    !spray->h || !spray->chars || !spray->bones || !spray->h->n_chars ||
    !sample || character>=16 || model>=normal->h->n_chars ||
    normal->chars[model].nbones!=20 || spray->chars[0].nbones!=20 || fseek(f,0,SEEK_SET) || fread(header,1,24,f)!=24 ||
    memcmp(header,"NCS1",4) || word(header+4)!=1 || word(header+8)!=FIELD_COUNT || !word(header+16) ||
    word(header+16)>30000 || word(header+12)>STREAM_LIMIT || !word(header+20) || word(header+20)>32)goto done;
 p=calloc(1,sizeof *p);reader=calloc(1,sizeof *reader);if(!p || !reader)goto done;reader->file=f;
 for(unsigned event=0;event<word(header+12);event++) {
  uint32_t kind;if(!read_word(reader,&kind))goto done;
  if(kind==EVENT_BEGIN) {
   uint32_t next;if(pending || !read_word(reader,&next) || next>65535 || (parts&&next!=part+1))goto done;
   part=next;parts++;reset=1;have_after=0;memset(state,0,sizeof state);memset(previous,0,sizeof previous);continue;
  }
  if(!parts || kind<EVENT_BEFORE || kind>EVENT_FRAME)goto done;
  uint32_t mask[MASK_COUNT];for(unsigned i=0;i<MASK_COUNT;i++)if(!read_word(reader,mask+i))goto done;
  if(FIELD_COUNT%32 && (mask[MASK_COUNT-1]>>(FIELD_COUNT%32)))goto done;
  for(unsigned i=0;i<FIELD_COUNT;i++)if((mask[i/32]>>(i%32))&1u)if(!read_word(reader,state+i))goto done;
  for(unsigned i=0;i<FIELD_COUNT;i++)if(fields[i].is_float && (state[i]&0x7f800000u)==0x7f800000u)goto done;
  if(kind==EVENT_BEFORE) {
   if(pending)goto done;int motion=logical_motion(state);
   if(!jgr_native_player_motion_valid(motion,0,0))goto done;
   if(reset){jgr_native_player_init(p,0,0,0);jgr_native_player_set_character(p,character);apply(p,state,previous,1,1);
    apply(p,state,previous,1,0);p->spray_bank=spray;p->sound_blocked=1;p->trick_hud.blocked=1;
    jgr_native_player_bind_animation(p,normal,model);}
   else apply(p,state,previous,0,1);
   resolve_pose(p);jgr_native_player_motion(p,motion);apply(p,state,previous,reset||!have_after,0);
   if(!valid_motion(p))goto done;
   jgr_native_player_animation(p);reset=0;pending=1;
  } else if(kind==EVENT_AFTER) {
   if(!pending)goto done;memcpy(previous,state,sizeof previous);have_after=1;pending=0;
  } else {
   if(reset || pending || frames>=word(header+16))goto done;
   uint32_t words[sizeof(PoseCamera)/4];for(unsigned i=0;i<sizeof words/4;i++)if(!read_word(reader,words+i))goto done;
   PoseCamera camera;memcpy(&camera,words,sizeof camera);if(camera.part!=part || camera.cut>1 || camera.tracks_body>1)goto done;
   for(unsigned i=0;i<8;i++)if((words[i]&0x7f800000u)==0x7f800000u)goto done;
   apply(p,state,previous,0,1);apply(p,state,previous,!have_after,0);if(!valid_motion(p))goto done;
   memcpy(previous,state,sizeof previous);have_after=1;
   SceneCamera view={0};view.active=1;memcpy(view.pos,camera.position,12);memcpy(view.forward,camera.direction,12);
   view.near_clip=camera.near_clip;view.far_clip=camera.far_clip;view.roll=camera.roll;view.fov=camera.fov;
   if(camera.tracks_body) {
    FollowInput body;jgr_native_player_follow_input(p,&body);
    for(unsigned k=0;k<3;k++)view.forward[k]=body.body[k]-view.pos[k];
    float length=probe_fipr(view.forward,view.forward);if(length>0.f){float inverse=1.f/sqrtf(length);for(unsigned k=0;k<3;k++)view.forward[k]*=inverse;}
   }
   if(!sample(user,p,&view,part,camera.cut))goto done;
   frames++;if(config && config->progress && !(frames&255) && !config->progress(frames,config->progress_user))goto done;
  }
 }
 if(pending || frames!=word(header+16) || parts!=word(header+20))goto done;
 if(fseek(f,-(long)(reader->bytes-reader->position),SEEK_CUR))goto done;
 result=1;
done:free(reader);free(p);if(!result && config)jgr_native_error(config,"Native credits pose stream failed");return result;
}
