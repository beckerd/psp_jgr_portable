#include "smsb.h"
#include "audio_spatial.h"
#include <string.h>
#if JGR_PORTABLE_ASSETS
#include "source_data_smsb_tables.inc"
#else
#include "generated/source_data/smsb_tables.inc"
#endif
static unsigned rd16(const unsigned char *p){return p[0]|((unsigned)p[1]<<8);}
static unsigned rd32(const unsigned char *p){return rd16(p)|(rd16(p+2)<<16);}
static int fits(unsigned at,unsigned n,unsigned size){return at<=size && n<=size-at;}
static unsigned pitch_bend(unsigned key,const unsigned char *layer,int bend)
{
    /* MANATEE 1c4c. Centred channel/sequence tuning and pitch bend only;
       neither of the audited streams issues bend/tuning controllers. */
    int fine=(int8_t)layer[0x27]+bend;unsigned note=((fine>>8)+key+96-(layer[0x26]&127))&255;
    if(note>=sizeof SMSB_NOTE)return ~0u;
    unsigned packed=SMSB_NOTE[note]<<7,octave=packed&0x7800;
    unsigned index=(((packed<<1)&0xf00)|((unsigned)fine&0xfe))>>1;
    if(index>=sizeof SMSB_PITCH)return ~0u;
    return octave|(SMSB_PITCH[index]+(index>=0x4d9?256:0)+(index>=0x383?256:0)+(index>=0x1ef?256:0));
}
static unsigned pitch(unsigned key,const unsigned char *layer){return pitch_bend(key,layer,0);}
static int patch_parts(const void *image,unsigned size,unsigned program,unsigned key,unsigned velocity,unsigned volume,
               unsigned global_volume,SmsbPatch out[4],const SmsbPatchBank *parts)
{
    const unsigned char *b=image;int n=0;
    if(!b || !out || size<48 || memcmp(b,"SMPB",4) || rd32(b+4)!=2 || rd32(b+8)>size || rd32(b+8)<48 || key>127 || velocity>127 || volume>127)return -1;
    size=rd32(b+8);
    if(!parts && memcmp(b+size-4,"ENDB",4))return -1;
    unsigned metadata=parts ? parts->metadata_size:size;
    unsigned table=rd32(b+16),count=rd32(b+20),curves=rd32(b+24),ncurves=rd32(b+28);
    if(program>=count || count>128 || !fits(table,count*4,metadata))return -2;
    unsigned p=rd32(b+table+program*4);
    if(!fits(p,16,metadata))return -3;
    for(unsigned j=0;j<4;j++) {
        unsigned group=rd32(b+p+j*4);if(!group)continue;
        if(!fits(group,16,metadata) || (rd32(b+group)&0x80))return -4;
        unsigned layers=rd32(b+group)&127,at=rd32(b+group+4);
        if(layers>4 || !fits(at,layers*48,metadata))return -5;
        for(unsigned k=0;k<layers;k++,at+=48) {
            const unsigned char *s=b+at;
            if(key<s[36] || key>s[37] || velocity<s[43] || velocity>s[44])continue;
            if(n==4 || s[45] || s[46] || s[42]>=ncurves || !fits(curves,(s[42]+1)*128,metadata))return -6;
            unsigned ctl=rd16(s),start=((ctl&127)<<16)|rd16(s+2),length=rd16(s+6),ls=rd16(s+4),fmt=(ctl>>7)&3;
            unsigned step_pitch=pitch(key,s),bytes=fmt==2 ? (length+1)/2:length*2;
            if((fmt!=0 && fmt!=2) || !length || ls>=length || !fits(start,bytes,size) || step_pitch==~0u || (rd16(s+10)&0x4000))return -7;
            const unsigned char *samples;
            const unsigned char *const *pages=NULL;
            if(parts) {
                const SmsbWave *wave=parts->waves;
                while(wave && (wave->offset!=start || wave->bytes<bytes))wave=wave->next;
                if(!wave || !wave->samples)return -7;
                samples=wave->samples;
                pages=(const unsigned char *const *)wave->pages;
            } else samples=b+start;
            SmsbPatch *o=out+n++;memset(o,0,sizeof *o);
            o->sample=(OsbProgram){.samples=samples,.pages=pages,.offset=start,.length=length,.loop_start=ls,
                .control=ctl,.pitch=step_pitch,.step=osb_pitch_step(step_pitch)};
            o->sample.dsp=(s[16]&15)|(MANATEE_SEND[(s[16]>>4)*4]<<4); /* ARM 11f0..1254, default controllers */
            unsigned e=rd16(s+8);o->env1=(e&31)|((e>>6&31)<<6)|((e>>11&31)<<11);
            o->env2=rd16(s+10);if(!(o->env2&31))o->env2|=1;
            /* 1468..14f4: velocity curve, controller 7, layer TL, then
               the sequence's signed global volume (127 for refs 3/4). */
            unsigned gain=((b[curves+s[42]*128+velocity]+1)*(volume+1)*(256-s[21])-1)>>14;
            unsigned tl=MANATEE_TL[source_audio_clamp((gain&255)+2*(int)(int8_t)global_volume,255)];
            unsigned pan=MANATEE_PAN[MANATEE_PAN_BASE[rd16(s+18)&31]];
            unsigned send=MANATEE_SEND[(rd16(s+18)>>6)&63];
            o->level=(send<<8)|pan;o->total=(tl<<8)|(s[20]&63);o->layer=at;
        }
    }
    return n;
}
int smsb_patch(const void *image,unsigned size,unsigned program,unsigned key,unsigned velocity,unsigned volume,
               unsigned global_volume,SmsbPatch out[4])
{return patch_parts(image,size,program,key,velocity,volume,global_volume,out,NULL);}
static unsigned rate(unsigned r,unsigned env2,unsigned pitch)
{
    unsigned n=r*2,krs=(env2>>10)&15;int octave=(pitch>>11)&15;if(octave&8)octave-=16;
    if(krs<15){n+=(pitch>>9)&1;int k=(int)krs+octave;if(k>0)n+=2*k;}
    return n>63?63:n;
}
void smsb_envelope_start(SmsbEnvelope *e,unsigned env1,unsigned env2,unsigned pitch)
{
    *e=(SmsbEnvelope){.value=640<<16,.threshold=((env2>>5)&31)<<5,.link=(env2>>14)&1,
        .attack=SMSB_EG_ATTACK[rate(env1&31,env2,pitch)],
        .decay1=SMSB_EG_DSR[rate((env1>>6)&31,env2,pitch)],
        .decay2=SMSB_EG_DSR[rate((env1>>11)&31,env2,pitch)],
        .release=SMSB_EG_DSR[rate(env2&31,env2,pitch)]};
}
unsigned smsb_envelope_tick(SmsbEnvelope *e)
{
    unsigned attenuation=e->value>0?(unsigned)e->value>>18:0;
    if(e->state==0) {
        if(e->attack) {
            e->value-=(int32_t)(((uint64_t)(uint32_t)e->value<<16)/e->attack+1);
            if(e->value<65536){e->value=0;if(!e->link)e->state=1;}
        }
    } else if(e->state==1) {
        e->value+=e->decay1;if((unsigned)(e->value>>16)>=e->threshold)e->state=2;
    } else {
        e->value+=e->state==2?e->decay2:e->release;
        if(e->value>=1023<<16){e->value=1023<<16;e->state=e->state==2?3:4;}
    }
    return attenuation;
}
static int bind_storage(Smsb *s,const void *sequence,unsigned ss,const void *patch,unsigned ps,const SmsbPatchBank *parts)
{
    memset(s,0,sizeof *s);const unsigned char *a=sequence,*b=patch;
    if(!a || !b || ss<20 || ps<48 || memcmp(a,"SMSB",4) || memcmp(b,"SMPB",4) || rd32(a+4)!=2 || rd32(b+4)!=2 ||
       rd32(a+8)>ss || rd32(b+8)>ps || rd32(a+8)<20 || rd32(b+8)<48)return -1;
    ss=rd32(a+8);ps=rd32(b+8);
    if(memcmp(a+ss-4,"ENDB",4) || (!parts && memcmp(b+ps-4,"ENDB",4)))return -1;
    s->patch_parts=parts;
    s->sequence=a;s->sequence_size=ss;s->patch=b;s->patch_size=ps;s->patch_bank=2;
    s->allowed_programs=(1u<<15)|(1u<<16);
    s->global_volume[0]=s->global_volume[1]=127;
    source_audio_ramp_set(s->volume_ramp,127,0);source_audio_ramp_set(s->volume_ramp+1,127,0);return 0;
}
int smsb_bind(Smsb *s,const void *sequence,unsigned ss,const void *patch,unsigned ps)
{return bind_storage(s,sequence,ss,patch,ps,NULL);}
int smsb_bind_parts(Smsb *s,const void *sequence,unsigned ss,const SmsbPatchBank *parts)
{
    if(!parts || !parts->metadata || parts->metadata_size<48 || parts->metadata_size>parts->image_size)return -1;
    return bind_storage(s,sequence,ss,parts->metadata,parts->image_size,parts);
}
int smsb_bind_menu(Smsb *s,const void *sequence,unsigned ss,const void *patch,unsigned ps)
{int error=smsb_bind(s,sequence,ss,patch,ps);if(!error){s->patch_bank=0;s->allowed_programs=65535;}return error;}
int smsb_bind_world(Smsb *s,const void *sequence,unsigned ss,const void *patch,unsigned ps)
{
    int error=smsb_bind(s,sequence,ss,patch,ps);
    /* Audited one-shot stage programs. 3/6/12/13 are separate ambient
       loops/multichannel streams, not interchangeable horn samples. */
    if(!error)s->allowed_programs=0xfb7;
    return error;
}
void smsb_stop(Smsb *s,unsigned slot)
{
    if(slot>=2)return;
    s->streams[slot].active=0;
    for(unsigned i=0;i<SMSB_VOICES;i++)if(s->voices[i].active && s->voices[i].owner==slot)s->voices[i].envelope.state=3;
}
void smsb_release(Smsb *s,unsigned slot)
{
    if(slot>=2)return;
    memset(s->streams+slot,0,sizeof s->streams[slot]);
    for(unsigned i=0;i<SMSB_VOICES;i++)if(s->voices[i].owner==slot)memset(s->voices+i,0,sizeof s->voices[i]);
}
static unsigned spatial_level(const Smsb *s,unsigned owner,unsigned layer,unsigned level)
{
    /* ARM 14f8..1548: patch pan + centred MIDI CC10, then signed
       sequence pan. These audited streams leave CC10 at its default 64. */
    unsigned base=MANATEE_PAN_BASE[rd16(s->patch+layer+18)&31];
    return (level&~31u)|MANATEE_PAN[source_audio_clamp((int)base+s->global_pan[owner],127)];
}
static void apply_pan(Smsb *s,unsigned slot,int pan)
{
    if(slot>=2)return;
    pan=pan<-128?-128:pan>127?127:pan;
    if(s->global_pan[slot]==pan)return;
    s->global_pan[slot]=pan;
    for(unsigned i=0;i<SMSB_VOICES;i++)if(s->voices[i].active && s->voices[i].owner==slot)
        s->voices[i].level=spatial_level(s,slot,s->voices[i].layer,s->voices[i].level);
}
static void apply_volume(Smsb *s,unsigned slot,int volume)
{
    if(slot>=2)return;
    if(volume<-128)volume=-128;
    if(volume>127)volume=127;
    if(s->global_volume[slot]==volume)return;
    s->global_volume[slot]=volume;
    for(unsigned i=0;i<SMSB_VOICES;i++){
        SmsbVoice *v=s->voices+i;if(!v->active || v->owner!=slot)continue;
        SmsbPatch p[4];int n=smsb_patch(s->patch,s->patch_size,v->program,v->key,v->velocity,v->volume,volume,p);
        for(int k=0;k<n;k++)if(p[k].layer==v->layer){v->level=spatial_level(s,slot,v->layer,p[k].level);v->total=p[k].total;}
    }
}
int smsb_fade_volume(Smsb *s,unsigned slot,int volume,int duration)
{
    if(slot>=2 || source_audio_ramp_set(s->volume_ramp+slot,volume,duration))return -1;
    apply_volume(s,slot,source_audio_ramp_value(s->volume_ramp+slot));return 0;
}
int smsb_fade_pan(Smsb *s,unsigned slot,int pan,int duration)
{
    if(slot>=2 || source_audio_ramp_set(s->pan_ramp+slot,pan,duration))return -1;
    apply_pan(s,slot,source_audio_ramp_value(s->pan_ramp+slot));return 0;
}
void smsb_volume(Smsb *s,unsigned slot,int volume){smsb_fade_volume(s,slot,volume,0);}
void smsb_pan(Smsb *s,unsigned slot,int pan){smsb_fade_pan(s,slot,pan,0);}
void smsb_service(Smsb *s)
{
    for(unsigned slot=0;slot<2;slot++){
        if(source_audio_ramp_tick(s->volume_ramp+slot))apply_volume(s,slot,source_audio_ramp_value(s->volume_ramp+slot));
        if(source_audio_ramp_tick(s->pan_ramp+slot))apply_pan(s,slot,source_audio_ramp_value(s->pan_ramp+slot));
    }
}
int smsb_start(Smsb *s,unsigned slot,unsigned program)
{
    if(slot>=2 || !s->sequence)return -1;
    if(program>=32 || !(s->allowed_programs&(1u<<program)))return -1;
    smsb_stop(s,slot);const unsigned char *b=s->sequence;unsigned count=rd32(b+12),size=s->sequence_size;
    if(count>128 || program>=count || !fits(16,count*4,size))return -2;
    unsigned at=rd32(b+16+program*4),end=size-4;
    if(!fits(at,12,size) || memcmp(b+at,"SMSD",4))return -3;
    for(unsigned i=0;i<count;i++){unsigned p=rd32(b+16+i*4);if(p>at && p<end)end=p;}
    if((rd32(b+at+4)!=136 && !(s->patch_bank==0 && rd32(b+at+4)==256)) || (rd32(b+at+8)!=500 && !(s->patch_bank==1 && rd32(b+at+8)==545)))return -4;
    s->streams[slot]=(SmsbStream){.pc=at+12,.limit=end,.base=rd32(b+at+4),.origin=at,
        .quantum=(rd32(b+at+4)*rd32(b+at+8))>>4,.volume=100,.active=1};
    for(unsigned c=0;c<16;c++)s->streams[slot].channels[c].volume=100;
    return 0;
}
static int byte(Smsb *s,SmsbStream *t)
{if(t->pc>=t->limit){t->error=1;return 0;}return s->sequence[t->pc++];}
static void transport(Smsb *s,unsigned owner)
{
    SmsbStream *t=s->streams+owner;t->clock+=16384;
    for(unsigned i=0;i<SMSB_VOICES;i++)if(s->voices[i].active && s->voices[i].owner==owner && t->clock>=s->voices[i].off_clock)
        s->voices[i].envelope.state=3;
    unsigned budget=0;
    while(t->active && !t->error && t->clock>=t->due) {
        if(budget++==128){t->error=5;break;}
        /* ARM 6ae0/6e9c: compact phrases return after N events, rather
           than interpreting their bytes as an independent MIDI track. */
        if(t->return_count){if(t->return_count==1)t->pc=t->return_pc;t->return_count--;}
        unsigned cmd=byte(s,t),delay=0;
        unsigned channel=cmd&15;SmsbChannel *ctl=t->channels+channel;
        if(cmd==0x83){t->active=0;t->ended=1;break;} /* end is not all-note-off */
        if(cmd<0x80) {
            unsigned key=byte(s,t),velocity=byte(s,t),gate=0;
            for(unsigned j=0;j<(cmd>>5)+1;j++)gate=(gate<<8)|byte(s,t);
            delay=byte(s,t);if(cmd&16)delay=(delay<<8)|byte(s,t);
            if(t->error)break;
            SmsbPatch patches[4];int n=ctl->bank!=s->patch_bank ? -1:patch_parts(s->patch,s->patch_size,ctl->program,key,velocity,ctl->volume,s->global_volume[owner],patches,s->patch_parts);
            if(n<0){t->error=2;break;}
            for(int j=0;j<n;j++) {
                unsigned i=0;while(i<SMSB_VOICES && s->voices[i].active)i++;
                if(i==SMSB_VOICES){t->error=3;break;}
                SmsbVoice *v=s->voices+i;*v=(SmsbVoice){.owner=owner,.key=key,.off_clock=t->due+gate*t->quantum+t->gate_extra,
                    .level=spatial_level(s,owner,patches[j].layer,patches[j].level),.total=patches[j].total,.active=1,
                    .program=ctl->program,.velocity=velocity,.volume=ctl->volume,.layer=patches[j].layer,.channel=channel};
                if(ctl->bend){unsigned p=pitch_bend(key,s->patch+v->layer,ctl->bend);if(p==~0u){t->error=2;break;}patches[j].sample.pitch=p;patches[j].sample.step=osb_pitch_step(p);}
                osb_start(&v->sample,&patches[j].sample);smsb_envelope_start(&v->envelope,patches[j].env1,patches[j].env2,patches[j].sample.pitch);s->notes++;
            }
            t->gate_extra=0;
        } else if((cmd&0xf0)==0xb0 || (cmd&0xf0)==0xc0) {
            unsigned first=byte(s,t);
            if((cmd&0xf0)==0xc0)ctl->program=first&127;
            else {
                unsigned value=byte(s,t),control=first&127;
                if(control==121 && value==0){*ctl=(SmsbChannel){.volume=100};}
                else if(control==32)ctl->bank=value;
                else if(control==7) {
                    ctl->volume=value;
                    for(unsigned i=0;i<SMSB_VOICES;i++)if(s->voices[i].active && s->voices[i].owner==owner && s->voices[i].channel==channel)s->voices[i].volume=value;
                    apply_volume(s,owner,s->global_volume[owner]);
                }
                else t->error=4;
            }
            if(!channel){t->program=ctl->program;t->volume=ctl->volume;t->bank=ctl->bank;}
            delay=byte(s,t);if(first&128)delay=(delay<<8)|byte(s,t);
        } else if((cmd&0xf0)==0xe0) {
            /* 6d88 -> 15cc/1c4c: seven-bit bend, default +/-2 semitones.
               Preserve the driver's integer fine-pitch/table calculation. */
            ctl->bend=((int)byte(s,t)-64)*8;delay=byte(s,t);
            for(unsigned i=0;i<SMSB_VOICES;i++) {
                SmsbVoice *v=s->voices+i;if(!v->active || v->owner!=owner || v->channel!=channel)continue;
                unsigned p=pitch_bend(v->key,s->patch+v->layer,ctl->bend);
                if(p==~0u){t->error=2;break;}v->sample.program.pitch=p;v->sample.program.step=osb_pitch_step(p);
            }
        } else if(cmd==0x81) {
            unsigned at=byte(s,t)<<8;at|=byte(s,t);unsigned count=byte(s,t);
            if(t->return_count || !count || at>=t->limit-t->origin){t->error=4;break;}
            t->return_pc=t->pc;t->return_count=count+1;t->pc=t->origin+at;continue;
        } else if(cmd==0x82) {
            if(!t->loop_pc){t->loop_pc=t->pc;t->loop_left=s->sequence[t->pc]&127;if(!t->loop_left)t->loop_left=128;}
            else if(t->loop_left){t->pc=t->loop_pc;if(t->loop_left<128)t->loop_left--;}
            else t->loop_pc=0;
            unsigned first=byte(s,t);delay=byte(s,t);if(first&128)delay=(delay<<8)|byte(s,t);
        } else if(cmd>=0x88 && cmd<=0x8f) {
            /* ARM 702c/7054: extensions apply to the next gate/delay. */
            static const unsigned gate_add[4]={512,2048,4096,8192},delay_add[4]={256,512,2048,4096};
            if(cmd<0x8c)t->gate_extra+=gate_add[cmd&3]*t->quantum;
            else t->delay_extra+=delay_add[cmd&3]*t->quantum;
            /* 702c/7054 resume at 6af8, after the phrase counter. An
               extension byte is part of the following event. */
            if(t->return_count)t->return_count++;
            continue;
        } else if(cmd==0x84) {
            unsigned tempo=byte(s,t)<<8;tempo|=byte(s,t);t->quantum=(t->base*tempo)>>4;delay=byte(s,t);
        } else t->error=4;
        t->due+=delay*t->quantum+t->delay_extra;t->delay_extra=0;
    }
    if(t->error && t->active){s->errors++;smsb_stop(s,owner);}
}
static void smsb_tick(Smsb *s)
{
    /* MANATEE timer A: reload d4 (44 samples), skip every 441st IRQ,
       advance the sequencer by 4000h on every fourth remaining IRQ.
       PSP starts a local timer phase; ARM mailbox service latency differs. */
    if(++s->sample_clock==44) {
        s->sample_clock=0;
        if(++s->skips==441)s->skips=0;
        else if(++s->divider==4){s->divider=0;s->irq++;for(unsigned j=0;j<2;j++)transport(s,j);}
    }
}
void smsb_next(Smsb *s,int *left,int *right)
{
    *left=*right=0;s->dsp_input=0;if(!s->sequence)return;
    smsb_tick(s);
    for(unsigned i=0;i<SMSB_VOICES;i++) {
        SmsbVoice *v=s->voices+i;if(!v->active)continue;
        unsigned attenuation=smsb_envelope_tick(&v->envelope),gains[2],tl=(v->total>>8)+attenuation;
        source_aica_gains(v->level,(tl>255?255:tl)<<8,gains);
        int sample=osb_next(&v->sample);
        if(!(v->sample.program.dsp&15))s->dsp_input+=source_aica_dsp(sample,tl,v->sample.program.dsp>>4);
        *left+=(sample*(int)gains[0])>>15;*right+=(sample*(int)gains[1])>>15;
        if(!v->sample.active || v->envelope.state==4)v->active=0;
    }
}

void smsb_next_buffer(Smsb *s,int (*out)[3],unsigned frames)
{
    if(!frames)return;
    memset(out,0,frames*sizeof *out);s->dsp_input=0;
    if(!s->sequence)return;
    unsigned base=0;
    while(base<frames) {
        /* A timer interrupt acts BEFORE its sample. Stop this span just
           before the next interrupt can start/release/change any voice. */
        smsb_tick(s);
        unsigned count=44-s->sample_clock;
        if(count>frames-base)count=frames-base;
        for(unsigned slot=0;slot<SMSB_VOICES;slot++) {
            SmsbVoice *v=s->voices+slot;
            if(!v->active)continue;
            for(unsigned i=base;i<base+count;i++) {
                unsigned attenuation=smsb_envelope_tick(&v->envelope),gain[2],tl=(v->total>>8)+attenuation;
                source_aica_gains(v->level,(tl>255?255:tl)<<8,gain);
                int sample=osb_next(&v->sample);
                out[i][0]+=(sample*(int)gain[0])>>15;
                out[i][1]+=(sample*(int)gain[1])>>15;
                if(!(v->sample.program.dsp&15))out[i][2]+=source_aica_dsp(sample,tl,v->sample.program.dsp>>4);
                if(!v->sample.active || v->envelope.state==4){v->active=0;break;}
            }
        }
        s->sample_clock+=count-1;base+=count;
    }
    s->dsp_input=out[frames-1][2];
}
