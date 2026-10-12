/* 05d000/05d0d4 wrappers in 1ba..1d8. Their lifetime is separate from
   the shared SOSP channels: PLAY uses channel2; grind/effects use 0/1. */
#include "player_sound.h"
#define PLAYER_AUDIO_HANDLES 31
typedef struct {
    OsbVoice voice;SmsbEnvelope envelope;unsigned level,total;
    int used,kind,program,follow_head,finished;
} PlayerAudioHandle;
static PlayerAudioHandle player_audio[PLAYER_AUDIO_HANDLES];
static unsigned char *player_audio_bank[2];static unsigned player_audio_length[2];
static OsbSample *player_audio_parts[2];
static FILE *player_sample_file;
static unsigned player_sample_generation;
static int player_sample_cached;
static unsigned player_sample_base[2],player_sample_budget,player_sample_clock,player_sample_used[2][19];
static AudioPlayerCacheInfo player_cache;
void audio_player_cache(unsigned bytes){audio_free_player();player_sample_budget=bytes;}
void audio_player_cache_info(AudioPlayerCacheInfo *out)
{
    lock_scene();*out=player_cache;out->cached=player_sample_budget!=0;
    for(unsigned bank=0;bank<2;bank++) {
        unsigned count=bank?16:19;
        if(player_audio_bank[bank]) {
            out->resident_bytes+=player_audio_length[bank];out->resident_samples+=count;
        } else if(player_audio_parts[bank]) {
            out->resident_bytes+=count*sizeof(OsbSample);
            for(unsigned i=0;i<count;i++) {
                const OsbSample *p=player_audio_parts[bank]+i;
                if(p->samples)out->resident_samples++;
                if(p->owned)out->resident_bytes+=p->bytes;
            }
        }
    }
    unlock_scene();
}
static int player_program(unsigned bank,unsigned index,OsbProgram *p)
{
    if(bank>1 || index>=(bank?16u:19u))return -1;
    if(player_audio_parts[bank]){*p=player_audio_parts[bank][index].program;return p->samples?0:-3;}
    return osb_program(player_audio_bank[bank],player_audio_length[bank],index,p);
}
static const unsigned char *player_program_header(unsigned bank,unsigned index)
{
    if(player_audio_parts[bank])return player_audio_parts[bank][index].header;
    unsigned offset;memcpy(&offset,player_audio_bank[bank]+16+index*4,4);return player_audio_bank[bank]+offset;
}
static int player_audio_character=-1;
static int player_audio_suspended;
static unsigned player_audio_starts[5],player_audio_errors;
static SourceSoundPosition player_audio_position[2];
static void player_audio_levels(PlayerAudioHandle *h)
{
    unsigned bank=h->kind==PLAYER_SOUND_VOICE;
    /* Controller mode 1 selects the independent SOSB balance. The
       original near-field is 50 units (1000*.05), not BGM attenuation. */
    SourceAudioProfile profile={0,1000.f,.5f,.05f};
    SourceAudioContext context={.mode=1,.bank=bank?6:4,.program=h->program};
    source_audio_mix_context(&audio_mix,&context);
    const SourceSoundPosition *p=player_audio_position+(bank || h->follow_head);
    SourceAudioLevels levels=source_audio_levels(&profile,&context,p->distance,p->angle,p->sign);
    audio_channel_levels(&h->voice,levels.volume,levels.pan);
}
void audio_player_position(const float body[3],const float head[3],const float listener[3],int yaw,
                           int (*atan_bam)(float,float))
{
    SourceSoundPosition p[2]={source_sound_position(body,listener,yaw,atan_bam),
                             source_sound_position(head,listener,yaw,atan_bam)};
    lock_scene();memcpy(player_audio_position,p,sizeof p);
    for(unsigned i=0;i<PLAYER_AUDIO_HANDLES;i++)if(player_audio[i].used){
        /* 05d2f8/330 create at +75c; 05d43c/47c update from +7d4. */
        player_audio[i].follow_head=player_audio[i].kind>=PLAYER_SOUND_GRIND;
        player_audio_levels(player_audio+i);
    }
    unlock_scene();
}
void audio_player_stop(void)
{lock_scene();for(unsigned i=0;i<PLAYER_AUDIO_HANDLES;i++)audio_channel_release(&player_audio[i].voice);memset(player_audio,0,sizeof player_audio);unlock_scene();}
void audio_free_player(void)
{
    audio_player_stop();lock_scene();
    unsigned char *a=player_audio_bank[0],*b=player_audio_bank[1];
    OsbSample *pa=player_audio_parts[0],*pb=player_audio_parts[1];
    player_audio_parts[0]=player_audio_parts[1]=NULL;
    player_audio_bank[0]=player_audio_bank[1]=NULL;
    player_audio_length[0]=player_audio_length[1]=0;player_audio_character=-1;player_audio_suspended=0;
    memset(player_audio_position,0,sizeof player_audio_position);
    FILE *f=player_sample_file;player_sample_file=NULL;player_sample_clock=0;player_sample_cached=0;
    memset(&player_cache,0,sizeof player_cache);memset(player_sample_used,0,sizeof player_sample_used);
    unlock_scene();if(f)fclose(f);free(a);free(b);osb_parts_free(pa,19);osb_parts_free(pb,16);
}
void audio_player_resume(void)
{
    /* Cached PLAY cues were the one long-lived gameplay file omitted by
       the power-resume path. Reopen on the next miss, outside scene_lock;
       resident samples and active/restartable loops keep their ownership. */
    FILE *f=player_sample_file;player_sample_file=NULL;
    player_sample_generation=audio_resume_generation;
    if(f){unsigned phase=hang_mark(HANG_PLAYER_AUDIO_IO);fclose(f);hang_mark(phase);}
}
/* Original 0b1b68 classes: common effects share SOSP0/1 and PLAY voices
   share SOSP2. Keep their original compressed bytes in one cache instead
   of permanently loading all 19 common effects (125927 bytes) PLUS PLAY.
   Main-thread misses never hold the mixer lock during Memory Stick I/O.
   Active samples and suspended, restartable loops remain pinned. free()
   also defers retirement while an in-flight ME job borrows old samples. */
static int player_sample_evict(void)
{
    int candidate=-1;unsigned oldest=0;lock_scene();
    for(unsigned bank=0;bank<2;bank++)for(unsigned i=0;i<(bank?16u:19u);i++) {
        if(!player_audio_parts[bank] || !player_audio_parts[bank][i].samples)continue;
        int pinned=0;
        for(unsigned h=0;h<PLAYER_AUDIO_HANDLES;h++) {
            PlayerAudioHandle *v=player_audio+h;
            if(v->used && (v->kind==PLAYER_SOUND_VOICE)==bank && v->program==(int)i &&
               (v->voice.active || (!bank && i<=2 && !v->finished)))pinned=1;
        }
        if(!pinned && (candidate<0 || player_sample_used[bank][i]<oldest)) {
            candidate=(int)(bank*19+i);oldest=player_sample_used[bank][i];
        }
    }
    unsigned char *data=NULL;
    if(candidate>=0) {
        OsbSample *p=player_audio_parts[candidate/19]+candidate%19;data=p->samples;
        player_cache.bytes-=p->bytes;player_cache.evictions++;
        p->samples=NULL;p->program.samples=NULL;p->owned=0;
    }
    unlock_scene();free(data);return candidate>=0;
}
static int player_prepare_sample(unsigned bank,unsigned index)
{
    if(!player_sample_cached)return 0;
    if(bank>1 || index>=(bank?16u:19u))return -1;
    OsbSample *p=player_audio_parts[bank]+index;player_sample_used[bank][index]=++player_sample_clock;
    if(player_sample_generation!=audio_resume_generation)audio_player_resume();
    if(p->samples)return 0;
    if(!player_sample_file) {
        unsigned phase=hang_mark(HANG_PLAYER_AUDIO_IO);
        player_sample_file=fopen("voices.bin","rb");
        hang_mark(phase);
        if(!player_sample_file)return -2; /* retry later; cached cues still work */
    }
    /* A new cue is loaded before the source channel allocator replaces
       the previous cue. Never evict that still-playing predecessor just
       to enforce the cache target; its bytes become reclaimable afterward. */
    while(player_cache.bytes+p->bytes>player_sample_budget && player_sample_evict());
    unsigned char *data=malloc(p->bytes);
    while(!data && player_sample_evict())data=malloc(p->bytes);
    if(!data)return -4;
    unsigned started=JGR_PROFILE_CLOCK();
    unsigned phase=hang_mark(HANG_PLAYER_AUDIO_IO);
    int error=fseek(player_sample_file,player_sample_base[bank]+p->program.offset,SEEK_SET) ||
              fread(data,1,p->bytes,player_sample_file)!=p->bytes;
    hang_mark(phase);
    unsigned elapsed=JGR_PROFILE_CLOCK()-started;
    if(error){free(data);return -3;}
    lock_scene();p->samples=data;p->program.samples=data;p->owned=1;
    player_cache.bytes+=p->bytes;player_cache.loads++;player_cache.read_us+=elapsed;
    if(player_cache.bytes>player_cache.peak_bytes)player_cache.peak_bytes=player_cache.bytes;
    if(elapsed>player_cache.max_read_us)player_cache.max_read_us=elapsed;
    unlock_scene();return 0;
}
static int player_load_cached(FILE *f,const unsigned rec[17][2],int character)
{
    OsbSample *parts[2]={0};int error=-3;
    if(fseek(f,0,SEEK_END))return error;
    long file_size=ftell(f);if(file_size<144)return error;
    for(unsigned bank=0;bank<2;bank++) {
        const unsigned *r=rec[bank?character+1:0];unsigned count=bank?16:19,header[4],offset[19];
        if(r[0]<144 || r[1]<16+count*4 || r[1]>0x200000 ||
           r[0]>(unsigned long)file_size || r[1]>(unsigned long)file_size-r[0] ||
           fseek(f,r[0],SEEK_SET) || fread(header,16,1,f)!=1 ||
           memcmp(header,"SOSB",4) || header[3]!=count || fread(offset,4,count,f)!=count)goto failed;
        parts[bank]=calloc(count,sizeof(OsbSample));if(!parts[bank]){error=-4;goto failed;}
        for(unsigned i=0;i<count;i++) {
            OsbSample *p=parts[bank]+i;
            if(offset[i]>r[1] || r[1]-offset[i]<64 || fseek(f,r[0]+offset[i],SEEK_SET) || fread(p->header,64,1,f)!=1)goto failed;
            unsigned short control,low,length;memcpy(&control,p->header+4,2);memcpy(&low,p->header+6,2);memcpy(&length,p->header+10,2);
            unsigned start=((control&127u)<<16)|low,format=(control>>7)&3;
            if(format!=0 && format!=2)goto failed;
            p->bytes=format==2?(length+1u)/2:length*2u;
            if(!length || start>r[1] || p->bytes>r[1]-start || osb_program_parts(p->header,64,p->header,p->bytes,&p->program))goto failed;
            p->program.samples=NULL;
        }
    }
    lock_scene();player_audio_parts[0]=parts[0];player_audio_parts[1]=parts[1];
    player_sample_file=f;player_sample_cached=1;player_sample_generation=audio_resume_generation;
    player_sample_base[0]=rec[0][0];player_sample_base[1]=rec[character+1][0];
    player_audio_character=character;memset(player_audio_starts,0,sizeof player_audio_starts);player_audio_errors=0;unlock_scene();
    /* Warm both grind surfaces and the spray loop before gameplay. */
    for(unsigned i=0;i<3;i++)if((error=player_prepare_sample(0,i))) {
        /* Caller owns f on failure, audio_free_player owns it on success. */
        player_sample_file=NULL;audio_free_player();return error;
    }
    return 0;
failed:osb_parts_free(parts[0],19);osb_parts_free(parts[1],16);return error;
}
static int player_load_parts(FILE *f,const unsigned rec[17][2],int character)
{
    OsbSample *parts[2]={0};int error=0;
    for(unsigned bank=0;bank<2;bank++)
        if((error=osb_read_parts(f,rec[bank?character+1:0],bank?16:19,parts+bank)))goto done;
    lock_scene();player_audio_parts[0]=parts[0];player_audio_parts[1]=parts[1];parts[0]=parts[1]=NULL;
    player_audio_character=character;memset(player_audio_starts,0,sizeof player_audio_starts);player_audio_errors=0;
    unlock_scene();
done:osb_parts_free(parts[0],19);osb_parts_free(parts[1],16);return error;
}

int audio_load_player(int character)
{
    if((unsigned)character>=16 || scene_lock<0)return -1;
    if(character==player_audio_character)return 0;
    /* No old-character sound may survive a failed character replacement. */
    audio_free_player();
    FILE *f=fopen("voices.bin","rb");if(!f)return -2;
    unsigned head[2],rec[17][2],sizes[2]={0};unsigned char *data[2]={0};int error=-3;
    if(fread(head,8,1,f)!=1 || memcmp(head,"OSV1",4) || head[1]!=17 || fread(rec,sizeof rec,1,f)!=1)goto done;
    if(player_sample_budget){error=player_load_cached(f,rec,character);if(!error)return 0;fclose(f);return error;}
    /* A scene handoff leaves several free heap spans. Reserve the larger
       PLAY bank first so the smaller common bank cannot consume its only
       sufficiently large span. Bank IDs and sample bytes stay unchanged. */
    int first=rec[character+1][1]>rec[0][1] ? 1:0;
    for(int pass=0;pass<2;pass++){
        int bank=first^pass;
        unsigned *r=rec[bank?character+1:0];sizes[bank]=r[1];
        if(r[0]<144 || r[1]<16 || r[1]>0x200000)goto done;
        data[bank]=malloc(r[1]);
        if(!data[bank]){error=-4-bank;goto done;}
        if(fseek(f,r[0],SEEK_SET) || fread(data[bank],1,r[1],f)!=r[1])goto done;
        for(unsigned program=0;program<(bank?16:19);program++){
            OsbProgram p;if(osb_program(data[bank],r[1],program,&p))goto done;
        }
    }
    lock_scene();
    for(int bank=0;bank<2;bank++){player_audio_bank[bank]=data[bank];player_audio_length[bank]=sizes[bank];data[bank]=NULL;}
    player_audio_character=character;memset(player_audio_starts,0,sizeof player_audio_starts);player_audio_errors=0;
    unlock_scene();error=0;
done:
    free(data[0]);free(data[1]);
    /* Preserve source headers and compressed samples, but let each sample
       occupy its own small heap span when a whole bank cannot fit. */
    if(error==-4 || error==-5)error=player_load_parts(f,rec,character);
    fclose(f);return error;
}
int audio_player_sound(int kind,int program)
{
    if(kind==PLAYER_SOUND_RESET){audio_player_stop();return 0;}
    if(kind<PLAYER_SOUND_VOICE || kind>PLAYER_SOUND_SPRAY)return -1;
    lock_scene();int loop=kind>=PLAYER_SOUND_GRIND,slot=-1,error=0;
    for(int i=0;i<PLAYER_AUDIO_HANDLES;i++){
        PlayerAudioHandle *h=player_audio+i;
        /* A loop wrapper remains allocated until its animation clears it;
           an exhausted one-shot is reusable without stealing a live cue. */
        if(h->used && h->kind<PLAYER_SOUND_GRIND && !h->voice.active){audio_channel_release(&h->voice);h->used=0;}
        if(loop && h->used && h->kind==kind){
            if(program<0){audio_channel_release(&h->voice);memset(h,0,sizeof *h);}
            /* Original +874 does not swap programs if the surface changes. */
            unlock_scene();return 0;
        }
        if(slot<0 && !h->used)slot=i;
    }
    if(program<0){unlock_scene();return loop?0:-1;}
    if(player_audio_suspended){unlock_scene();return 0;}
    unsigned bank=kind==PLAYER_SOUND_VOICE;OsbProgram p;
    if((loop && ((kind==PLAYER_SOUND_GRIND && program!=1 && program!=2) ||
                 (kind==PLAYER_SOUND_SPRAY && program!=0))) ||
       (unsigned)program>=(bank?16u:19u))error=-1;
    else if(slot<0)error=-4;
    else {
        /* Loop owners are checked above: a repeated animation tick must
           not reload a displaced/finished loop or allocate another cue. */
        unlock_scene();error=player_prepare_sample(bank,(unsigned)program);lock_scene();
        if(!error)error=player_program(bank,program,&p);
    }
    if(!error){
        PlayerAudioHandle *h=player_audio+slot;h->used=1;h->kind=kind;h->program=program;h->follow_head=0;
        if(audio_managed_osb_header(&h->voice,&h->envelope,&h->level,&h->total,bank?6:4,program,player_program_header(bank,program),&p,&h->finished)){
            player_audio_levels(h);player_audio_starts[kind]++;
        }else h->finished=1;
    }else player_audio_errors++;
    unlock_scene();return error;
}
void audio_player_suspend(int suspended)
{
    /* Common manager 0e6c8c -> 0e6ec0/0e7380: stop, not a paused PCM
       cursor. Profiles with +14=-1 (bank4 programs 0/1/2) retain their
       owner and key on again after resume; one-shots retire. */
    lock_scene();suspended=!!suspended;
    if(suspended!=player_audio_suspended){
        for(int i=0;i<PLAYER_AUDIO_HANDLES;i++){
            PlayerAudioHandle *h=player_audio+i;if(!h->used)continue;
            if(suspended){
                int ended=h->finished;audio_channel_release(&h->voice);
                if(h->kind==PLAYER_SOUND_VOICE || h->program>2 || ended)memset(h,0,sizeof *h);
                else h->finished=0; /* retained profile; restart on resume */
            }else{
                unsigned bank=h->kind==PLAYER_SOUND_VOICE;OsbProgram p;
                if(!player_program(bank,h->program,&p)){
                    if(audio_managed_osb_header(&h->voice,&h->envelope,&h->level,&h->total,bank?6:4,h->program,player_program_header(bank,h->program),&p,&h->finished)){
                        player_audio_levels(h);player_audio_starts[h->kind]++;
                    }else h->finished=1;
                }else {memset(h,0,sizeof *h);player_audio_errors++;}
            }
        }
        player_audio_suspended=suspended;
    }
    unlock_scene();
}
void audio_player_info(unsigned starts[5],unsigned *errors,unsigned *active)
{
    lock_scene();memcpy(starts,player_audio_starts,sizeof player_audio_starts);
    *errors=player_audio_errors;*active=0;
    for(unsigned i=0;i<PLAYER_AUDIO_HANDLES;i++)*active+=player_audio[i].voice.active!=0;
    unlock_scene();
}
