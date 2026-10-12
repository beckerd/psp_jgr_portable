/* Original bank16/SOSB6 and bank17/SMSB2 owners. The channel classes are
   recovered from 0b1b68: NPC SOSB7, sequence5/6 (class17 alternating,
   class12/13 fixed). AICA note stealing across other transports is separate. */
#if JGR_PORTABLE_ASSETS
#include "source_data_world_audio_tables.inc"
#else
#include "generated/source_data/world_audio_tables.inc"
#endif
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_street_prop_audio.inc"
#else
#include "generated/source_data/street_prop_audio.inc"
#endif
#define WORLD_AUDIO_HANDLES 100 /* task IDs 233..296 */
typedef struct {unsigned token,age;int program,sequence,bank,channel,finished;float position[3];} WorldAudioHandle;
static WorldAudioHandle world_audio[WORLD_AUDIO_HANDLES];
static unsigned world_audio_serial,world_starts,world_steals,world_errors;
static unsigned char *world_bank;
static OsbSample *world_parts;static unsigned world_part_count;
static const unsigned char *world_osb;static unsigned world_osb_length;
static Smsb world_sequence;
static OsbVoice world_voice;static SmsbEnvelope world_envelope;
static unsigned world_level,world_total,world_channels[4],world_next_channel;
/* Benten type48 uses bank4/program5 (0efdf0), independent of SOSB6. */
static OsbSample *world_common;
static OsbVoice world_common_voice;static SmsbEnvelope world_common_envelope;
static unsigned world_common_level,world_common_total;
static OsbVoice *world_osb_voice(const WorldAudioHandle *h)
{return h->bank==4 ? &world_common_voice:&world_voice;}
static int world_suspended,world_stage,world_audit;
static float world_listener[3];static int world_yaw;
static int (*world_atan)(float,float);
static uint64_t world_sample_mask(int stage)
{
    uint64_t mask=(UINT64_C(1)<<27)-1; /* source NPC/traffic programs */
    if((unsigned)stage<sizeof street_prop_sound_masks/sizeof *street_prop_sound_masks)
        mask|=street_prop_sound_masks[stage];
    /* Existing train/house/door owners retain their distinct programs. */
    if(stage==2)mask|=(UINT64_C(1)<<35)|(UINT64_C(1)<<37)|(UINT64_C(1)<<39);
    if(stage==3)mask|=(UINT64_C(1)<<31)|(UINT64_C(1)<<43);
    if(stage==5)mask|=(UINT64_C(1)<<30)|(UINT64_C(1)<<39)|(UINT64_C(1)<<43);
    return mask;
}
static WorldAudioHandle *world_find(unsigned token)
{
    if(!token)return NULL;
    for(unsigned i=0;i<WORLD_AUDIO_HANDLES;i++)if(world_audio[i].token==token)return world_audio+i;
    return NULL;
}
static void world_levels(WorldAudioHandle *h)
{
    if(!world_atan || h->finished || world_channels[h->channel]!=h->token)return;
    SourceSoundPosition p=source_sound_position(h->position,world_listener,world_yaw,world_atan);
    SourceAudioProfile profile={h->sequence,1000.f,.5f,.05f};
    SourceAudioContext context={.mode=1,.bank=h->bank,.program=h->program};
    source_audio_mix_context(&audio_mix,&context);
    SourceAudioLevels levels=source_audio_levels(&profile,&context,p.distance,p.angle,p.sign);
    if(h->sequence){smsb_volume(&world_sequence,h->channel-1,levels.volume);smsb_pan(&world_sequence,h->channel-1,levels.pan);}
    else audio_channel_levels(world_osb_voice(h),levels.volume,levels.pan);
}
static void world_retire(WorldAudioHandle *h)
{
    if(world_channels[h->channel]==h->token){
        audio_channel_release(h->sequence?(const void *)h:(const void *)world_osb_voice(h));
        if(h->sequence)smsb_release(&world_sequence,h->channel-1);
        else memset(world_osb_voice(h),0,sizeof world_voice);
        world_channels[h->channel]=0;
    }
    h->finished=1;
}
void audio_world_stop(unsigned token)
{lock_scene();WorldAudioHandle *h=world_find(token);if(h){world_retire(h);memset(h,0,sizeof *h);}unlock_scene();}
void audio_free_world(void)
{
    lock_scene();unsigned char *old=world_bank;
    for(unsigned i=0;i<WORLD_AUDIO_HANDLES;i++)if(world_audio[i].token)world_retire(world_audio+i);
    const void *seq=world_sequence.sequence,*patch=world_sequence.patch;
    OsbSample *common=world_common;world_common=NULL;
    OsbSample *parts=world_parts;unsigned count=world_part_count;world_parts=NULL;world_part_count=0;
    world_bank=NULL;world_osb=NULL;world_osb_length=0;
    memset(world_audio,0,sizeof world_audio);memset(world_channels,0,sizeof world_channels);
    memset(&world_voice,0,sizeof world_voice);memset(&world_sequence,0,sizeof world_sequence);
    world_suspended=world_stage=0;world_next_channel=0;world_atan=NULL;
    unlock_scene();free(old);free((void *)seq);free((void *)patch);
    osb_parts_free(parts,count);osb_parts_free(common,1);
    /* All sample borrowers retired first; reverb tail survives. */
}
int audio_load_world(int stage)
{
    if(stage==world_stage && (world_bank || world_parts))return 0;
    {FILE *audit=fopen("audioaudit","r");world_audit=audit!=NULL;if(audit)fclose(audit);}
    audio_free_world();char name[100];snprintf(name,sizeof name,"worldaudio%d.bin",stage);
    FILE *f=fopen(name,"rb");
    if(!f)return -1;
    unsigned char *b[3]={0};OsbSample *parts=NULL,*common=NULL;long size;int error=-2;unsigned header[8],count=0;Smsb next;
    if(fseek(f,0,SEEK_END) || (size=ftell(f))<32 || size>0x100000 || fseek(f,0,SEEK_SET))goto done;
    if(fread(header,1,32,f)!=32 || memcmp(header,"WSA1",4) || header[1]!=(unsigned)stage)goto done;
    unsigned end=32;
    for(unsigned i=0;i<3;i++){
        unsigned at=header[2+i*2],n=header[3+i*2];
        if(at!=end || n<16 || at>(unsigned)size || n>(unsigned)size-at)goto done;
        end=at+n;
    }
    if(end!=(unsigned)size)goto done;
    if(fseek(f,header[2]+12,SEEK_SET) || fread(&count,4,1,f)!=1 || count<27 || count>64)goto done;
    /* Reserve the contiguous SMSB patch before the more flexible SOSB
       waves. A tutorial handoff can leave enough total free bytes without
       two 200-KiB holes. Keep all original programs and payloads. */
    for(int i=2;i>=1;i--){
        unsigned n=header[3+i*2];b[i]=malloc(n);
        if(!b[i] || fseek(f,header[2+i*2],SEEK_SET) || fread(b[i],1,n,f)!=n)goto done;
    }
    /* Load the current stage's authored impact waves before gameplay,
       sharing programs with existing owners. Unused waves remain absent;
       impacts never read the memory stick or allocate sample storage. */
    uint64_t mask=world_sample_mask(stage);count=64;
    while(count && !(mask&(UINT64_C(1)<<(count-1))))count--;
    if(osb_read_mask(f,header+2,0,count,mask,&parts))goto done;
    if(stage==1 || stage==3){
        FILE *v=fopen("voices.bin","rb");
        if(!v)goto done;
        unsigned vh[4];int bad=fread(vh,4,4,v)!=4 || memcmp(vh,"OSV1",4) || vh[1]!=17;
        if(!bad)bad=osb_read_range(v,vh+2,5,1,&common);
        fclose(v);if(bad)goto done;
    }
    if(smsb_bind_world(&next,b[1],header[5],b[2],header[7]))goto done;
    if((unsigned)stage<sizeof street_prop_sequence_masks/sizeof *street_prop_sequence_masks)
        next.allowed_programs|=street_prop_sequence_masks[stage];
    /* 0fbe60: Bantam's long train note, retired with its 1560-frame owner.
       Other unaudited ambient streams remain disabled. */
    if(stage==5)next.allowed_programs|=1u<<12;
    lock_scene();world_bank=b[0];world_osb=b[0];world_osb_length=header[3];world_sequence=next;
    world_parts=parts;world_part_count=count;parts=NULL;world_common=common;common=NULL;
    world_stage=stage;world_starts=world_steals=world_errors=0;unlock_scene();
    memset(b,0,sizeof b);error=0;
done:for(unsigned i=0;i<3;i++)free(b[i]);osb_parts_free(parts,count);osb_parts_free(common,1);fclose(f);return error;
}
static unsigned world_request(int bank,int program,const float position[3])
{
    lock_scene();unsigned token=0;int sequence=bank==17,channel=bank==4 ? 3:0;
    if((!world_bank && !world_parts) || world_suspended || !position)goto done;
    for(int j=0;j<3;j++)if(!isfinite(position[j]))goto done;
    if((sequence && ((unsigned)program>=17 || !(world_sequence.allowed_programs&(1u<<program)))) ||
       (bank==16 && ((unsigned)program>=world_part_count || !world_parts[program].program.samples)) ||
       (bank==4 && (program!=5 || !world_common || !world_common->program.samples))){world_errors++;goto done;}
    unsigned i=0;while(i<WORLD_AUDIO_HANDLES && world_audio[i].token)i++;
    if(i==WORLD_AUDIO_HANDLES || world_audio_serial==0x7fffffffu){world_errors++;goto done;}
    WorldAudioHandle *h=world_audio+i;int physical=-1;
    if(sequence){
        physical=audio_channel_acquire(h,17,program);if(physical<0)goto done;
        channel=physical-12; /* source SMSB5/6 -> this bank's two transports */
        if(channel<1 || channel>2){audio_channel_release(h);world_errors++;goto done;}
    }
    WorldAudioHandle *old=world_find(world_channels[channel]);
    if(old){world_retire(old);world_steals++;}
    h->sequence=sequence;h->bank=bank;h->channel=channel;h->program=program;
    h->token=++world_audio_serial;memcpy(h->position,position,12);world_channels[channel]=h->token;
    if(sequence){
        audio_backends[physical].sequence=&world_sequence;audio_backends[physical].stream=channel-1;
        audio_backends[physical].finished=&h->finished;
        /* A newly allocated physical handle has no previous release tails. */
        smsb_release(&world_sequence,channel-1);
        smsb_volume(&world_sequence,channel-1,127);smsb_pan(&world_sequence,channel-1,0);
        if(smsb_start(&world_sequence,channel-1,program)){world_errors++;world_retire(h);memset(h,0,sizeof *h);goto done;}
    } else {
        OsbProgram p;const unsigned char *header;int err=0;
        if(bank==4){p=world_common->program;header=world_common->header;}
        else if(world_parts){p=world_parts[program].program;header=world_parts[program].header;}
        else {unsigned offset;err=osb_program(world_osb,world_osb_length,program,&p);memcpy(&offset,world_osb+16+program*4,4);header=world_osb+offset;}
        if(err){world_errors++;world_retire(h);memset(h,0,sizeof *h);goto done;}
        if(!audio_managed_osb_header(world_osb_voice(h),bank==4 ? &world_common_envelope:&world_envelope,
            bank==4 ? &world_common_level:&world_level,bank==4 ? &world_common_total:&world_total,bank,program,header,&p,&h->finished)){
            world_retire(h);memset(h,0,sizeof *h);goto done;
        }
    }
    world_levels(h);world_starts++;token=h->token;
done:unlock_scene();
    if(world_audit && position)printf("WORLD_REQUEST bank=%d program=%d token=%u xyz=%.3f,%.3f,%.3f listener=%.3f,%.3f,%.3f\n",bank,program,token,position[0],position[1],position[2],world_listener[0],world_listener[1],world_listener[2]);
    return token;
}
unsigned audio_world_sound(int reference,const float position[3])
{return reference==INT32_MIN?0:world_request(reference<0?17:16,reference<0?-reference:reference,position);}
unsigned audio_world_common(int program,const float position[3])
{return world_request(4,program,position);}
unsigned audio_world_sequence(int program,const float position[3])
{return world_request(17,program,position);}
unsigned audio_world_npc(int type,const float position[3])
{return (unsigned)type<76 ? audio_world_sound(WORLD_NPC_REACTION[type],position):0;}
int audio_world_finished(unsigned token)
{lock_scene();WorldAudioHandle *h=world_find(token);int done=!h || h->finished;unlock_scene();return done;}
void audio_world_position(unsigned token,const float position[3])
{
    if(!position)return;
    for(int j=0;j<3;j++)if(!isfinite(position[j]))return;
    lock_scene();WorldAudioHandle *h=world_find(token);
    if(h){memcpy(h->position,position,12);world_levels(h);}unlock_scene();
}
void audio_world_listener(const float position[3],int yaw,int (*atan_bam)(float,float))
{
    if(!position || !atan_bam)return;
    lock_scene();memcpy(world_listener,position,12);world_yaw=yaw;world_atan=atan_bam;
    for(unsigned i=0;i<WORLD_AUDIO_HANDLES;i++)if(world_audio[i].token)world_levels(world_audio+i);
    unlock_scene();
}
void audio_world_tick(void)
{
    lock_scene();
    for(unsigned i=0;i<WORLD_AUDIO_HANDLES;i++){
        WorldAudioHandle *h=world_audio+i;if(!h->token || h->finished)continue;
        /* 0e73d4: bank17's ended stream is retained through clock450.
           This owner lifetime is NOT the note gate or an invented loop. */
        int active=h->sequence ? world_sequence.streams[h->channel-1].active:world_osb_voice(h)->active;
        if(!active && (!h->sequence || h->age>450))world_retire(h);
        else h->age++;
    }
    unlock_scene();
}
void audio_world_suspend(int suspended)
{
    lock_scene();world_suspended=!!suspended;
    if(suspended)for(unsigned i=0;i<WORLD_AUDIO_HANDLES;i++)if(world_audio[i].token && !world_audio[i].finished)world_retire(world_audio+i);
    unlock_scene(); /* all enabled profiles have play limit1: no restart */
    if(suspended){audio_enemy_chain_tick(1);audio_enemy_held_tick(1);audio_final_boss_voice(-2,NULL);}
}
int audio_world_blocked(void)
{lock_scene();int blocked=world_suspended || (!world_bank && !world_parts);unlock_scene();return blocked;}
void audio_world_info(unsigned out[5])
{
    lock_scene();out[0]=world_starts;out[1]=world_steals;out[2]=world_errors+world_sequence.errors;out[3]=out[4]=0;
    for(unsigned i=0;i<WORLD_AUDIO_HANDLES;i++)if(world_audio[i].token){out[3]++;out[4]+=!world_audio[i].finished;}
    unlock_scene();
}
