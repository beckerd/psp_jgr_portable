#include "diagnostics.h"
#include <pspaudiolib.h>
#include <pspaudio.h>
#include "audio.h"
#include "hang.h"
#include "stream.h"
#include "osb.h"
#include "smsb.h"
#include "adx.h"
#include "movie_voice.h"
#include "countdown_audio.h"
#include "audio_spatial.h"
#include "audio_mix.h"
#include "reverb.h"
#include "audio_decode.h"
#if (defined(__PSP__) && !defined(JGR_DISABLE_ME)) || defined(JGR_AUDIO_ME_TEST)
#define JGR_AUDIO_ME 1
#include "audio_me.h"
#include "me.h"
#include "audio_job.h"
#else
#define JGR_AUDIO_ME 0
#endif
#include <pspkernel.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#ifdef __PSP__
#include <pspdisplay.h>
#endif

#if JGR_AUDIO_ME
#include "audio_memory.h"
#include "audio_transaction.inc"
/* Unpublished radio buffers are private to their loader, never borrowed by
   an ME snapshot. Cancel them immediately, even while ME is quarantined. */
static void message_private_free(void *p){if(p)free((AudioAllocation *)p-1);}
#define malloc audio_malloc
#define calloc audio_calloc
#define free audio_free
#else
static void message_private_free(void *p){free(p);}
#endif

#if !JGR_AUDIO_ME
#define AUDIO_TOUCH_SIMPLE(i) ((void)0)
#endif
#define NVOICE 6
static short *sfx_pcm[64]; static unsigned sfx_len[64], sfx_rate[64]; static int sfx_n;
static struct { int id; unsigned pos; float vol; int active; } voice[NVOICE];   /* pos in 1/2 samples (22050 -> 44100 by doubling) */
static OsbVoice scene_voice;
static SmsbEnvelope scene_envelope,recruit_envelope;
static unsigned scene_level,scene_total,recruit_level,recruit_total;
static OsbVoice hud_voice;
static unsigned hud_voice_level,hud_voice_total;
static SmsbEnvelope hud_voice_envelope;
static OsbVoice recruit_voice;
static unsigned char *scene_banks[3];static unsigned scene_lengths[3];
static int scene_lock=-1;
static int16_t *final_pcm;
static unsigned final_samples,final_position,final_hash;
static int final_active;
static unsigned final_gain[2]={32768,32768};
static unsigned char *final_object_bank;
static unsigned final_object_length;
static OsbVoice final_object_voice[30];
static int final_object_volume[30],final_object_pan[30];
static SmsbEnvelope final_object_envelope[30];
static unsigned final_object_level[30],final_object_total[30];
static Smsb final_sequence;
static unsigned char *final_sequence_bank,*final_patch_bank;
static MovieVoice movie_voice;
static CountdownAudio countdown_audio;
static unsigned char *countdown_images[4];
static unsigned char *movie_voice_image;
static MovieVoice message_voice,message_playing;
/* Compressed ADX blocks, not one large allocation: the original city actors
   leave a fragmented 32-MiB heap. No allocation or file I/O in the mixer. */
#define MESSAGE_CHUNK_BLOCKS 512
#define MESSAGE_CHUNKS 114 /* at most one MiB of 18-byte ADX blocks */
typedef struct {unsigned char *chunk[MESSAGE_CHUNKS];} MessageImage;
static MessageImage *message_image,*message_playing_image;
static int message_reference=-1;
static Smsb menu_sequence;
static unsigned char *menu_sequence_bank,*menu_patch_bank,*garage_voice_bank,*garage_select_bank;
static unsigned garage_select_length;
static unsigned garage_voice_length;
static OsbVoice garage_voice;
static unsigned char *attack_bank;static unsigned attack_length;
static OsbVoice attack_voice[2];
static SmsbEnvelope attack_envelope[2];
static unsigned attack_level[2],attack_total[2];
static SmsbEnvelope garage_envelope;
static unsigned garage_level,garage_total;
static SourceAudioMix audio_mix={8,127,-60,0,-280,-50,0,0};
static int music_class=-1;
#if JGR_AUDIO_ME
#define reverb (audio_reverb_storage.cpu)
static unsigned reverb_reset_request,reverb_reset_seen;
static AudioMeProfile me_report;
#else
static SourceReverb reverb;
#endif
static int reverb_enabled,reverb_input;
/* The main thread may read/reset statistics while the callback yields for
   ME. Publish only completed callbacks; keep its in-progress timers private. */
static AudioProfile mixer_profile,mixer_accum;
#ifdef __PSP__
static unsigned mixer_detail_cursor;
static int mixer_detail_active;
#endif
static unsigned audio_vblank;
static volatile int audio_suspended,audio_exiting;
static volatile int audio_resume_clock;
static volatile unsigned audio_resume_generation;
static int audio_callback_active; /* guarded by scene_lock, including ME wait */
static void lock_scene(void){if(scene_lock>=0)sceKernelWaitSema(scene_lock,1,0);}
static void unlock_scene(void){if(scene_lock>=0)sceKernelSignalSema(scene_lock,1);}
void audio_profile_read(AudioProfile *out)
{
    lock_scene();*out=mixer_accum;
    mixer_accum=(AudioProfile){0};unlock_scene();
}
#ifdef __PSP__
static void audio_retire_profile(void)
{
#define SUM(field) mixer_accum.field+=mixer_profile.field
    SUM(callbacks);SUM(total_us);SUM(detail_callbacks);SUM(detail_frames);
    SUM(dry_us);SUM(worker_us);SUM(reverb_us);SUM(output_us);SUM(service_us);SUM(lock_us);SUM(late_callbacks);
#undef SUM
#define MAX(field) if(mixer_profile.field>mixer_accum.field)mixer_accum.field=mixer_profile.field
    MAX(max_us);MAX(max_lock_us);MAX(max_gap_us);
#undef MAX
}
#endif
void audio_reverb_enable(int enabled)
{
    lock_scene();
#if JGR_AUDIO_ME
    /* The ME may own an in-flight copy while scene_lock is released.
       Publish the reset for the next callback boundary; never overwrite it. */
    reverb_reset_request++;
#else
    source_reverb_reset(&reverb);
#endif
    reverb_enabled=!!enabled;unlock_scene();
}
void audio_note_suspend(void)
{
    audio_suspended=1;audio_decode_enable(0);
#if JGR_AUDIO_ME
    me_note_suspend();
#endif
}
void audio_note_resume(void)
{
    audio_resume_clock=1;
    audio_resume_generation++; /* file owners service this on their own thread */
#if JGR_AUDIO_ME
    me_note_resume();
#endif
    audio_suspended=0;
}
void audio_note_exit(void)
{
    audio_exiting=1;audio_decode_enable(0);
#if JGR_AUDIO_ME
    me_note_exit();
#endif
}
void audio_me_profile_read(AudioMeProfile *out)
{
    lock_scene();
#if JGR_AUDIO_ME
    *out=me_report;
    me_report.jobs=me_report.samples=me_report.fallbacks=me_report.timeouts=0;
    me_report.wait_us=me_report.max_wait_us=me_report.submit_us=me_report.polls=0;
#else
    *out=(AudioMeProfile){0};
#endif
    unlock_scene();
}
int audio_me_status(void)
{
    int status=AUDIO_ME_OFF;
    lock_scene();
#if JGR_AUDIO_ME
    if(!audio_suspended && !audio_exiting && me_report.enabled)
        status=me_report.full_callback ? AUDIO_ME_FULL:AUDIO_ME_REVERB;
#endif
    unlock_scene();return status;
}
#if JGR_AUDIO_ME
/* Called with scene_lock held after a callback's ME job has retired. The
   main thread only sees this snapshot, never races transport counter resets. */
static void audio_me_retire_profile(void)
{
    AudioMeProfile p;audio_me_collect(&p);
    me_report.enabled=p.enabled;me_report.load_error=p.load_error;me_report.boot_status=p.boot_status;
    me_report.ping_error=p.ping_error;me_report.ping_us=p.ping_us;me_report.quarantined=p.quarantined;
    me_report.workspace_bytes=p.workspace_bytes;me_report.sequence_capacity=p.sequence_capacity;
    me_report.mix_verify_status=p.mix_verify_status;me_report.mix_verify_samples=p.mix_verify_samples;me_report.mix_verify_us=p.mix_verify_us;
    me_report.full_callback=p.full_callback;me_report.mix_fail_kind=p.mix_fail_kind;me_report.mix_fail_result=p.mix_fail_result;
    me_report.mix_fail_wait_us=p.mix_fail_wait_us;me_report.mix_fail_timeout_us=p.mix_fail_timeout_us;
    me_report.mix_recoveries=p.mix_recoveries;me_report.mix_reference_us=p.mix_reference_us;
    me_report.verify_status=p.verify_status;me_report.verify_samples=p.verify_samples;me_report.verify_us=p.verify_us;
    me_report.suspends=p.suspends;me_report.resumes=p.resumes;me_report.resume_errors=p.resume_errors;
    me_report.jobs+=p.jobs;me_report.samples+=p.samples;me_report.fallbacks+=p.fallbacks;me_report.timeouts+=p.timeouts;
    me_report.wait_us+=p.wait_us;me_report.submit_us+=p.submit_us;me_report.polls+=p.polls;
    if(p.max_wait_us>me_report.max_wait_us)me_report.max_wait_us=p.max_wait_us;
}
#endif
static void audio_reverb_feed(const OsbVoice *voice,int sample,unsigned tl)
{
    /* Shipped reverb1 reads MIXS[0] only. Other input buses have no return. */
    if(reverb_enabled && !(voice->program.dsp&15))
        reverb_input+=source_aica_dsp(sample,tl,voice->program.dsp>>4);
}
/* Same SOSP header/envelope path for EVENT and HUD actors. Sample ownership
   remains with the caller, so a replacing cue cannot read a freed bank. */
static void audio_osb_start_header(OsbVoice *v,SmsbEnvelope *env,unsigned *level,unsigned *total,
                                   const unsigned char *h,const OsbProgram *p)
{
#if JGR_AUDIO_ME
    audio_touch_voice(v);
#endif
    unsigned short e1,e2;memcpy(&e1,h+12,2);memcpy(&e2,h+14,2);
    source_osb_registers(h,127,0,level,total);
    smsb_envelope_start(env,e1,e2,p->pitch);osb_start(v,p);
}
#include "audio_manager.inc"
#include "audio_bank.inc"
#include "player_audio.inc"
#include "world_audio.inc"
#include "event_audio.inc"
#include "enemy_audio.inc"
static OsbSample *scene_parts[3];
static OsbSample *hud_voice_parts;
void audio_free_movie_voice(void)
{
    lock_scene();unsigned char *old=movie_voice_image;movie_voice_image=NULL;
    memset(&movie_voice,0,sizeof movie_voice);unlock_scene();free(old);
}
int audio_load_movie_voice(int reference)
{
    if((unsigned)reference>=184)return -1;
    audio_free_movie_voice();char name[96];snprintf(name,sizeof name,"movievoice%03d.adx",reference);
    FILE *f=fopen(name,"rb");
    if(!f)return -2;
    unsigned char *data=NULL;MovieVoice next;long size=0;int error=-3;
    if(fseek(f,0,SEEK_END) || (size=ftell(f))<36 || size>0x300000)goto done;
    rewind(f);data=malloc(size);
    if(!data || fread(data,1,size,f)!=(unsigned)size || !movie_voice_bind(&next,data,size))goto done;
    lock_scene();movie_voice_image=data;movie_voice=next;unlock_scene();data=NULL;error=0;
done:fclose(f);free(data);return error;
}
void audio_start_movie_voice(void)
{lock_scene();if(movie_voice.ready && !movie_voice.error && movie_voice.position<movie_voice.samples)movie_voice.active=1;unlock_scene();}
int audio_movie_voice_ready(void)
{lock_scene();int ready=movie_voice.ready && !movie_voice.error;unlock_scene();return ready;}
void audio_movie_voice_info(unsigned *position,int *active,int *error)
{lock_scene();*position=movie_voice.position;*active=movie_voice.active;*error=movie_voice.error;unlock_scene();}
void audio_free_final_objects(void)
{
    lock_scene();for(unsigned i=0;i<30;i++)audio_channel_release(final_object_voice+i);
    for(unsigned i=0;i<2;i++)audio_channel_release(final_sequence.streams+i);
    memset(final_object_voice,0,sizeof final_object_voice);
    unsigned char *old=final_object_bank;final_object_bank=NULL;final_object_length=0;
    unsigned char *seq=final_sequence_bank,*patch=final_patch_bank;
    final_sequence_bank=final_patch_bank=NULL;memset(&final_sequence,0,sizeof final_sequence);
    unlock_scene();free(old);free(seq);free(patch);
}
static unsigned char *final_bank_read(const char *name,unsigned *size)
{
    FILE *f=fopen(name,"rb");
    if(!f)return NULL;
    unsigned char *data=NULL;long length=0;
    if(!fseek(f,0,SEEK_END) && (length=ftell(f))>=20 && length<=0x100000) {
        rewind(f);data=malloc(length);
        if(data && fread(data,1,length,f)!=(unsigned)length){free(data);data=NULL;}
    }
    fclose(f);*size=data ? (unsigned)length:0;return data;
}
#include "message_audio.inc"
void audio_free_countdown(void)
{
    unsigned char *old[4];lock_scene();memcpy(old,countdown_images,sizeof old);
    memset(countdown_images,0,sizeof countdown_images);memset(&countdown_audio,0,sizeof countdown_audio);
    unlock_scene();for(int i=0;i<4;i++)free(old[i]);
}
int audio_load_countdown(void)
{
    if(scene_lock<0)return -1;
    if(countdown_images[0])return 0;
    unsigned char *images[4]={0};CountdownAudio next={0};int ok=1;
    for(int i=0;i<4 && ok;i++) {
        char name[32];unsigned size=0;snprintf(name,sizeof name,"countvoice%d.adx",179+i);
        images[i]=final_bank_read(name,&size);ok=movie_voice_bind(next.cue+i,images[i],size);
    }
    if(!ok){for(int i=0;i<4;i++)free(images[i]);return -2;}
    lock_scene();countdown_audio=next;memcpy(countdown_images,images,sizeof images);unlock_scene();return 0;
}
int audio_countdown_voice(int reference)
{
    lock_scene();AUDIO_TOUCH_SIMPLE(1);int result=countdown_audio_select(&countdown_audio,reference);
    MessageImage *old=NULL,*pending=NULL;
    if(!result){
        /* A later countdown supersedes an already-dispatched, delayed
           radio start. A radio still in its alert retains its preparation
           and can take this shared owner at its own later boundary. */
        if(message_start_requested){message_cancel_locked();pending=message_image;message_image=NULL;
            message_reference=-1;memset(&message_voice,0,sizeof message_voice);}
        AUDIO_TOUCH_SIMPLE(2);old=message_playing_image;message_playing_image=NULL;memset(&message_playing,0,sizeof message_playing);
    }
    unlock_scene();message_image_free(old);message_image_free(pending);return result;
}
void audio_free_final_attacks(void)
{
    lock_scene();unsigned char *old=attack_bank;attack_bank=NULL;attack_length=0;
    for(unsigned i=0;i<2;i++)audio_channel_release(attack_voice+i);
    memset(attack_voice,0,sizeof attack_voice);unlock_scene();free(old);
}
int audio_load_final_attacks(void)
{
    if(scene_lock<0)return -1;
    if(attack_bank)return 0;
    unsigned size=0;unsigned char *data=final_bank_read("finalobjects.bin",&size);OsbProgram p;
    if(!data || osb_program(data,size,0,&p) || osb_program(data,size,1,&p)){free(data);return -2;}
    lock_scene();attack_bank=data;attack_length=size;memset(attack_voice,0,sizeof attack_voice);unlock_scene();return 0;
}
int audio_final_attack(unsigned owner,int program)
{
    if(owner>=2 || program>1)return -1;
    lock_scene();audio_channel_release(attack_voice+owner);attack_voice[owner].active=0;int error=0;
    if(program>=0) {
        OsbProgram p;error=osb_program(attack_bank,attack_length,program,&p);
        if(!error) {
            unsigned offset;memcpy(&offset,attack_bank+16+program*4,4);
            const unsigned char *h=attack_bank+offset;unsigned short env1,env2;
            memcpy(&env1,h+12,2);memcpy(&env2,h+14,2);
            source_osb_registers(h,127,0,attack_level+owner,attack_total+owner);
            audio_managed_osb(attack_voice+owner,attack_envelope+owner,attack_level+owner,attack_total+owner,
                              16,program,attack_bank,&p,NULL);
        }
    }
    unlock_scene();return error;
}
void audio_final_attack_info(unsigned owner,unsigned *position,int *active)
{
    lock_scene();*position=owner<2 ? attack_voice[owner].position:0;
    *active=owner<2 ? attack_voice[owner].active:0;unlock_scene();
}
void audio_free_menu(void)
{
    lock_scene();unsigned char *a=menu_sequence_bank,*b=menu_patch_bank,*c=garage_voice_bank,*d=garage_select_bank;
    menu_sequence_bank=menu_patch_bank=garage_voice_bank=NULL;garage_voice_length=0;
    garage_select_bank=NULL;garage_select_length=0;
    audio_channel_release(&garage_voice);
    memset(&menu_sequence,0,sizeof menu_sequence);memset(&garage_voice,0,sizeof garage_voice);
    unlock_scene();free(a);free(b);free(c);free(d);
}
int audio_load_menu(void)
{
    if(scene_lock<0)return -1;
    if(menu_sequence_bank && garage_voice_bank)return 0;
    unsigned ss=0,ps=0,vs=0;Smsb seq;OsbProgram voice;
    unsigned char *a=final_bank_read("menuseq.bin",&ss),*b=final_bank_read("menupatch.bin",&ps),*c=final_bank_read("garagevoice.bin",&vs);
    int error=smsb_bind_menu(&seq,a,ss,b,ps);
    for(int i=0;!error && i<10;i++)error=osb_program(c,vs,i,&voice);
    if(error){free(a);free(b);free(c);return -2;}
    lock_scene();menu_sequence_bank=a;menu_patch_bank=b;garage_voice_bank=c;
    menu_sequence=seq;garage_voice_length=vs;unlock_scene();return 0;
}
int audio_menu_sound(int program)
{
    lock_scene();smsb_volume(&menu_sequence,0,audio_mix.sosb_max);
    int error=smsb_start(&menu_sequence,0,program);unlock_scene();return error;
}
static int garage_speech(int program,int selection)
{
    lock_scene();audio_channel_release(&garage_voice);garage_voice.active=0;int error=0;
    const unsigned char *bank=selection ? garage_select_bank:garage_voice_bank;
    unsigned length=selection ? garage_select_length:garage_voice_length;
    if(program>=0){OsbProgram p;error=osb_program(bank,length,program,&p);
        if(!error){unsigned offset;memcpy(&offset,bank+16+program*4,4);
            const unsigned char *h=bank+offset;unsigned short env1,env2;
            memcpy(&env1,h+12,2);memcpy(&env2,h+14,2);
            source_osb_registers(h,127,0,&garage_level,&garage_total);
            /* 0e6354 refs 2/3 select physical SOSB banks 1/2 on the
               SAME reserved garage handle (+0x10), wrapper class 2. */
            if(!audio_managed_osb(&garage_voice,&garage_envelope,&garage_level,&garage_total,2,program,bank,&p,NULL))error=-3;
        }}
    unlock_scene();return error;
}
int audio_garage_voice(int program){return garage_speech(program,0);}
int audio_garage_selection_voice(int program){return garage_speech(program,1);}
int audio_load_garage_selection(void)
{
    if(scene_lock<0)return -1;
    if(garage_select_bank)return 0;
    unsigned size=0;unsigned char *bank=final_bank_read("garageselect.bin",&size);
    OsbProgram voice;int error=0;
    for(int i=0;!error && i<11;i++)error=osb_program(bank,size,i,&voice);
    if(error){free(bank);return -2;}
    lock_scene();garage_select_bank=bank;garage_select_length=size;unlock_scene();return 0;
}
void audio_free_garage_selection(void)
{
    lock_scene();audio_channel_release(&garage_voice);garage_voice.active=0;
    unsigned char *bank=garage_select_bank;garage_select_bank=NULL;garage_select_length=0;
    unlock_scene();free(bank);
}
void audio_menu_info(unsigned *notes,unsigned *errors,unsigned *position,int *active)
{lock_scene();*notes=menu_sequence.notes;*errors=menu_sequence.errors;*position=garage_voice.position;*active=garage_voice.active;unlock_scene();}
int audio_load_final_objects(void)
{
    if(scene_lock<0)return -1;
    FILE *f=fopen("finalobjects.bin","rb");
    if(!f)return -2;
    unsigned char *data=NULL,*seq=NULL,*patch=NULL;long length=0;int error=-3;OsbProgram program;
    if(fseek(f,0,SEEK_END) || (length=ftell(f))<16 || length>0x100000)goto done;
    rewind(f);data=malloc(length);
    if(!data || fread(data,1,length,f)!=(unsigned)length || osb_program(data,length,1,&program))goto done;
    /* Reject an unrelated SOSB with a valid header; the audited slot has
       this exact sample layout/pitch. Hash/bytes are checked by exporter. */
    if(program.length!=65017 || program.loop_start || program.pitch!=30720 || (program.control&0x380)!=0x100)goto done;
    unsigned ss=0,ps=0;Smsb sequence;
    seq=final_bank_read("finalseq.bin",&ss);patch=final_bank_read("finalpatch.bin",&ps);
    if(smsb_bind(&sequence,seq,ss,patch,ps)){error=-5;goto done;}
    lock_scene();for(unsigned i=0;i<30;i++)audio_channel_release(final_object_voice+i);
    for(unsigned i=0;i<2;i++)audio_channel_release(final_sequence.streams+i);
    unsigned char *old=final_object_bank;final_object_bank=data;data=old;
    old=final_sequence_bank;final_sequence_bank=seq;seq=old;
    old=final_patch_bank;final_patch_bank=patch;patch=old;final_sequence=sequence;
    final_object_length=length;memset(final_object_voice,0,sizeof final_object_voice);
    for(unsigned i=0;i<30;i++){final_object_volume[i]=127;final_object_pan[i]=0;}
    unlock_scene();error=0;
done:
    fclose(f);free(data);free(seq);free(patch);return error;
}
int audio_final_object(unsigned slot,int reference)
{
    if(slot>=30)return -1;
    lock_scene();OsbVoice *voice=final_object_voice+slot;audio_channel_release(voice);voice->active=0;int error=0;
    /* The recovered EVENT object slots own separate sequence handles. */
    if(slot==2 || slot==3){audio_channel_release(final_sequence.streams+(slot-2));smsb_stop(&final_sequence,slot-2);}
    if(reference>=0) {
        OsbProgram program;
        if((reference==4 && slot==2) || (reference==3 && slot==3)){
            unsigned stream=slot-2,program=reference==4?16:15;
            int channel=audio_channel_acquire(final_sequence.streams+stream,17,program);
            if(channel>=0){
                audio_backends[channel].sequence=&final_sequence;audio_backends[channel].stream=stream;
                error=smsb_start(&final_sequence,stream,program);
                if(error)audio_channel_release(final_sequence.streams+stream);
            }
        }
        else {
            error=reference!=6 ? -100-reference:osb_program(final_object_bank,final_object_length,1,&program);
            if(!error){
                audio_managed_osb(voice,final_object_envelope+slot,final_object_level+slot,
                                  final_object_total+slot,16,1,final_object_bank,&program,NULL);
                unsigned off;memcpy(&off,final_object_bank+20,4);
                audio_channel_levels(voice,final_object_volume[slot],final_object_pan[slot]);
            }
        }
    }
    unlock_scene();return error;
}
void audio_final_object_info(unsigned slot,unsigned *position,int *active)
{
    lock_scene();*position=slot<30 ? final_object_voice[slot].position:0;
    *active=slot<30 ? final_object_voice[slot].active:0;
    if(slot==2 || slot==3) {
        unsigned owner=slot-2;
        if(final_sequence.streams[owner].active)*active=1;
        for(unsigned i=0;i<SMSB_VOICES;i++)if(final_sequence.voices[i].active && final_sequence.voices[i].owner==owner) {
            *active=1;*position=final_sequence.voices[i].sample.position;
        }
    }
    unlock_scene();
}
void audio_final_object_levels(unsigned slot,int volume,int pan)
{
    if(slot>=30)return;
    lock_scene();
    if(final_object_bank && final_object_length>=24) {
        uint32_t off;memcpy(&off,final_object_bank+20,4);
        if(off<=final_object_length && final_object_length-off>=64) {
            unsigned level,total;
            source_osb_registers(final_object_bank+off,volume,pan,&level,&total);
            final_object_level[slot]=level;final_object_total[slot]=total;
            final_object_volume[slot]=volume;final_object_pan[slot]=pan;
            audio_channel_levels(final_object_voice+slot,volume,pan);
        }
    }
    unlock_scene();
}
void audio_final_sequence_info(unsigned *notes,unsigned *errors,unsigned *voices)
{
    lock_scene();*notes=final_sequence.notes;*errors=final_sequence.errors;*voices=0;
    for(unsigned i=0;i<SMSB_VOICES;i++)*voices+=!!final_sequence.voices[i].active;
    unlock_scene();
}
void audio_free_final_voice(void)
{
    lock_scene();int16_t *old=final_pcm;final_pcm=0;final_samples=final_position=final_hash=0;final_active=0;
    unlock_scene();free(old);
}
int audio_load_final_voice(void)
{
    if(scene_lock<0)return -1;
    FILE *f=fopen("gojivoice.adx","rb");
    if(!f)return -2;
    unsigned char *data=NULL;int16_t *pcm=NULL;int error=-3;long size=0;
    if(fseek(f,0,SEEK_END) || (size=ftell(f))<36 || size>0x100000)goto done;
    rewind(f);data=malloc(size);if(!data || fread(data,1,size,f)!=(unsigned)size)goto done;
    /* This bounded path is for the audited original asset, not a generic
       ADX parser. Reject a wrong bank/rate/version instead of guessing. */
    static const unsigned char header[20]={0x80,0,0,0x20,3,18,4,1,0,0,0xac,0x44,0,7,0x1c,0,1,0xf4,3,0};
    if(memcmp(data,header,sizeof header))goto done;
    unsigned samples=0x71c00,blocks=samples/32;
    if((unsigned)size<36+blocks*18)goto done;
    pcm=malloc(samples*2);if(!pcm){error=-4;goto done;}
    int16_t history[2]={0,0};
    /* Offline-sized preload: same 297c04 math, one call. Dreamcast CRI
       streaming job boundaries (and therefore history requantization)
       are not reproduced by this PSP preload. No audio-thread file I/O. */
    if(adx_decode_mono(data+36,size-36,blocks,pcm,samples,history,7334,-3283)!=(int)blocks)goto done;
    unsigned hash=2166136261u;
    for(unsigned i=0;i<samples*2;i++)hash=(hash^((unsigned char *)pcm)[i])*16777619u;
    lock_scene();int16_t *old=final_pcm;final_pcm=pcm;pcm=old;
    final_samples=samples;final_position=0;final_hash=hash;final_active=0;
    final_gain[0]=final_gain[1]=32768;unlock_scene();error=0;
done:
    fclose(f);free(data);free(pcm);return error;
}
int audio_final_voice(int reference)
{
    lock_scene();AUDIO_TOUCH_SIMPLE(3);int error=0;final_active=0;final_position=0;
    if(reference>=0) {
        if(reference!=2 || !final_pcm)error=-1;
        else final_active=1;
    }
    unlock_scene();return error;
}
void audio_final_voice_info(unsigned *samples,unsigned *position,unsigned *hash,int *active)
{
    lock_scene();*samples=final_samples;*position=final_position;*hash=final_hash;*active=final_active;unlock_scene();
}
void audio_final_voice_levels(int volume,int pan)
{
    unsigned level,total,gains[2];source_adx_registers(volume,pan,&level,&total);
    source_aica_gains(level,total,gains);
    lock_scene();final_gain[0]=gains[0];final_gain[1]=gains[1];unlock_scene();
}
void audio_free_scene_banks(void)
{
    lock_scene();audio_channel_release(&scene_voice);audio_channel_release(&recruit_voice);
    scene_voice.active=recruit_voice.active=0;
    unsigned char *a=scene_banks[0],*b=scene_banks[1],*c=scene_banks[2];
    OsbSample *parts[3];memcpy(parts,scene_parts,sizeof parts);memset(scene_parts,0,sizeof scene_parts);
    memset(scene_banks,0,sizeof scene_banks);memset(scene_lengths,0,sizeof scene_lengths);
    unlock_scene();free(a);free(b);free(c);
    for(unsigned i=0;i<3;i++)osb_parts_free(parts[i],i?16:15);
}
void audio_free_hud_voice(void)
{
    lock_scene();OsbSample *old=hud_voice_parts;hud_voice_parts=NULL;
    audio_channel_release(&hud_voice);
    memset(&hud_voice,0,sizeof hud_voice);unlock_scene();osb_parts_free(old,2);
}
int audio_load_hud_voice(int character)
{
    if((unsigned)character>=16 || scene_lock<0)return -1;
    FILE *f=fopen("voices.bin","rb");if(!f)return -2;
    unsigned h[2],rec[17][2];OsbSample *parts=NULL;int error=-3;
    if(fread(h,8,1,f)!=1 || memcmp(h,"OSV1",4) || h[1]!=17 || fread(rec,sizeof rec,1,f)!=1)goto done;
    /* Only programs 10/11 belong to the result/failure owner. Preserve
       their original headers/waves without duplicating the whole PLAY bank. */
    if(rec[character+1][0]<144)goto done;
    error=osb_read_range(f,rec[character+1],10,2,&parts);if(error)goto done;
    lock_scene();audio_channel_release(&hud_voice);hud_voice.active=0;OsbSample *old=hud_voice_parts;
    hud_voice_parts=parts;parts=old;unlock_scene();
done:fclose(f);osb_parts_free(parts,2);return error;
}
int audio_hud_voice(int program)
{
    lock_scene();audio_channel_release(&hud_voice);hud_voice.active=0;int error=0;
    if(program>=0){
        error=(program!=10 && program!=11) || !hud_voice_parts ? -1:0;
        if(!error){
            OsbSample *sample=hud_voice_parts+program-10;const unsigned char *h=sample->header;
            source_osb_registers(h,127,0,&hud_voice_level,&hud_voice_total);
            audio_managed_osb_header(&hud_voice,&hud_voice_envelope,&hud_voice_level,&hud_voice_total,6,program,h,&sample->program,NULL);
        }
    }
    unlock_scene();return error;
}
static int scene_program(unsigned bank,unsigned index,OsbProgram *p,const unsigned char **header)
{
    if(bank>2 || index>=(bank?16u:15u))return -1;
    if(scene_parts[bank]){
        if(!scene_parts[bank][index].samples)return -1;
        *p=scene_parts[bank][index].program;*header=scene_parts[bank][index].header;return 0;
    }
    int error=osb_program(scene_banks[bank],scene_lengths[bank],index,p);
    if(!error){unsigned offset;memcpy(&offset,scene_banks[bank]+16+index*4,4);*header=scene_banks[bank]+offset;}
    return error;
}
int audio_load_scene_banks(int character)
{
    if((unsigned)character>=16 || scene_lock<0)return -1;
    FILE *f=fopen("voices.bin","rb");if(!f)return -2;
    unsigned h[2],rec[17][2],size[2]={0};unsigned char *data[2]={0};OsbSample *parts[2]={0};int err=-3;
    if(fread(h,8,1,f)!=1 || memcmp(h,"OSV1",4) || h[1]!=17 || fread(rec,sizeof rec,1,f)!=1)goto done;
    int first=rec[character+1][1]>rec[0][1] ? 1:0;
    for(int pass=0;pass<2;pass++) {
        int i=first^pass;unsigned *r=rec[i?character+1:0];size[i]=r[1];
        if((err=osb_read_bank(f,r,i?16:15,data+i,parts+i)))goto done;
    }
    lock_scene();audio_channel_release(&scene_voice);audio_channel_release(&recruit_voice);
    scene_voice.active=recruit_voice.active=0;
    for(int i=0;i<2;i++){
        unsigned char *old=scene_banks[i];scene_banks[i]=data[i];scene_lengths[i]=size[i];data[i]=old;
        OsbSample *old_parts=scene_parts[i];scene_parts[i]=parts[i];parts[i]=old_parts;
    }
    unlock_scene();err=0;
done:
    fclose(f);free(data[0]);free(data[1]);osb_parts_free(parts[0],15);osb_parts_free(parts[1],16);return err;
}
int audio_scene_voice(int reference)
{
    lock_scene();audio_channel_release(&scene_voice);scene_voice.active=0;int err=0;
    if(reference>=0) {
        unsigned bank=reference>=15,index=reference-(bank?15:0);OsbProgram program;const unsigned char *header=NULL;
        err=(unsigned)reference>=31 ? -1:scene_program(bank,index,&program,&header);
        if(!err)audio_managed_osb_header(&scene_voice,&scene_envelope,&scene_level,&scene_total,
                                        bank?6:4,index,header,&program,NULL);
    }
    unlock_scene();return err;
}
static int audio_load_cue_banks(int character,unsigned common,unsigned personal,int recruit)
{
    if((unsigned)character>=16 || scene_lock<0)return -1;
    FILE *f=fopen("voices.bin","rb");if(!f)return -2;
    unsigned h[2],rec[17][2];OsbSample *parts[2]={0};int err=-3;
    if(fread(h,8,1,f)!=1 || memcmp(h,"OSV1",4) || h[1]!=17 || fread(rec,sizeof rec,1,f)!=1)goto done;
    if(!recruit && common && (err=osb_read_mask(f,rec[0],0,15,common,&parts[0])))goto done;
    if(personal && (err=osb_read_mask(f,rec[character+1],0,16,personal,&parts[1])))goto done;
    /* Publish only a complete preload. The callback never allocates or
       reads files, and a failed replacement leaves the old voices intact. */
    lock_scene();
    if(!recruit){audio_channel_release(&scene_voice);scene_voice.active=0;}
    audio_channel_release(&recruit_voice);recruit_voice.active=0;
    unsigned char *old_data[2]={0};
    for(unsigned i=recruit?1:0;i<2;i++) {
        unsigned bank=recruit?2:i;
        old_data[i]=scene_banks[bank];scene_banks[bank]=NULL;scene_lengths[bank]=0;
        OsbSample *old=scene_parts[bank];scene_parts[bank]=parts[i];parts[i]=old;
    }
    unlock_scene();free(old_data[0]);free(old_data[1]);err=0;
done:fclose(f);osb_parts_free(parts[0],15);osb_parts_free(parts[1],16);return err;
}
int audio_load_scene_cues(int character,unsigned primary_refs,unsigned recruit_refs)
{
    if((primary_refs|recruit_refs)&0x80000000u)return -1;
    return audio_load_cue_banks(character,(primary_refs|recruit_refs)&0x7fffu,primary_refs>>15,0);
}
int audio_load_recruit_cues(int character,unsigned refs)
{
    if(refs&0x80000000u)return -1;
    return audio_load_cue_banks(character,0,refs>>15,1);
}
int audio_load_recruit_bank(int character)
{
    if((unsigned)character>=16 || scene_lock<0)return -1;
    FILE *f=fopen("voices.bin","rb");if(!f)return -2;
    unsigned h[2],rec[17][2];unsigned char *data=NULL;OsbSample *parts=NULL;int err=-3;
    if(fread(h,8,1,f)!=1 || memcmp(h,"OSV1",4) || h[1]!=17 || fread(rec,sizeof rec,1,f)!=1)goto done;
    unsigned *r=rec[character+1];if((err=osb_read_bank(f,r,16,&data,&parts)))goto done;
    lock_scene();audio_channel_release(&recruit_voice);recruit_voice.active=0;
    unsigned char *old=scene_banks[2];scene_banks[2]=data;scene_lengths[2]=r[1];data=old;
    OsbSample *old_parts=scene_parts[2];scene_parts[2]=parts;parts=old_parts;
    unlock_scene();err=0;
done:fclose(f);free(data);osb_parts_free(parts,16);return err;
}
int audio_recruit_voice(int reference)
{
    /* 0ce94c / 1ce6c0: bank 4 for 0..14; independent bank 7 for 15..30. */
    lock_scene();audio_channel_release(&recruit_voice);recruit_voice.active=0;int err=0;
    if(reference>=0){unsigned bank=reference>=15 ? 2:0,index=reference-(bank?15:0);OsbProgram program;const unsigned char *header=NULL;
        err=(unsigned)reference>=31 ? -1:scene_program(bank,index,&program,&header);
        if(!err)audio_managed_osb_header(&recruit_voice,&recruit_envelope,&recruit_level,&recruit_total,
                                        bank?7:4,index,header,&program,NULL);}
    unlock_scene();return err;
}

void audio_recruit_voice_info(unsigned *position,int *active)
{lock_scene();*position=recruit_voice.position;*active=recruit_voice.active;unlock_scene();}
int audio_load_sfx(const char *path)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return -1;
    unsigned h[2]; sceIoRead(fd, h, 8); if (memcmp(h, "SFX1", 4) || h[1] > 64) { sceIoClose(fd); return -2; }
    sfx_n = h[1];
    for (int i = 0; i < sfx_n; i++) { unsigned rn[2]; sceIoRead(fd, rn, 8); sfx_rate[i] = rn[0]; sfx_len[i] = rn[1]; sfx_pcm[i] = malloc(rn[1] * 2); sceIoRead(fd, sfx_pcm[i], rn[1] * 2); }
    sceIoClose(fd); return 0;
}
void audio_play_sfx(int id, float vol)
{
    if (id < 0 || id >= sfx_n || !sfx_pcm[id]) return;
    lock_scene();
    int k = -1; for (int i = 0; i < NVOICE; i++) if (!voice[i].active) { k = i; break; }
    if (k < 0) k = 0;
    AUDIO_TOUCH_SIMPLE(4+k);voice[k].id = id; voice[k].pos = 0; voice[k].vol = vol; voice[k].active = 1;
    unlock_scene();
}

/* One 44.1 kHz stereo callback; FUNKY.PCM is 44.1 kHz s16 stereo and is
 * consumed 1:1. No transcendentals in here — the callback runs on a
 * tight audio thread (Tetralumen hardware lesson). */

static volatile int music_on = 1;
static volatile int output_mono;
static float music_gain;
static int music_paused;
static float music_volume=1.f;
static float speech_volume=1.f;
static unsigned music_frames,music_nonzero,music_underruns,music_input_peak;
void audio_music_profile(MusicProfile *out)
{
    lock_scene();
    *out=(MusicProfile){music_on,music_paused,music_class,(unsigned)(music_volume*32768.f),
        music_frames,music_nonzero,music_underruns,music_input_peak};
    music_frames=music_nonzero=music_underruns=music_input_peak=0;
    unlock_scene();
}
static void audio_mix_refresh(void)
{
    int bgm,speech;unsigned send,total,gain[2];
    source_audio_stream_mix(&audio_mix,music_class,&bgm,&speech);
    source_adx_registers(bgm,0,&send,&total);source_aica_gains(send,total,gain);music_volume=gain[0]/32768.f;
    source_adx_registers(speech,0,&send,&total);source_aica_gains(send,total,gain);speech_volume=gain[0]/32768.f;
    /* Direct bank-0 menu handle: 0e6354 -> 29fa70(2cabc8). */
    smsb_volume(&menu_sequence,0,audio_mix.sosb_max);
    for(unsigned i=0;i<PLAYER_AUDIO_HANDLES;i++)if(player_audio[i].used)player_audio_levels(player_audio+i);
    for(unsigned i=0;i<WORLD_AUDIO_HANDLES;i++)if(world_audio[i].token)world_levels(world_audio+i);
}
void audio_music_entry(int entry)
{lock_scene();music_class=source_audio_bgm_class(entry);audio_mix_refresh();unlock_scene();}
void audio_pause_music(int paused)
{lock_scene();music_paused=paused!=0;unlock_scene();audio_player_suspend(paused);audio_world_suspend(paused);}
void audio_music_volume(int level)
{
    lock_scene();audio_mix=source_audio_mix(level);audio_mix_refresh();unlock_scene();
}

static void audio_osb_mix(OsbVoice *v,SmsbEnvelope *env,unsigned level,unsigned total,float *left,float *right)
{
    if(!v->active)return;
    unsigned attenuation=smsb_envelope_tick(env),tl=(total>>8)+attenuation,gain[2];
    source_aica_gains(level,(tl>255?255:tl)<<8,gain);
    int sample=osb_next(v);
    audio_reverb_feed(v,sample,tl);
    *left+=((sample*(int)gain[0])>>15)*(1.f/32768.f);
    *right+=((sample*(int)gain[1])>>15)*(1.f/32768.f);
    if(env->state==4)v->active=0;
}

typedef struct {
    OsbVoice *voice;SmsbEnvelope *envelope;unsigned level,total;
} AudioOsbMix;
#define AUDIO_OSB_MIX_MAX (8+PLAYER_AUDIO_HANDLES+2+30+EVENT_AUDIO_HANDLES+ENEMY_AUDIO_HANDLES+16)
static unsigned audio_collect_osb(AudioOsbMix out[AUDIO_OSB_MIX_MAX])
{
    unsigned n=0;
    /* The callback holds scene_lock after servicing VBlank fades. OSB
       voices can finish during this buffer, but cannot start or change
       registers until the lock is released. Scan slots once per callback,
       not 44,100 times/second; keep their original floating-add order.
       Sequencers are separate: their notes CAN start within the buffer. */
#define MIX_OSB(v,e,l,t) do { if((v)->active)out[n++]=(AudioOsbMix){v,e,l,t}; } while(0)
    MIX_OSB(&scene_voice,&scene_envelope,scene_level,scene_total);
    MIX_OSB(&hud_voice,&hud_voice_envelope,hud_voice_level,hud_voice_total);
    MIX_OSB(&recruit_voice,&recruit_envelope,recruit_level,recruit_total);
    MIX_OSB(&world_voice,&world_envelope,world_level,world_total);
    MIX_OSB(&world_common_voice,&world_common_envelope,world_common_level,world_common_total);
    for(unsigned i=0;i<PLAYER_AUDIO_HANDLES;i++) {
        PlayerAudioHandle *h=player_audio+i;
        MIX_OSB(&h->voice,&h->envelope,h->level,h->total);
    }
    for(unsigned i=0;i<2;i++)MIX_OSB(attack_voice+i,attack_envelope+i,attack_level[i],attack_total[i]);
    for(unsigned i=0;i<30;i++)MIX_OSB(final_object_voice+i,final_object_envelope+i,final_object_level[i],final_object_total[i]);
    if(event_audio)for(unsigned i=0;i<EVENT_AUDIO_HANDLES;i++) {
        EventAudioHandle *h=event_audio->handles+i;
        MIX_OSB(&h->voice,&h->envelope,h->level,h->total);
    }
    for(unsigned i=0;i<ENEMY_AUDIO_HANDLES;i++){EventAudioHandle *h=enemy_audio_handles+i;MIX_OSB(&h->voice,&h->envelope,h->level,h->total);}
    MIX_OSB(&enemy_chain.voice,&enemy_chain.envelope,enemy_chain.level,enemy_chain.total);
    MIX_OSB(&enemy_mine.voice,&enemy_mine.envelope,enemy_mine.level,enemy_mine.total);
    MIX_OSB(&enemy_final_voice.voice,&enemy_final_voice.envelope,enemy_final_voice.level,enemy_final_voice.total);
    for(unsigned i=0;i<16;i++) {
        EventAudioHandle *h=&enemy_guns[i].handle;
        MIX_OSB(&h->voice,&h->envelope,h->level,h->total);
    }
#undef MIX_OSB
    return n;
}

static void audio_service_to_vblank(unsigned now)
{
    /* 29dce4 registers 2a3890 at interrupt 900; original capture observes
       one call per VBlank. Use the PSP display clock, not render dt or the
       ARM sequence IRQ. Mixer-buffer granularity is a platform boundary. */
    unsigned ticks=now-audio_vblank;audio_vblank=now;
    while(ticks--){audio_channel_service();smsb_service(&final_sequence);smsb_service(&world_sequence);smsb_service(&menu_sequence);smsb_service(&enemy_sequence);
        if(event_audio)for(unsigned i=0;i<8;i++)smsb_service(event_audio->sequencers[i]);}
}

/* Keep voice decoding and the long original DSP program in separate hot
   loops. Alternating their instruction footprints every single sample
   thrashes Allegrex's small instruction cache. This is still sample-exact:
   all dry additions retain their order, every DSP input is retained, and
   the DSP advances once per sample before the original final clamp.
   The mixer lock prevents changes to voices/effect settings mid-buffer. */
#ifndef AUDIO_MIX_BLOCK_FRAMES
#define AUDIO_MIX_BLOCK_FRAMES 64
#endif
static void __attribute__((noinline)) audio_finish_block(short *out,const float dry[][2],const int *sends,unsigned count,int enabled)
{
    int wet[AUDIO_MIX_BLOCK_FRAMES][2];
#ifdef __PSP__
    unsigned started=mixer_detail_active ? JGR_PROFILE_CLOCK():0;
#endif
    /* Separate the DSP and output loops to time them without a timer call
       per sample. The wet stream is independent of dry/output processing. */
    if(enabled)for(unsigned i=0;i<count;i++)
        source_reverb_next(&reverb,sends[i],wet[i]);
#ifdef __PSP__
    if(mixer_detail_active) {
        unsigned now=JGR_PROFILE_CLOCK();
        mixer_profile.reverb_us+=now-started;started=now;
    }
#endif
    for(unsigned i=0;i<count;i++) {
        float L=dry[i][0],R=dry[i][1];
        if(enabled) {
            /* Original EFREG0 0f1f / EFREG1 0f0f: full-send hard L/R. */
            L+=wet[i][0]*(1.f/32768.f);R+=wet[i][1]*(1.f/32768.f);
        }
        if(output_mono)L=R=(L+R)*.5f;
        if(L>1.f)L=1.f;if(L< -1.f)L=-1.f;
        if(R>1.f)R=1.f;if(R< -1.f)R=-1.f;
        out[i*2]=(short)(int)(L*28000.f);
        out[i*2+1]=(short)(int)(R*28000.f);
    }
#ifdef __PSP__
    if(mixer_detail_active)mixer_profile.output_us+=JGR_PROFILE_CLOCK()-started;
#endif
}

/* Each source owns one hot loop. The scene lock freezes register changes;
   each output sample still receives sources in the original addition order.
   No resampling, dropped voice, shortened tail or DSP approximation. */
static void __attribute__((noinline)) audio_dry_block(float dry[][2],int *sends,
        unsigned count,const AudioOsbMix *mix,unsigned voices)
{
    memset(dry,0,count*sizeof *dry);memset(sends,0,count*sizeof *sends);
    if(!music_paused)for(unsigned i=0;i<count;i++)if(stream_active()) {
        short sl,sr;int fetched=stream_fetch(&sl,&sr);
        if(music_gain>0.f) {
            if(!fetched)music_underruns++;
            else {
                music_frames++;music_nonzero+=sl!=0 || sr!=0;
                unsigned a=sl<0 ? -(int)sl:sl,b=sr<0 ? -(int)sr:sr;
                if(a>music_input_peak)music_input_peak=a;
                if(b>music_input_peak)music_input_peak=b;
            }
        }
        dry[i][0]=(float)sl*(1.f/32768.f)*music_gain;
        dry[i][1]=(float)sr*(1.f/32768.f)*music_gain;
    }
    if(movie_voice.active)for(unsigned i=0;i<count && movie_voice.active;i++) {
        float value=movie_voice_next(&movie_voice)*(1.f/32768.f);
        dry[i][0]+=value;dry[i][1]+=value;
    }
    if(!music_paused && countdown_audio.playing.active)
        for(unsigned i=0;i<count && countdown_audio.playing.active;i++) {
            float value=movie_voice_next(&countdown_audio.playing)*(1.f/32768.f)*speech_volume;
            dry[i][0]+=value;dry[i][1]+=value;
        }
    if(!music_paused && message_playing.active)
        for(unsigned i=0;i<count && message_playing.active;i++) {
            float value=message_next()*(1.f/32768.f)*speech_volume;
            dry[i][0]+=value;dry[i][1]+=value;
        }
    for(unsigned slot=0;slot<voices;slot++) {
        const AudioOsbMix *m=mix+slot;
        for(unsigned i=0;i<count && m->voice->active;i++) {
            reverb_input=0;
            audio_osb_mix(m->voice,m->envelope,m->level,m->total,&dry[i][0],&dry[i][1]);
            sends[i]+=reverb_input;
        }
    }
    Smsb *sequences[12]={&final_sequence,&world_sequence,&menu_sequence,&enemy_sequence};
    unsigned sequence_count=4;
    if(event_audio)for(unsigned i=0;i<8;i++)sequences[sequence_count++]=event_audio->sequencers[i];
    int sequence_pcm[AUDIO_MIX_BLOCK_FRAMES][3];
    for(unsigned slot=0;slot<sequence_count;slot++) {
        Smsb *seq=sequences[slot];
        if(!seq->sequence){seq->dsp_input=0;continue;}
        smsb_next_buffer(seq,sequence_pcm,count);
        for(unsigned i=0;i<count;i++) {
            dry[i][0]+=sequence_pcm[i][0]*(1.f/32768.f);
            dry[i][1]+=sequence_pcm[i][1]*(1.f/32768.f);
            sends[i]+=sequence_pcm[i][2];
        }
    }
    for(unsigned i=0;i<count && garage_voice.active;i++) {
        reverb_input=0;
        audio_osb_mix(&garage_voice,&garage_envelope,garage_level,garage_total,&dry[i][0],&dry[i][1]);
        sends[i]+=reverb_input;
    }
    for(unsigned i=0;i<count && final_active;i++) {
        int sample=final_pcm[final_position++];
        dry[i][0]+=((sample*(int)final_gain[0])>>15)*(1.f/32768.f);
        dry[i][1]+=((sample*(int)final_gain[1])>>15)*(1.f/32768.f);
        if(final_position>=final_samples)final_active=0;
    }
    for(unsigned v=0;v<NVOICE;v++)if(voice[v].active) {
        for(unsigned i=0;i<count;i++) {
            unsigned sp=voice[v].pos>>1;
            if(sp>=sfx_len[voice[v].id]){voice[v].active=0;break;}
            float value=(float)sfx_pcm[voice[v].id][sp]*(1.f/32768.f)*voice[v].vol;
            dry[i][0]+=value;dry[i][1]+=value;voice[v].pos++;
        }
    }
    reverb_input=count ? sends[count-1]:0;
}

#if JGR_AUDIO_ME
#include "audio_me_mix.inc"
/* Original reverb-only offload remains available if the full transaction
   fails verification or cannot reserve its sequencer snapshots. */
static int audio_finish_reverb(short *out,unsigned count,const AudioOsbMix *mix,unsigned voices)
{
    AudioReverbBuffer *b=audio_me_reverb_buffer();if(!b)return 0;
    int enabled=reverb_enabled,mono=output_mono;
    unsigned reset_serial=reverb_reset_request;int reset=reset_serial!=reverb_reset_seen;
    for(unsigned i=0;i<count;i+=AUDIO_MIX_BLOCK_FRAMES) {
        unsigned n=count-i;if(n>AUDIO_MIX_BLOCK_FRAMES)n=AUDIO_MIX_BLOCK_FRAMES;
        audio_dry_block(b->dry+i,b->sends+i,n,mix,voices);
    }
    unsigned started=JGR_PROFILE_CLOCK();
    unlock_scene();
    int result=audio_me_process(b->sends,b->wet,enabled ? count:0,reset);
    if(result==0) {
        if(reset)source_reverb_reset(&reverb);
        if(enabled)for(unsigned i=0;i<count;i++)source_reverb_next(&reverb,b->sends[i],b->wet[i]);
    }
    unsigned wet_done=JGR_PROFILE_CLOCK();
    for(unsigned i=0;i<count;i++) {
        float l=b->dry[i][0],r=b->dry[i][1];
        if(enabled && result>=0){l+=b->wet[i][0]*(1.f/32768.f);r+=b->wet[i][1]*(1.f/32768.f);}
        if(mono)l=r=(l+r)*.5f;
        if(l>1.f)l=1.f;if(l< -1.f)l=-1.f;
        if(r>1.f)r=1.f;if(r< -1.f)r=-1.f;
        out[2*i]=(short)(int)(l*28000.f);out[2*i+1]=(short)(int)(r*28000.f);
    }
    unsigned output_done=JGR_PROFILE_CLOCK();
    lock_scene();
    if(result>=0)reverb_reset_seen=reset_serial;
#ifdef __PSP__
    if(mixer_detail_active){mixer_profile.reverb_us+=wet_done-started;mixer_profile.output_us+=output_done-wet_done;}
#else
    (void)started;(void)wet_done;(void)output_done;
#endif
    return 1;
}
#endif
static void audio_cb(void *buf, unsigned int length, void *userdata)
{
    short *out = (short *)buf;
    unsigned int i;
    float dry[AUDIO_MIX_BLOCK_FRAMES][2];int sends[AUDIO_MIX_BLOCK_FRAMES];
    (void)userdata;
    if(audio_suspended || audio_exiting){memset(out,0,length*2*sizeof *out);return;}

#ifdef __PSP__
    static unsigned previous_callback;
    if(audio_resume_clock) {
        audio_resume_clock=0;previous_callback=0;audio_vblank=sceDisplayGetVcount();
    }
    unsigned entered=JGR_PROFILE_CLOCK(),gap=previous_callback ? entered-previous_callback:0;
    previous_callback=entered;
#endif
    lock_scene();
    if(audio_suspended || audio_exiting){unlock_scene();memset(out,0,length*2*sizeof *out);return;}
    audio_callback_active=1;
#if JGR_AUDIO_ME
    audio_me_service_resume();
    audio_decode_enable(audio_me_full_mixer());
#endif
#ifdef __PSP__
    mixer_profile=(AudioProfile){0};
    unsigned mixer_start=JGR_PROFILE_CLOCK();
    unsigned waited=mixer_start-entered;mixer_profile.lock_us+=waited;
    if(waited>mixer_profile.max_lock_us)mixer_profile.max_lock_us=waited;
    if(gap>mixer_profile.max_gap_us)mixer_profile.max_gap_us=gap;
    /* Scheduling signal, not a hardware underflow counter: more than
       1.5 nominal callback periods between successive callback entries. */
    if(gap>length*1500000u/44100u)mixer_profile.late_callbacks++;
    /* Keep detailed timers off 31/32 callbacks; no per-sample timing or I/O.
       Do not reset the sampling phase when the main thread reads stats. */
    mixer_detail_active=JGR_DIAGNOSTICS && (mixer_detail_cursor++&31u)==0;
    unsigned parts_before=mixer_profile.reverb_us+mixer_profile.output_us+mixer_profile.service_us+mixer_profile.worker_us;
    audio_service_to_vblank(sceDisplayGetVcount());
    if(mixer_detail_active)mixer_profile.service_us+=JGR_PROFILE_CLOCK()-mixer_start;
#endif
    AudioOsbMix osb_mix[AUDIO_OSB_MIX_MAX];unsigned osb_count=audio_collect_osb(osb_mix);
    music_gain=music_on ? music_volume:0.f;
#if JGR_AUDIO_ME
    /* An unfenced job owns its private mixer buffer and borrowed banks. */
    int quarantined=audio_me_quarantined() && !audio_me_force_cpu();
    if(!quarantined && audio_mix_pending){audio_mix_pending=0;audio_memory_release();}
    if(!audio_suspended && !audio_me_owns_state())audio_me_release_idle_mix();
    for(i=0;i<length;i+=1024) {
        unsigned count=length-i;if(count>1024)count=1024;
        if(i)osb_count=audio_collect_osb(osb_mix);
        if(audio_me_full_mixer() && audio_me_mix_buffer() && osb_count<=AUDIO_JOB_OSB_MAX &&
           audio_finish_me(out+2*i,count,osb_mix,osb_count))continue;
        if(!audio_me_quarantined() && audio_me_owns_state() && audio_finish_reverb(out+2*i,count,osb_mix,osb_count))continue;
        {
            quarantined=audio_me_quarantined() || (audio_me_owns_state() && !audio_me_force_cpu());
            if(!quarantined && reverb_reset_seen!=reverb_reset_request){source_reverb_reset(&reverb);reverb_reset_seen=reverb_reset_request;}
            for(unsigned k=0;k<count;k+=AUDIO_MIX_BLOCK_FRAMES) {
                unsigned n=count-k;if(n>AUDIO_MIX_BLOCK_FRAMES)n=AUDIO_MIX_BLOCK_FRAMES;
                audio_dry_block(dry,sends,n,osb_mix,osb_count);
                audio_finish_block(out+2*(i+k),dry,sends,n,reverb_enabled && !quarantined);
            }
        }
    }
    audio_me_retire_profile();
#else
    for(i=0;i<length;i+=AUDIO_MIX_BLOCK_FRAMES) {
        unsigned count=length-i;if(count>AUDIO_MIX_BLOCK_FRAMES)count=AUDIO_MIX_BLOCK_FRAMES;
        audio_dry_block(dry,sends,count,osb_mix,osb_count);
        audio_finish_block(out+2*i,dry,sends,count,reverb_enabled);
    }
#endif

#ifdef __PSP__
    unsigned elapsed=JGR_PROFILE_CLOCK()-mixer_start;
    mixer_profile.callbacks++;mixer_profile.total_us+=elapsed;
    if(elapsed>mixer_profile.max_us)mixer_profile.max_us=elapsed;
    if(mixer_detail_active) {
        unsigned parts=mixer_profile.reverb_us+mixer_profile.output_us+mixer_profile.service_us+mixer_profile.worker_us-parts_before;
        mixer_profile.dry_us+=elapsed-parts;
        mixer_profile.detail_callbacks++;mixer_profile.detail_frames+=length;
    }
    if(JGR_DIAGNOSTICS)audio_retire_profile();
#endif
    audio_callback_active=0;
    unlock_scene();
}

void audio_init(const char *music)
{
    scene_lock=sceKernelCreateSema("scene_sound",0,1,1,0);
#if JGR_AUDIO_ME
    audio_me_init();audio_me_retire_profile();
#endif
#ifdef __PSP__
    audio_vblank=sceDisplayGetVcount();
#else
    audio_vblank=0;
#endif
    audio_reverb_enable(1);
    audio_mix_refresh();
    message_worker_init();
    stream_init(music);
    pspAudioInit();
    pspAudioSetVolume(0, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX);
    pspAudioSetChannelCallback(0, audio_cb, 0);
}

int audio_shutdown(void)
{
    static int output_stopped;
    audio_note_exit();
    if(!message_worker_stop())return 0;
    /* This SDK's pspAudioEnd requests termination but does not join a
       running callback. Explicitly drain it before freeing voices/state. */
    lock_scene();int pending=audio_callback_active;unlock_scene();
    if(pending)return 0;
    if(!output_stopped){stream_shutdown();pspAudioEnd();output_stopped=1;}
#if JGR_AUDIO_ME
    if(!audio_me_shutdown())return 0;
    audio_mix_pending=0;audio_memory_release();
#endif
    audio_free_scene_banks();
    audio_free_hud_voice();
    audio_free_player();
    audio_free_world();audio_free_enemies();
    audio_free_final_voice();
    audio_free_final_objects();
    audio_free_final_attacks();
    audio_free_movie_voice();
    audio_free_countdown();
    audio_free_message();
    audio_free_menu();
    if(scene_lock>=0){sceKernelDeleteSema(scene_lock);scene_lock=-1;}
    return 1;
}

void audio_set_music(int on)
{
    lock_scene();music_on = on;unlock_scene();
}
void audio_set_mono(int mono){lock_scene();output_mono=mono!=0;unlock_scene();}

#if JGR_AUDIO_ME
#undef malloc
#undef calloc
#undef free
#undef smsb_start
#undef smsb_stop
#undef smsb_release
#undef smsb_volume
#undef smsb_pan
#undef smsb_fade_volume
#undef smsb_fade_pan
#endif
