#include "osb.h"
#include "audio_spatial.h"
#include <string.h>
static uint16_t u16(const unsigned char *p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t u32(const unsigned char *p){return u16(p)|((uint32_t)u16(p+2)<<16);}
uint32_t osb_pitch_step(unsigned pitch)
{
    /* AICA OCT/FNS, same 22.10 increment as Flycast ChannelEx::UpdatePitch. */
    unsigned octave=(pitch>>11)&15,step=1024|(pitch&1023);
    return octave&8 ? step>>(16-octave):step<<octave;
}
int osb_program_parts(const void *header,size_t header_size,const void *samples,size_t size,OsbProgram *out)
{
    memset(out,0,sizeof *out);const unsigned char *p=header;
    if(!p || header_size<64 || memcmp(p,"SOSP",4) || memcmp(p+60,"ENDP",4))return -3;
    unsigned control=u16(p+4),format=(control>>7)&3;
    unsigned start=((control&127)<<16)|u16(p+6),length=u16(p+10);
    if(format!=0 && format!=2)return -4;
    unsigned bytes=format==2 ? (length+1)/2:length*2;
    if(!samples || !length || bytes>size || u16(p+8)>=length)return -5;
    out->samples=samples;out->offset=start;out->length=length;out->loop_start=u16(p+8);
    out->control=control;out->pitch=u16(p+16);out->step=osb_pitch_step(out->pitch);
    out->dsp=(p[20]&15)|(MANATEE_SEND[(p[20]>>4)*4]<<4); /* ARM 538c..53e8 */
    return out->step ? 0:-6;
}
int osb_program(const void *image,size_t size,unsigned index,OsbProgram *out)
{
    memset(out,0,sizeof *out);const unsigned char *d=image;
    if(!d || size<16 || memcmp(d,"SOSB",4))return -1;
    unsigned count=u32(d+12);
    if(count>(size-16)/4 || index>=count)return -2;
    unsigned off=u32(d+16+index*4);
    if(off>size || size-off<64 || memcmp(d+off,"SOSP",4) || memcmp(d+off+60,"ENDP",4))return -3;
    unsigned control=u16(d+off+4),format=(control>>7)&3;
    if(format!=0 && format!=2)return -4;
    unsigned start=((control&127)<<16)|u16(d+off+6);
    if(start>size)return -5;
    return osb_program_parts(d+off,64,d+start,size-start,out);
}
int osb_adpcm(unsigned nibble,int previous,int *quant)
{
    /* Hardware's x.8 multipliers are 230/307/409/... . The old exporter
       rounded them to x.6 (228/308/408), changing every subsequent sample.
       Also clamp the DELTA before addition, not just the resulting sample. */
    static const int factor[8]={230,230,230,230,307,409,512,614};
    unsigned mag=nibble&7;int delta=(*quant*(2*mag+1))>>3;
    if(delta>32767)delta=32767;
    int value=previous+((nibble&8) ? -delta:delta);
    *quant=(*quant*factor[mag])>>8;
    if(*quant<127)*quant=127;if(*quant>24576)*quant=24576;
    return value>32767 ? 32767:value< -32768 ? -32768:value;
}
static int decode(const OsbProgram *p,unsigned at,int previous,int *quant)
{
    int pcm=((p->control>>7)&3)==0;
    unsigned offset=pcm ? at*2:at/2;
    const unsigned char *sample=p->pages ?
        p->pages[offset>>OSB_SAMPLE_PAGE_SHIFT]+(offset&(OSB_SAMPLE_PAGE_SIZE-1)):
        p->samples+offset;
    /* PCM words and page boundaries are both even, so a word never spans pages. */
    if(pcm)return (int16_t)u16(sample);
    return osb_adpcm(*sample>>((at&1)*4),previous,quant);
}
static void sample(OsbVoice *v)
{
    const OsbProgram *p=&v->program;
    if(v->position==p->loop_start) {
        if(!v->loop_saved){v->loop_quant=v->quant;v->loop_sample=v->sample;v->loop_saved=1;}
        else {v->quant=v->loop_quant;v->sample=v->loop_sample;}
    }
    v->sample=decode(p,v->position,v->sample,&v->quant);
    unsigned next=v->position+1;
    if(next>=p->length)next=p->loop_start;
    int quant=v->quant,previous=v->sample;
    if(next==p->loop_start && v->loop_saved){quant=v->loop_quant;previous=v->loop_sample;}
    v->next=decode(p,next,previous,&quant);
}
void osb_start(OsbVoice *v,const OsbProgram *p)
{ memset(v,0,sizeof *v);v->program=*p;v->quant=127;v->active=p->samples && p->length && p->step;if(v->active)sample(v); }
int osb_next(OsbVoice *v)
{
    if(!v->active)return 0;
    int out=(v->sample*(int)(1024-v->fraction)>>10)+(v->next*(int)v->fraction>>10);
    unsigned advance=v->fraction+v->program.step;v->fraction=advance&1023;advance>>=10;
    while(advance--) {
        if(++v->position>=v->program.length) {
            if(!(v->program.control&512)){v->active=0;break;}
            v->position=v->program.loop_start;
        }
        sample(v);
    }
    return out;
}
