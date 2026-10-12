#ifndef JGR_GE_MATRIX_H
#define JGR_GE_MATRIX_H
#include <string.h>
/* Emit the SDK's column-major 3x4 matrix words directly from row-major
   CPU/VFPU storage. Drop the same low eight float bits; never round them.
   No temporary transpose and no extra list publication/allocation. */
static inline void ge_matrix_words(unsigned *out,unsigned command,unsigned index,const float *in)
{
    out[0]=(command<<24)|index;
    for(unsigned col=0;col<4;col++)for(unsigned row=0;row<3;row++) {
        unsigned bits;memcpy(&bits,in+row*4+col,4);
        out[1+col*3+row]=((command+1)<<24)|(bits>>8);
    }
}
#endif
