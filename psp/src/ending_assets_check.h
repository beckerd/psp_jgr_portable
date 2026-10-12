#ifndef JGR_ENDING_ASSETS_CHECK_H
#define JGR_ENDING_ASSETS_CHECK_H
#include <stdio.h>
#include <string.h>
#include <stdint.h>
/* Transport preflight, not a full mesh validator. Do this BEFORE retiring
   the earned final result: missing/truncated ending assets must not discard
   that live result. The actual movie/voice loaders still validate payloads. */
static inline int ending_asset_file(const char *path,long exact,const void *header,unsigned hn)
{
    FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned char h[16];int ok=hn<=sizeof h && (!hn || fread(h,1,hn,f)==hn);
    if(ok && hn)ok=!memcmp(h,header,hn);
    if(ok)ok=!fseek(f,0,SEEK_END) && (exact ? ftell(f)==exact:ftell(f)>=68);
    fclose(f);return ok;
}
static inline int ending_route_asset(const char *path,unsigned character,int metadata)
{
    FILE *f=fopen(path,"rb");if(!f)return 0;
    uint32_t h[8];int ok=fread(h,1,sizeof h,f)==sizeof h;unsigned expected=0;
    if(ok && metadata) {
        ok=!memcmp(h,"SRT1",4) && h[1]==1 && h[2]==character && h[3] && h[3]<=30000 &&
            h[4]==9 && h[5]>=9 && h[5]<=h[3] && h[5]<=4096 && h[6]<=4096 && !h[7];
        if(ok){expected=32+9*24+(h[5]+h[6])*32;ok=expected<=128u*1024u;}
    } else if(ok) {
        ok=!memcmp(h,"TRT2",4) && h[1] && h[1]<=30000 && h[2]==character && h[3]==20 &&
            h[5]<=120 && h[4]==428+h[5]*4;
        if(ok)expected=512+h[5]*4+h[1]*h[4];
    }
    if(ok)ok=!fseek(f,0,SEEK_END) && ftell(f)==(long)expected;
    fclose(f);return ok;
}
static inline int ending_assets_available(const unsigned char *save,int model,char missing[64])
{
    const char *names[]={"ending25.bin","ending26.bin","ending25.jgc","ending26.jgc","staffroll.bin",
        "endingvoice003.adx","endingvoice144.adx","endingvoice145.adx","endingvoice146.adx","staffsky.jgc"};
    const long sizes[]={161196,1046220,0,0,16336,2683134,105498,141894,95382,0};
    const unsigned char adx[]={0x80,0,0,0x20,3,18,4,1,0,0,0xac,0x44};
    for(unsigned i=0;i<sizeof names/sizeof *names;i++){
        snprintf(missing,64,"%s",names[i]);
        int model=i==2 || i==3 || i==9;
        const void *header=model ? (const void *)(i==9 ? "JGC2":"JGC3"):adx;
        if(!ending_asset_file(missing,sizes[i],header,model ? 4:i>=5 ? sizeof adx:0))return 0;
    }
    for(int i=0;i<20;i++){
        const unsigned char h[]={'T','E','X','1',1,0,0,0,0,2,0,2};
        snprintf(missing,64,"staff%02d.bin",i);
        if(!ending_asset_file(missing,524300,h,sizeof h))return 0;
    }
    /* Staff controller streams can enter the original large-spray task.
       Its actor variant and camera tracks are prerequisites, not optional
       cosmetics. Check before retiring the live earned final result. */
    snprintf(missing,64,"spraymesh%02d.jgc",model);
    if((unsigned)model>=16 || !ending_asset_file(missing,0,"JGC2",4))return 0;
    snprintf(missing,64,"spraycam.bin");
    if(!ending_asset_file(missing,0,"SPC1",4))return 0;
    const unsigned char character_of_model[16]={7,4,3,2,0,13,1,5,9,14,6,11,12,10,15,8};
    unsigned character=character_of_model[model];
    snprintf(missing,64,"staffroute%02u.bin",character);
    if(!ending_route_asset(missing,character,0))return 0;
    snprintf(missing,64,"staffroute%02u_meta.bin",character);
    if(!ending_route_asset(missing,character,1))return 0;
    for(unsigned i=0;i<3;i++) {
        snprintf(missing,64,"grf/%c%02u.bin","sml"[i],save[0x3e5+i]);
        if(save[0x3e5+i]>=34 || !ending_asset_file(missing,(128u<<i)*128*2,NULL,0))return 0;
    }
    snprintf(missing,64,"walltags1.bin");
    if(!ending_asset_file(missing,145664,"CWT1",4))return 0;
    snprintf(missing,64,"finaltagmask.bin");
    if(!ending_asset_file(missing,131072,NULL,0))return 0;
    missing[0]=0;return 1;
}
#endif
