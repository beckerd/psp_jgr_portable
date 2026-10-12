#ifndef JGR_ADX_H
#define JGR_ADX_H
#include <stdint.h>
/* Mono CRI 297c04. History is s16 only at CALL boundaries; internally the
   original retains float samples scaled by 65536. Output has blocks*32
   entries. Returns blocks decoded, stopping at the original 0x8000 marker. */
int adx_decode_mono(const unsigned char *input,unsigned bytes,int blocks,
                    int16_t *output,unsigned samples,int16_t history[2],
                    int16_t coefficient1,int16_t coefficient2);
/* Same kernel, retaining float history across bounded PSP buffers. CRI's
   streaming-job requantization boundaries are not reproduced. */
int adx_decode_continuous(const unsigned char *,unsigned,int,int16_t *,unsigned,
                          float history[2],int16_t,int16_t);
#endif
