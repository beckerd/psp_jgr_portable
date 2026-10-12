#include "pathaudio.h"
#include "audio.h"
static void input(void *context,const PathInstance *p,PathObjectInput *in)
{
    (void)context;
    in->sound_blocked=audio_world_blocked();
    if(p->task.sound)in->sound_finished=audio_world_finished(p->task.sound);
    else {
        /* Evaluate the pure recovered wedge on a COPY. Only a real trigger
           may allocate; feed its real result back into the authoritative
           tick (including allocation failure and the original disarm). */
        PathObject preview=p->task;
        if(pathobject_tick(&preview,in)&PATH_SOUND_CREATE)
            in->sound_created=audio_world_sequence(p->sound_program,in->position);
    }
}
static void output(void *context,const PathInstance *p,unsigned requests,unsigned sound)
{
    (void)context;
    if(requests&PATH_SOUND_DELETE)audio_world_stop(sound);
    else if(requests&PATH_SOUND_POSITION)audio_world_position(sound,p->task.pos);
}
void pathaudio_bind(PathWorld *w)
{w->sound_input=input;w->sound_output=output; /* retain the campaign's own context */}
