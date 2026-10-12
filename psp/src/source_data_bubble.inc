/* External mutable storage prevents mesh constant folding. */
#include "source_data_bubble.h"
extern BubbleVertex jgr_bubble_v0[28];
extern unsigned short jgr_bubble_i0[72];
extern BubbleBatch jgr_bubble_b0[2];
extern BubbleVertex jgr_bubble_v1[28];
extern unsigned short jgr_bubble_i1[72];
extern BubbleBatch jgr_bubble_b1[2];
extern BubbleVertex jgr_bubble_v2[28];
extern unsigned short jgr_bubble_i2[72];
extern BubbleBatch jgr_bubble_b2[2];
static const BubbleVertex *const bubble_vertices[]={jgr_bubble_v0,jgr_bubble_v1,jgr_bubble_v2};
static const unsigned short *const bubble_indices[]={jgr_bubble_i0,jgr_bubble_i1,jgr_bubble_i2};
static const BubbleBatch *const bubble_batches[]={jgr_bubble_b0,jgr_bubble_b1,jgr_bubble_b2};
