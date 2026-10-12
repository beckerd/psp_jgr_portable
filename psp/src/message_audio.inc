/* One radio reader, one current request, and one playing line. Replacement
   cancels at the next bounded read; only the main thread starts playback.
   The worker never holds scene_lock during file I/O or heap allocation. */
static unsigned message_serial,message_completed;
static int message_requested=-1,message_start_requested,message_load_error;
#if defined(__PSP__) || defined(JGR_MESSAGE_ASYNC_TEST)
#define MESSAGE_WORKER 1
static int message_thread=-1,message_wake=-1,message_quit,message_thread_done;
#else
#define MESSAGE_WORKER 0
#endif
static void message_image_free(MessageImage *image)
{if(image){for(int i=0;i<MESSAGE_CHUNKS;i++)free(image->chunk[i]);free(image);}}
static void message_image_free_private(MessageImage *image)
{if(image){for(int i=0;i<MESSAGE_CHUNKS;i++)message_private_free(image->chunk[i]);message_private_free(image);}}
/* Called with scene_lock held. Ready/playing storage is detached separately;
   cancelling a private in-flight read never waits for the Memory Stick. */
static void message_cancel_locked(void)
{message_serial++;message_requested=-1;message_start_requested=message_load_error=0;}
static int message_read_cancelled(unsigned serial,unsigned power,int asynchronous)
{
    if(!asynchronous)return 0;
    lock_scene();int cancelled=serial!=message_serial || message_requested<0 ||
        audio_exiting || audio_suspended || power!=audio_resume_generation;
    unlock_scene();return cancelled;
}
static int message_read(int reference,unsigned serial,unsigned power,int asynchronous,
                        MessageImage **image,MovieVoice *voice)
{
    *image=NULL;memset(voice,0,sizeof *voice);
    if(message_read_cancelled(serial,power,asynchronous))return -7;
    char path[40];snprintf(path,sizeof path,"messagevoice%03d.adx",reference);
    FILE *f=fopen(path,"rb");if(!f)return -2;
    /* Read straight into each small owned chunk; avoid another stdio buffer
       competing with the fragmented gameplay heap. */
    setvbuf(f,NULL,_IONBF,0);
    MessageImage *data=NULL;int error=-3;long length;
    if(fseek(f,0,SEEK_END) || (length=ftell(f))<36 || length>0x100000 || fseek(f,0,SEEK_SET))goto done;
    unsigned char header[36];unsigned start,samples;
    if(fread(header,1,sizeof header,f)!=sizeof header ||
       !movie_voice_format(header,sizeof header,(unsigned)length,&start,&samples) ||
       fseek(f,start,SEEK_SET)){error=-5;goto done;}
    unsigned blocks=(samples+31)/32;
    if(blocks>MESSAGE_CHUNKS*MESSAGE_CHUNK_BLOCKS){error=-5;goto done;}
    if(message_read_cancelled(serial,power,asynchronous)){error=-7;goto done;}
    data=calloc(1,sizeof *data);if(!data){error=-4;goto done;}
    for(unsigned block=0;block<blocks;block+=MESSAGE_CHUNK_BLOCKS){
        if(message_read_cancelled(serial,power,asynchronous)){error=-7;goto done;}
        unsigned n=blocks-block;if(n>MESSAGE_CHUNK_BLOCKS)n=MESSAGE_CHUNK_BLOCKS;
        unsigned char *chunk=data->chunk[block/MESSAGE_CHUNK_BLOCKS]=malloc(n*18);
        if(!chunk){error=-4;goto done;}
        if(fread(chunk,18,n,f)!=n){error=-5;goto done;}
    }
    if(message_read_cancelled(serial,power,asynchronous)){error=-7;goto done;}
    voice->count=blocks;voice->samples=samples;voice->ready=1;
    *image=data;data=NULL;error=0;
done:fclose(f);message_image_free_private(data);return error;
}
#if MESSAGE_WORKER
static int message_reader(SceSize size,void *args)
{
    (void)size;(void)args;
    for(;;){
        lock_scene();
        int quit=message_quit || audio_exiting,reference=message_requested;
        unsigned serial=message_serial,power=audio_resume_generation;
        int awaiting=reference>=0 && message_completed!=serial;
        int work=!quit && !audio_suspended && awaiting;
        unlock_scene();
        if(quit)break;
        if(!work){
            /* Idle readers sleep until requested. Only a suspended pending
               request polls for resume, which arrives from a power callback. */
            unsigned timeout=20000;sceKernelWaitSema(message_wake,1,awaiting ? &timeout:NULL);continue;
        }
        MessageImage *image=NULL;MovieVoice voice;
        int error=message_read(reference,serial,power,1,&image,&voice);
        lock_scene();
        if(serial==message_serial && reference==message_requested && !audio_exiting){
            /* A power change invalidates the old file handle. Close it on
               this thread, then retry from zero after resume. */
            if(!audio_suspended && power==audio_resume_generation){
                message_completed=serial;message_load_error=error;
                if(!error){message_image=image;message_voice=voice;message_reference=reference;image=NULL;}
            }
        }
        unlock_scene();message_image_free_private(image);
    }
    lock_scene();message_thread_done=1;unlock_scene();return 0;
}
static int message_worker_init(void)
{
    if(message_thread>=0)return 0;
    if(scene_lock<0 || audio_exiting)return -6;
    message_quit=message_thread_done=0;
    message_wake=sceKernelCreateSema("radio_request",0,0,1,NULL);
    if(message_wake<0)return -6;
    /* Below music (0x18), above gameplay (0x20), so a busy frame cannot
       starve the alert's bounded reads. This thread performs no decoding. */
    message_thread=sceKernelCreateThread("radio_reader",message_reader,0x1c,8192,0,NULL);
    if(message_thread<0 || sceKernelStartThread(message_thread,0,NULL)<0){
        if(message_thread>=0)sceKernelDeleteThread(message_thread);
        sceKernelDeleteSema(message_wake);message_thread=message_wake=-1;return -6;
    }
    return 0;
}
static int message_worker_stop(void)
{
    if(message_thread<0)return 1;
    lock_scene();message_cancel_locked();message_quit=1;int done=message_thread_done;unlock_scene();
    sceKernelSignalSema(message_wake,1);
    if(!done)return 0; /* Existing audio_shutdown caller retries without UAF. */
    sceKernelWaitThreadEnd(message_thread,NULL);sceKernelDeleteThread(message_thread);
    sceKernelDeleteSema(message_wake);message_thread=message_wake=-1;return 1;
}
#else
static int message_worker_init(void){return -6;}
static int message_worker_stop(void){return 1;}
#endif
/* scene_lock held; returned old image must be retired after unlocking. */
static MessageImage *message_start_locked(void)
{
    AUDIO_TOUCH_SIMPLE(2);
    memset(&countdown_audio.playing,0,sizeof countdown_audio.playing);
    MessageImage *old=message_playing_image;message_playing_image=message_image;message_image=NULL;
    message_playing=message_voice;message_playing.active=1;
    memset(&message_voice,0,sizeof message_voice);message_reference=-1;
    message_requested=-1;message_start_requested=0;return old;
}
void audio_message_collect(void)
{
    /* Only the game thread publishes start intent and retires consumed
       chunks. The callback/ME sees immutable, fully prepared payloads. */
    unsigned char *old[MESSAGE_CHUNKS]={0};MessageImage *replaced=NULL;
    lock_scene();
    if(message_start_requested && message_requested>=0 && message_completed==message_serial){
        if(message_image && message_reference==message_requested && message_voice.ready && !message_load_error)
            replaced=message_start_locked();
        else {message_playing.error=1;message_start_requested=0;}
    }
    if(message_playing_image){
        unsigned consumed=message_playing.position/(32*MESSAGE_CHUNK_BLOCKS);
        if(message_playing.position==message_playing.samples)consumed=MESSAGE_CHUNKS;
        for(unsigned i=0;i<consumed;i++){old[i]=message_playing_image->chunk[i];message_playing_image->chunk[i]=NULL;}
    }
    unlock_scene();for(int i=0;i<MESSAGE_CHUNKS;i++)free(old[i]);message_image_free(replaced);
}
static int16_t message_next(void)
{
    MovieVoice *v=&message_playing;
    if(!v->active || !v->ready || v->error)return 0;
    unsigned index=v->position&31;
    if(!index){
        unsigned block=v->position/32;unsigned char *chunk=message_playing_image->chunk[block/MESSAGE_CHUNK_BLOCKS];
        if(!chunk || adx_decode_continuous(chunk+(block%MESSAGE_CHUNK_BLOCKS)*18,18,1,v->decoded,32,v->history,7334,-3283)!=1){v->active=0;v->error=1;return 0;}
    }
    int16_t result=v->decoded[index];if(++v->position>=v->samples)v->active=0;return result;
}
void audio_free_message(void)
{
    lock_scene();message_cancel_locked();AUDIO_TOUCH_SIMPLE(2);
    MessageImage *old=message_image,*playing=message_playing_image;
    message_image=message_playing_image=NULL;message_reference=-1;
    memset(&message_voice,0,sizeof message_voice);memset(&message_playing,0,sizeof message_playing);
    unlock_scene();message_image_free(old);message_image_free(playing);
}
int audio_message_request(int reference)
{
    if(reference<-1 || reference>=37)return -1;
    int error=reference>=0 ? message_worker_init():0;
    lock_scene();message_cancel_locked();
    MessageImage *old=message_image;message_image=NULL;message_reference=-1;
    memset(&message_voice,0,sizeof message_voice);message_load_error=error;
    if(!error)message_requested=reference;
    unlock_scene();message_image_free(old);
#if MESSAGE_WORKER
    if(reference>=0 && !error)sceKernelSignalSema(message_wake,1);
#endif
    return error;
}
int audio_message_prepare(int reference)
{
    /* Explicit synchronous compatibility path; normal gameplay requests
       the worker above. Never truncate a preceding line during the alert. */
    if(reference<-1 || reference>=37)return -1;
    audio_message_collect();
    lock_scene();message_cancel_locked();MessageImage *old=message_image;message_image=NULL;message_reference=-1;
    memset(&message_voice,0,sizeof message_voice);unlock_scene();message_image_free(old);
    if(reference<0)return 0;
    MessageImage *image;MovieVoice voice;int error=message_read(reference,0,0,0,&image,&voice);
    if(error)return error;
    lock_scene();message_image=image;message_voice=voice;message_reference=reference;unlock_scene();return 0;
}
int audio_message_voice(int reference)
{
    if(reference<0){
        lock_scene();memset(&countdown_audio.playing,0,sizeof countdown_audio.playing);unlock_scene();
        audio_free_message();return 0;
    }
    lock_scene();int error=0;MessageImage *old=NULL;
    if(reference==message_reference && message_voice.ready && !message_voice.error)old=message_start_locked();
    else if(reference==message_requested && message_completed!=message_serial){
        /* Preserve the UI's dispatch clock when storage is late. The old
           shared CRI owner stops now; the new line starts when complete. */
        AUDIO_TOUCH_SIMPLE(2);message_start_requested=1;
        memset(&countdown_audio.playing,0,sizeof countdown_audio.playing);
        old=message_playing_image;message_playing_image=NULL;memset(&message_playing,0,sizeof message_playing);
    } else error=-1;
    unlock_scene();message_image_free(old);return error;
}
void audio_message_info(unsigned *position,int *active,int *error)
{lock_scene();*position=message_playing.position;*active=message_playing.active;*error=message_playing.error || message_load_error;unlock_scene();}
