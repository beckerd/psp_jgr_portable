/* The setup process uses scene parsers and the CPU pose sampler, without a
 * renderer or a background animation worker. These services are not linked
 * into the gameplay executable. */
#include "stage.h"
#include "hang.h"
#include "me.h"
#if defined(__PSP__) || defined(JGR_HANG_HOST_TEST)
volatile HangProgress hang_progress;
#endif
int stage_load(Stage *stage,const char *path)
{(void)stage;(void)path;return -1;}
void stage_free(Stage *stage){(void)stage;}
void jgr_loading_poll(void){}
int me_background_begin(int (*chunk)(int),int argument)
{(void)chunk;(void)argument;return 0;}
int me_background_wait(unsigned *chunks,unsigned *ticks,unsigned *maximum)
{if(chunks)*chunks=0;if(ticks)*ticks=0;if(maximum)*maximum=0;return 0;}
