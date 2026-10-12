#ifndef JGR_OSB_H
#define JGR_OSB_H
#include <stdint.h>
#include <stddef.h>
/* Read-only original SOSB/SOSP banks. Caller retains the image during play.
   This is the sample/pitch/loop layer, NOT an AICA envelope/DSP emulator. */
#define OSB_SAMPLE_PAGE_SHIFT 12
#define OSB_SAMPLE_PAGE_SIZE (1u<<OSB_SAMPLE_PAGE_SHIFT)
typedef struct {
    const unsigned char *samples;
    const unsigned char *const *pages; /* optional immutable 4 KiB sample pages */
    uint32_t offset,length,loop_start,step;
    uint16_t control,pitch,dsp; /* original ISEL/IMXL register */
} OsbProgram;
typedef struct {
    OsbProgram program;
    uint32_t position,fraction;
    int active,sample,next,quant,loop_quant,loop_sample,loop_saved;
} OsbVoice;
int osb_program(const void *image,size_t size,unsigned index,OsbProgram *out);
int osb_program_parts(const void *header,size_t header_size,const void *samples,size_t bytes,OsbProgram *out);
uint32_t osb_pitch_step(unsigned pitch);
int osb_adpcm(unsigned nibble,int previous,int *quant);
void osb_start(OsbVoice *,const OsbProgram *);
int osb_next(OsbVoice *); /* one 44100-Hz mono output sample */
#endif
