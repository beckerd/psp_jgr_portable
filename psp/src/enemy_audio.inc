/* Enemy tasks own these transports independently of EVENT/cutscene voices.
   Relevant attacks can be warmed at level load; other original SOSP
   fragments load on first use. Physical MANATEE channels control stealing. */
#if JGR_PORTABLE_ASSETS
#include "source_data_enemy_audio_tables.inc"
#else
#include "generated/source_data/enemy_audio_tables.inc"
#endif
#define ENEMY_AUDIO_HANDLES 16
#define ENEMY_AUDIO_CUE_COUNT (sizeof ENEMY_AUDIO_CUES/sizeof *ENEMY_AUDIO_CUES)
static EventAudioHandle enemy_audio_handles[ENEMY_AUDIO_HANDLES],enemy_chain,enemy_mine;
static EventAudioHandle enemy_final_voice;
static float enemy_mine_position[3],enemy_mine_distance=500.f;
static int enemy_mine_requested;
static struct {EventAudioHandle handle;unsigned owner;int reference,requested;float position[3];} enemy_guns[16];
static float enemy_chain_position[3],enemy_chain_distance=500.f;
static int enemy_chain_requested;
static OsbSample *enemy_audio_samples[ENEMY_AUDIO_CUE_COUNT];
static unsigned enemy_audio_used[ENEMY_AUDIO_CUE_COUNT],enemy_audio_clock,enemy_audio_bytes;
/* Separate frequent attacks from the evictable speech cache. This bound is
   compressed sample storage, not decoded PCM; only current-level cues pin. */
#define ENEMY_AUDIO_PIN_LIMIT (128u*1024u)
static unsigned char enemy_audio_pinned[ENEMY_AUDIO_CUE_COUNT];
static unsigned enemy_audio_pin_bytes;
static int enemy_audio_preloading;
static AudioEnemyPreloadInfo enemy_audio_preload;
static unsigned char *enemy_sequence_image,*enemy_patch_image;
static unsigned enemy_sequence_size,enemy_patch_size,enemy_audio_cursor;
static Smsb enemy_sequence;
void audio_free_enemies(void)
{
    lock_scene();
    for(unsigned i=0;i<ENEMY_AUDIO_HANDLES;i++)audio_channel_release(enemy_audio_handles+i);
    audio_channel_release(&enemy_chain);memset(&enemy_chain,0,sizeof enemy_chain);
    enemy_chain_requested=0;enemy_chain_distance=500.f;
    audio_channel_release(&enemy_mine);memset(&enemy_mine,0,sizeof enemy_mine);
    audio_channel_release(&enemy_final_voice);memset(&enemy_final_voice,0,sizeof enemy_final_voice);
    enemy_mine_requested=0;enemy_mine_distance=500.f;
    for(unsigned i=0;i<16;i++)audio_channel_release(&enemy_guns[i].handle);
    memset(enemy_guns,0,sizeof enemy_guns);
    memset(enemy_audio_handles,0,sizeof enemy_audio_handles);memset(&enemy_sequence,0,sizeof enemy_sequence);
    unsigned char *a=enemy_sequence_image,*b=enemy_patch_image;enemy_sequence_image=enemy_patch_image=NULL;
    unlock_scene();free(a);free(b);
    for(unsigned i=0;i<ENEMY_AUDIO_CUE_COUNT;i++){osb_parts_free(enemy_audio_samples[i],1);enemy_audio_samples[i]=NULL;}
    enemy_audio_bytes=enemy_audio_clock=0;memset(enemy_audio_used,0,sizeof enemy_audio_used);
    enemy_audio_pin_bytes=0;enemy_audio_preloading=0;memset(enemy_audio_pinned,0,sizeof enemy_audio_pinned);
    enemy_audio_preload=(AudioEnemyPreloadInfo){0};
}
static int enemy_audio_reclaim(unsigned keep,int permit_resident)
{
    int candidate=-1;lock_scene();
    for(unsigned i=0;i<ENEMY_AUDIO_CUE_COUNT;i++)if(i!=keep && enemy_audio_samples[i] && (permit_resident || !enemy_audio_pinned[i])) {
        int pinned=0;
        for(unsigned h=0;h<ENEMY_AUDIO_HANDLES;h++)if(enemy_audio_handles[h].cue==ENEMY_AUDIO_CUES+i && enemy_audio_handles[h].voice.active)pinned=1;
        if(enemy_chain.cue==ENEMY_AUDIO_CUES+i && enemy_chain.voice.active)pinned=1;
        if(enemy_mine.cue==ENEMY_AUDIO_CUES+i && enemy_mine.voice.active)pinned=1;
        if(enemy_final_voice.cue==ENEMY_AUDIO_CUES+i && enemy_final_voice.voice.active)pinned=1;
        for(unsigned h=0;h<16;h++)if(enemy_guns[h].handle.cue==ENEMY_AUDIO_CUES+i && enemy_guns[h].handle.voice.active)pinned=1;
        if(!pinned && (candidate<0 || enemy_audio_used[i]<enemy_audio_used[candidate]))candidate=i;
    }
    OsbSample *sample=NULL;
    if(candidate>=0){sample=enemy_audio_samples[candidate];enemy_audio_samples[candidate]=NULL;enemy_audio_bytes-=sample->bytes;
        if(enemy_audio_pinned[candidate]){enemy_audio_pinned[candidate]=0;enemy_audio_pin_bytes-=sample->bytes;}}
    unlock_scene();osb_parts_free(sample,1);return candidate>=0;
}
static int enemy_audio_evict(unsigned keep){return enemy_audio_reclaim(keep,0);}
static int enemy_audio_prepare(unsigned i)
{
    const EventAudioCue *c=ENEMY_AUDIO_CUES+i;
    enemy_audio_used[i]=++enemy_audio_clock;
    if(c->backend==0 && enemy_audio_samples[i])return 0;
    if(c->backend==1 && enemy_sequence_image)return 0;
    FILE *f=fopen("enemyaudio.bin","rb");
    if(!f)return -1;int error=-2;
    if(c->backend==0) {
        unsigned r[2];OsbSample *sample=NULL;
        if(!event_audio_record(f,c->file,r)) {
            error=osb_read_range(f,r,c->program,1,&sample);
            /* Residency is a performance preference, never a reason to lose
               a requested voice under memory pressure. Retire only idle
               pins after the ordinary cache has been exhausted. */
            while(error==-4 && (enemy_audio_evict(i) || (!enemy_audio_preloading && enemy_audio_reclaim(i,1))))
                error=osb_read_range(f,r,c->program,1,&sample);
        }
        if(!error){lock_scene();enemy_audio_samples[i]=sample;enemy_audio_bytes+=sample->bytes;unlock_scene();
            while(enemy_audio_bytes>65536+enemy_audio_pin_bytes && enemy_audio_evict(i));}
        else osb_parts_free(sample,1);
    } else if(c->backend==1) {
        unsigned ns=0,np=0;unsigned char *s=event_audio_read(f,c->file,&ns),*p=event_audio_read(f,c->patch_file,&np);
        Smsb check;
        if(s && p && !smsb_bind(&check,s,ns,p,np)) {
            lock_scene();enemy_sequence_image=s;enemy_patch_image=p;enemy_sequence_size=ns;enemy_patch_size=np;unlock_scene();error=0;
        } else {free(s);free(p);}
    }
    fclose(f);return error;
}
static void enemy_audio_plan(unsigned char plan[ENEMY_AUDIO_CUE_COUNT],unsigned reference,unsigned mode)
{
    for(unsigned i=0;i<ENEMY_AUDIO_CUE_COUNT;i++)if(ENEMY_AUDIO_CUES[i].reference==reference){
        if(plan[i]<mode)plan[i]=(unsigned char)mode;return;
    }
}
int audio_preload_enemy_attacks(uint64_t kinds)
{
    /* Mapping follows the live attack emitters, not every voice in a bank.
       24/25 reuse Rocket/Fire; the fixed helicopter task 32 uses variant15. */
    if(kinds&(UINT64_C(1)<<24))kinds|=UINT64_C(1)<<8;
    if(kinds&(UINT64_C(1)<<25))kinds|=UINT64_C(1)<<9;
    if(kinds&(UINT64_C(1)<<30))kinds|=UINT64_C(1)<<10;
    if(kinds&(UINT64_C(1)<<32))kinds|=UINT64_C(1)<<15;
    unsigned char plan[ENEMY_AUDIO_CUE_COUNT]={0};
#define HAS(k) (kinds&(UINT64_C(1)<<(k)))
#define PIN(r) enemy_audio_plan(plan,(r),2)
#define WARM(r) enemy_audio_plan(plan,(r),1)
    if(HAS(17)){enemy_audio_plan(plan,0x110005,3);enemy_audio_plan(plan,0x110006,3);WARM(0x110002);WARM(0x110003);WARM(0x110004);}
    if(HAS(0) || HAS(26)){PIN(0xb);WARM(4);WARM(13);}
    if(HAS(3) || HAS(29)){PIN(0x30008);WARM(0x30003);WARM(0x30009);}
    if(HAS(6)){PIN(0x60007);WARM(0x60000);WARM(0x60002);WARM(0x60003);}
    if(HAS(7) || HAS(28)){PIN(0x7000d);WARM(0x7000c);}
    if(HAS(4))PIN(0x1000c);
    if(HAS(27)){PIN(0x1000d);WARM(0x10006);WARM(0x10007);}
    if(HAS(8))PIN(0x80004);
    if(HAS(9))WARM(0x90001);
    if(HAS(10)){PIN(0xa0000);PIN(0xa0002);}
    for(unsigned kind=13;kind<=15;kind+=2)if(HAS(kind))
        for(unsigned cue=0;cue<4;cue++)PIN((kind<<16)|cue);
    if(HAS(21)){PIN(0x150004);PIN(0x150005);PIN(0x150009);PIN(0x15000a);}
    if(HAS(22)){PIN(0x160000);PIN(0x160001);PIN(0x160006);}
    /* Dogs and bomb/mines share the original sequence/patch pair. */
    if(HAS(2))PIN(0xffff0007u);
    if(HAS(5)){WARM(0x50001);WARM(0x50002);PIN(0xffff0010u);}
    if(HAS(31))PIN(0xffff000fu);
    /* Bullets share that same sequence pair for their impact sound. */
    if(HAS(7) || HAS(8) || HAS(13) || HAS(15) || HAS(16) || HAS(17) ||
       HAS(26) || HAS(28) || HAS(29))PIN(0xffff0011u);
#undef HAS
#undef PIN
#undef WARM
    lock_scene();memset(enemy_audio_pinned,0,sizeof enemy_audio_pinned);enemy_audio_pin_bytes=0;
    enemy_audio_preload=(AudioEnemyPreloadInfo){0};unlock_scene();
    enemy_audio_preloading=1;
    /* Pin before warming speech, so preloading a shout cannot evict a gun. */
    for(unsigned mode=3;mode;mode--)for(unsigned i=0;i<ENEMY_AUDIO_CUE_COUNT;i++)if(plan[i]==mode) {
        enemy_audio_preload.planned_cues++;
        int error=enemy_audio_prepare(i);
        if(error){enemy_audio_preload.failures++;enemy_audio_preload.last_error=error;continue;}
        if(ENEMY_AUDIO_CUES[i].backend)continue;
        if(mode>=2) {
            unsigned bytes=enemy_audio_samples[i]->bytes;
            if(bytes>ENEMY_AUDIO_PIN_LIMIT-enemy_audio_pin_bytes){enemy_audio_preload.failures++;enemy_audio_preload.last_error=-4;continue;}
            lock_scene();enemy_audio_pinned[i]=1;enemy_audio_pin_bytes+=bytes;
            enemy_audio_preload.pinned_samples++;unlock_scene();
        } else enemy_audio_preload.warm_samples++;
    }
    enemy_audio_preloading=0;
    while(enemy_audio_bytes>65536+enemy_audio_pin_bytes && enemy_audio_evict(ENEMY_AUDIO_CUE_COUNT));
    for(unsigned i=0;i<ENEMY_AUDIO_CUE_COUNT;i++)if(plan[i] &&
        !(ENEMY_AUDIO_CUES[i].backend ? enemy_sequence_image!=NULL:enemy_audio_samples[i]!=NULL))enemy_audio_preload.missing_cues++;
    enemy_audio_preload.pinned_bytes=enemy_audio_pin_bytes;
    enemy_audio_preload.sequence_bytes=enemy_sequence_image ? enemy_sequence_size+enemy_patch_size:0;
    return enemy_audio_preload.failures ? -1:0;
}
void audio_enemy_preload_info(AudioEnemyPreloadInfo *out)
{
    if(out){lock_scene();*out=enemy_audio_preload;out->pinned_bytes=enemy_audio_pin_bytes;out->pinned_samples=0;
        for(unsigned i=0;i<ENEMY_AUDIO_CUE_COUNT;i++)out->pinned_samples+=enemy_audio_pinned[i]!=0;
        unlock_scene();}
}
static int enemy_audio_start(int reference,const float position[3],EventAudioHandle *retained)
{
    if(scene_lock<0 || !position || world_suspended)return -1;
    unsigned i=0;while(i<ENEMY_AUDIO_CUE_COUNT && ENEMY_AUDIO_CUES[i].reference!=(unsigned)reference)i++;
    if(i==ENEMY_AUDIO_CUE_COUNT)return -2;
    const EventAudioCue *c=ENEMY_AUDIO_CUES+i;
    SourceSoundPosition pos=world_atan?source_sound_position(position,world_listener,world_yaw,world_atan):(SourceSoundPosition){0};
    SourceAudioContext context={.mode=1,.bank=c->logical_bank,.program=c->program};source_audio_mix_context(&audio_mix,&context);
    SourceAudioLevels levels=source_audio_levels(&c->profile,&context,pos.distance,pos.angle,pos.sign);
    /* Outside the original audible range, do not load a sample just to
       emit a silent one-shot. No gameplay lifetime depends on this handle. */
    /* Goji's next taunt waits for this transport to finish, even when
       inaudible. Do not turn its retained lifetime into a delayed start. */
    if(retained!=&enemy_final_voice && c->profile.range>0 && pos.distance>=c->profile.range)return 0;
    int error=enemy_audio_prepare(i);if(error)return error;
    lock_scene();EventAudioHandle *h=retained?retained:enemy_audio_handles+(enemy_audio_cursor++%ENEMY_AUDIO_HANDLES);
    audio_channel_release(h);memset(h,0,sizeof *h);h->cue=c;
    int slot=audio_channel_acquire_class(h,c->channel,source_audio_protected(c->logical_bank,c->program));
    if(slot<0){h->finished=1;unlock_scene();return 0;}
    AudioChannelBackend *b=audio_backends+slot;b->finished=&h->finished;
    if(c->backend==0) {
        OsbSample *sample=enemy_audio_samples[i];b->voice=&h->voice;b->header=sample->header;b->level=&h->level;b->total=&h->total;
        audio_osb_start_header(&h->voice,&h->envelope,&h->level,&h->total,sample->header,&sample->program);
        audio_channel_levels(&h->voice,levels.volume,levels.pan);
    } else {
        error=smsb_bind(&enemy_sequence,enemy_sequence_image,enemy_sequence_size,enemy_patch_image,enemy_patch_size);
        if(!error){enemy_sequence.patch_bank=c->patch_bank;enemy_sequence.allowed_programs=1u<<c->program;error=smsb_start(&enemy_sequence,0,c->program);}
        b->sequence=&enemy_sequence;b->stream=0;h->sequence=&enemy_sequence;
        smsb_volume(&enemy_sequence,0,levels.volume);smsb_pan(&enemy_sequence,0,levels.pan);
        if(error)audio_channel_release(h);
    }
    unlock_scene();return error;
}

int audio_enemy_sound(int reference,const float position[3])
{return enemy_audio_start(reference,position,NULL);}
int audio_final_boss_voice(int reference,const float position[3])
{
    EventAudioHandle *h=&enemy_final_voice;
    lock_scene();
    if(reference==-2 || world_suspended || h->finished || (h->cue && !h->voice.active)) {
        audio_channel_release(h);memset(h,0,sizeof *h);
    }
    int playing=h->cue && h->voice.active;
    if(playing && position) {
        const EventAudioCue *c=h->cue;
        SourceSoundPosition p=world_atan?source_sound_position(position,world_listener,world_yaw,world_atan):(SourceSoundPosition){0};
        SourceAudioContext ctx={.mode=1,.bank=c->logical_bank,.program=c->program};source_audio_mix_context(&audio_mix,&ctx);
        SourceAudioLevels levels=source_audio_levels(&c->profile,&ctx,p.distance,p.angle,p.sign);
        audio_channel_levels(&h->voice,levels.volume,levels.pan);
    }
    int blocked=world_suspended;unlock_scene();
    if(reference<0 || playing || blocked || !position)return playing;
    if(reference!=0x170000 && reference!=0x170001 && reference!=0x170003 && reference!=0x170005)return -1;
    int error=enemy_audio_start(reference,position,h);
    if(error)return error;
    lock_scene();playing=h->cue && h->voice.active;unlock_scene();return playing;
}
/* Original 0ef8e0/0efb5c slot 10: closest request inside 500 units,
   retain its sample while requested, release on the first absent tick. */
void audio_enemy_chain_offer(const float position[3])
{
    if(!position || !world_atan || world_suspended)return;
    SourceSoundPosition p=source_sound_position(position,world_listener,world_yaw,world_atan);
    if(p.distance<enemy_chain_distance){memcpy(enemy_chain_position,position,12);enemy_chain_distance=p.distance;enemy_chain_requested=1;}
}
void audio_enemy_chain_tick(int reset)
{
    int start=0;lock_scene();
    if(reset || !enemy_chain_requested || world_suspended) {
        audio_channel_release(&enemy_chain);memset(&enemy_chain,0,sizeof enemy_chain);
    } else if(!enemy_chain.cue)start=1;
    else if(enemy_chain.finished) {
        /* The original retires an ended handle now, restarts next tick. */
        audio_channel_release(&enemy_chain);memset(&enemy_chain,0,sizeof enemy_chain);
    } else {
        const EventAudioCue *c=enemy_chain.cue;
        SourceSoundPosition pos=source_sound_position(enemy_chain_position,world_listener,world_yaw,world_atan);
        SourceAudioContext ctx={.mode=1,.bank=c->logical_bank,.program=c->program};source_audio_mix_context(&audio_mix,&ctx);
        SourceAudioLevels levels=source_audio_levels(&c->profile,&ctx,pos.distance,pos.angle,pos.sign);
        audio_channel_levels(&enemy_chain.voice,levels.volume,levels.pan);
    }
    unlock_scene();
    if(start)enemy_audio_start(0x150009,enemy_chain_position,&enemy_chain);
    enemy_chain_requested=0;enemy_chain_distance=500.f;
}

/* 0ef8e0/0efb5c slot 7. Only the nearest armed mine owns a transport. */
void audio_enemy_mine_offer(const float position[3])
{
    if(!position || !world_atan || world_suspended)return;
    SourceSoundPosition p=source_sound_position(position,world_listener,world_yaw,world_atan);
    if(p.distance<enemy_mine_distance){memcpy(enemy_mine_position,position,12);enemy_mine_distance=p.distance;enemy_mine_requested=1;}
}
/* 09ab40 retains +1a0 and 0938d4 retains +174 until the burst ends. Tokens survive compacting
   the actor array; absence retires a transport on this same logic tick. */
unsigned audio_enemy_gun_offer(unsigned owner,int reference,const float position[3])
{
    static unsigned next_owner;
    if(!position || world_suspended)return owner;
    if(!owner){if(++next_owner==0)++next_owner;owner=next_owner;}
    int slot=-1;
    for(int i=0;i<16;i++)if(enemy_guns[i].owner==owner){slot=i;break;}
    if(slot<0)for(int i=0;i<16;i++)if(!enemy_guns[i].owner){slot=i;break;}
    if(slot<0)return owner;
    enemy_guns[slot].owner=owner;enemy_guns[slot].reference=reference;enemy_guns[slot].requested=1;
    memcpy(enemy_guns[slot].position,position,12);
    return owner;
}
/* Fire's +260 owner (07d678/077638): start only on the animation entry,
   follow the actor/listener while playing, and clear an ended transport.
   The shared held pool also retires it when the actor disappears. */
unsigned audio_enemy_voice_offer(unsigned owner,int reference,const float position[3])
{
    if(!position || world_suspended)return 0;
    int retained=0,live_reference=reference;
    lock_scene();
    for(unsigned i=0;owner && i<16;i++)if(enemy_guns[i].owner==owner) {
        EventAudioHandle *h=&enemy_guns[i].handle;
        if(h->cue && (h->finished || (!h->sequence && !h->voice.active))) {
            audio_channel_release(h);memset(enemy_guns+i,0,sizeof enemy_guns[i]);
        } else {retained=1;live_reference=enemy_guns[i].reference;}
        break;
    }
    unlock_scene();
    if(!retained){owner=0;if(reference<0)return 0;}
    return audio_enemy_gun_offer(owner,retained?live_reference:reference,position);
}
static void enemy_audio_held_tick(EventAudioHandle *h,int reference,const float *position,int requested,int reset,int restart)
{
    int start=0;lock_scene();
    if(reset || !requested || world_suspended){audio_channel_release(h);memset(h,0,sizeof *h);}
    else if(!h->cue)start=1;
    else if(h->finished) {
        if(restart){audio_channel_release(h);memset(h,0,sizeof *h);}
    } else {
        const EventAudioCue *c=h->cue;
        SourceSoundPosition pos=world_atan?source_sound_position(position,world_listener,world_yaw,world_atan):(SourceSoundPosition){0};
        SourceAudioContext ctx={.mode=1,.bank=c->logical_bank,.program=c->program};source_audio_mix_context(&audio_mix,&ctx);
        SourceAudioLevels levels=source_audio_levels(&c->profile,&ctx,pos.distance,pos.angle,pos.sign);
        audio_channel_levels(&h->voice,levels.volume,levels.pan);
    }
    unlock_scene();if(start)enemy_audio_start(reference,position,h);
}
void audio_enemy_held_tick(int reset)
{
    enemy_audio_held_tick(&enemy_mine,(int)0xffff000fu,enemy_mine_position,enemy_mine_requested,reset,1);
    enemy_mine_requested=0;enemy_mine_distance=500.f;
    for(unsigned i=0;i<16;i++)if(enemy_guns[i].owner) {
        enemy_audio_held_tick(&enemy_guns[i].handle,enemy_guns[i].reference,enemy_guns[i].position,enemy_guns[i].requested,reset,0);
        if(reset || !enemy_guns[i].requested)enemy_guns[i].owner=0;
        enemy_guns[i].requested=0;
    }
}
