/* Setup-only pose views. Geometry and texture bytes never enter native
 * simulation; original file offsets remain valid through chars_data(). */
#include "portable_native_world.h"
#include "portable_native_pose.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int range(unsigned start,unsigned count,unsigned stride,unsigned end)
{return start<=end && count<=(end-start)/stride;}
static int keys(unsigned offset,unsigned count,unsigned stride,unsigned bytes)
{return !count || range(offset,count,stride,bytes);}

int jgr_native_bank_load(const JgrNativeConfig *c,Chars *bank,const char *name)
{
    if(!c || !c->dependency || !bank || !name || !*name || strchr(name,'/') ||
       strchr(name,'\\') || strstr(name,"..") || bank->base || bank->motion_data)
        return jgr_native_error(c,"Invalid native replay model request");
    int shared=!strcmp(name,"chars.jgc");
    if(shared && jgr_native_cache_bank_borrow(c->cache,c->disc,c->dependency,c->dependency_user,bank))return 0;
    FILE *f=c->dependency(c->dependency_user,name);
    if(!f)return jgr_native_error(c,"Native replay model bank is unavailable");
    JgcHeader header;unsigned char *metadata=NULL,*motions=NULL;long length;int valid=0;
    if(fseek(f,0,SEEK_END) || (length=ftell(f))<(long)sizeof header || length>32*1024*1024 ||
       fseek(f,0,SEEK_SET) || fread(&header,1,sizeof header,f)!=sizeof header ||
       memcmp(header.magic,"JGC2",4))goto done;
    unsigned bytes=(unsigned)length;
    if(header.off_chars<sizeof header || header.off_bones<header.off_chars ||
       header.off_motions<header.off_bones || header.off_keys<header.off_motions ||
       header.off_pix<header.off_keys || header.off_pix>bytes ||
       !range(header.off_chars,header.n_chars,sizeof(JgcChar),header.off_bones) ||
       !range(header.off_bones,header.n_bones,sizeof(JgcBone),header.off_motions) ||
       !range(header.off_motions,header.n_motions,sizeof(JgcMotion),header.off_keys) ||
       ((header.off_chars|header.off_bones|header.off_motions|header.off_keys)&3u))goto done;
    unsigned meta_bytes=header.off_bones+header.n_bones*sizeof(JgcBone);
    unsigned motion_bytes=header.off_pix-header.off_motions;
    metadata=malloc(meta_bytes);if(!metadata)goto done;
    if(fseek(f,0,SEEK_SET) || fread(metadata,1,meta_bytes,f)!=meta_bytes ||
       memcmp(metadata,&header,sizeof header))goto done;
    if(motion_bytes){
        motions=malloc(motion_bytes);if(!motions)goto done;
        if(fseek(f,header.off_motions,SEEK_SET) || fread(motions,1,motion_bytes,f)!=motion_bytes)goto done;
    }
    JgcHeader *h=(JgcHeader *)metadata;JgcChar *characters=(JgcChar *)(metadata+h->off_chars);
    JgcMotion *clips=(JgcMotion *)motions;
    unsigned channel_start=h->off_motions+h->n_motions*sizeof(JgcMotion);
    unsigned key_bytes=h->off_pix-h->off_keys;
    for(unsigned i=0;i<h->n_chars;i++){
        const JgcChar *ch=characters+i;
        if(ch->bone0>h->n_bones || ch->nbones>h->n_bones-ch->bone0 ||
           ch->motion0>h->n_motions || ch->nmotions>h->n_motions-ch->motion0)goto done;
        for(unsigned j=0;j<ch->nmotions;j++){
            const JgcMotion *m=clips+ch->motion0+j;
            if(m->chan0<channel_start || (m->chan0&3u) ||
               !range(m->chan0,ch->nbones,sizeof(JgcChan),h->off_keys))goto done;
            const JgcChan *channels=(const JgcChan *)(motions+(m->chan0-h->off_motions));
            for(unsigned k=0;k<ch->nbones;k++){
                const JgcChan *v=channels+k;
                if(!keys(v->off_pos,v->npos,12,key_bytes) || !keys(v->off_ang,v->nang,6,key_bytes) ||
                   !keys(v->off_scl,v->nscl,12,key_bytes) ||
                   (v->npos && (v->off_pos&3u)) || (v->nang && (v->off_ang&1u)) ||
                   (v->nscl && (v->off_scl&3u)))goto done;
            }
        }
    }
    valid=1;
done:
    if(fclose(f))valid=0;
    if(!valid){free(motions);free(metadata);return jgr_native_error(c,"Native replay model layout is invalid");}
    memset(bank,0,sizeof *bank);bank->base=metadata;bank->h=(JgcHeader *)metadata;
    bank->chars=(JgcChar *)(metadata+bank->h->off_chars);bank->bones=(JgcBone *)(metadata+bank->h->off_bones);
    bank->motion_data=motions;bank->motions=(JgcMotion *)motions;
    bank->resident_bytes=meta_bytes+motion_bytes;
    if(shared)jgr_native_cache_bank_adopt(c->cache,c->disc,c->dependency,c->dependency_user,bank);
    return 0;
}
void jgr_native_bank_free(const JgrNativeConfig *c,Chars *bank)
{
    if(!bank)return;
    jgr_native_chars_pose_forget();
    if((bank->base || bank->motion_data) && !jgr_native_cache_bank_release(c?c->cache:NULL,bank)){
        free(bank->motion_data);free(bank->base);
    }
    memset(bank,0,sizeof *bank);
}
