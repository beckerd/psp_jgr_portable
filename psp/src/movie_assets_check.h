#ifndef JGR_MOVIE_ASSETS_CHECK_H
#define JGR_MOVIE_ASSETS_CHECK_H
#include "movie.h"
#include "movie_route.h"
#include "ending_assets_check.h"
/* Preflight the entire authored chain before unloading the live result.
   No progress/save mutation; full bounds validation remains in the loaders. */
static inline int movie_assets_available(int id,const unsigned char *save,char missing[64])
{
    const unsigned char adx[]={0x80,0,0,0x20,3,18,4,1,0,0,0xac,0x44};
    unsigned visits=0;
    for(;id>=0;id=movie_next(id)) {
        if((unsigned)id>=35 || ++visits>35){snprintf(missing,64,"invalid movie chain");return 0;}
        snprintf(missing,64,"movie%02d.bin",id);
        if(!ending_asset_file(missing,0,NULL,0))return 0;
        snprintf(missing,64,"movie%02d.jgc",id);
        if(!ending_asset_file(missing,0,"JGC3",4))return 0;
        for(int seg=0;seg<7;seg++) {
            int ref=movie_voice_reference(id,seg);if(ref<0)continue;
            snprintf(missing,64,"movievoice%03d.adx",ref);
            if(!ending_asset_file(missing,0,adx,sizeof adx))return 0;
        }
        for(int cue=0;;cue++) {
            int ref=movie_script_voice_reference(id,cue);if(ref<0)break;
            snprintf(missing,64,"movievoice%03d.adx",ref);
            if(!ending_asset_file(missing,0,adx,sizeof adx))return 0;
        }
    }
    for(unsigned i=0;i<3;i++) {
        snprintf(missing,64,"grf/%c%02u.bin","sml"[i],save[0x3e5+i]);
        if(save[0x3e5+i]>=34 || !ending_asset_file(missing,(128u<<i)*128*2,NULL,0))return 0;
    }
    missing[0]=0;return 1;
}
#endif
