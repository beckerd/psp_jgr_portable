#ifndef JGR_STREAM_DECODE_H
#define JGR_STREAM_DECODE_H
#define STREAM_DECODE_BLOCKS 64
/* Original streaming decoder uses integer histories (movie speech uses the
   separate source ADX float decoder). Retain both original paths exactly. */
typedef struct {
    unsigned serial,blocks;int channels,coefficient1,coefficient2,history[2][2];
    unsigned char input[STREAM_DECODE_BLOCKS*36];
    short pcm[STREAM_DECODE_BLOCKS*32*2];
} StreamDecodeJob;
static inline void stream_decode_execute(StreamDecodeJob *j)
{
    if(j->blocks>STREAM_DECODE_BLOCKS || (j->channels!=1 && j->channels!=2))return;
    for(unsigned block=0;block<j->blocks;block++)for(int ch=0;ch<j->channels;ch++) {
        const unsigned char *p=j->input+(block*j->channels+ch)*18;
        int scale=p[0]*256+p[1],h0=j->history[ch][0],h1=j->history[ch][1];
        for(int i=0;i<32;i++) {
            int nib=i&1 ? p[2+i/2]&15:p[2+i/2]>>4;if(nib>=8)nib-=16;
            int v=nib*scale+((j->coefficient1*h0+j->coefficient2*h1)>>12);
            if(v>32767)v=32767;
            if(v< -32768)v=-32768;
            h1=h0;h0=v;j->pcm[(block*32+i)*2+ch]=(short)v;
            if(j->channels==1)j->pcm[(block*32+i)*2+1]=(short)v;
        }
        j->history[ch][0]=h0;j->history[ch][1]=h1;
    }
}
#endif
