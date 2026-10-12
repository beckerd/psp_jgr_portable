/* Setup-only source-rounded interpolation over the normal production key
 * decoder. No original keys, poses or coefficients are compiled here. */
#include "portable_native_pose.h"
#include "source_fmac.h"
#include <string.h>
/* The same clip/frame is consumed by every bone, often several times in the
 * skating blend tree. Cache only preparation: all keyed values and blends
 * are still evaluated in their original order for each call. Derive pointer
 * identities from the current bank, including borrowed motion/key storage.
 * Sixteen cursors cover concurrent skating/overlay clips without allocating
 * memory; overflow only repeats the existing preparation on the next miss. */
#define NATIVE_CURSORS 16u
typedef struct {
    CharSampleCursor cursor;
    const JgcBone *bones;const JgcChan *channels;const unsigned char *keys;
    uint32_t frame;unsigned frames;
} NativeCursor;
static NativeCursor native_cursors[NATIVE_CURSORS];
static unsigned native_cursor_count,native_cursor_next,native_cursor_recent;
void jgr_native_chars_pose_forget(void)
{native_cursor_count=native_cursor_next=native_cursor_recent=0;memset(native_cursors,0,sizeof native_cursors);}
static const CharSampleCursor *native_cursor(const Chars *c,const JgcChar *ch,int motion,float frame)
{
    const JgcBone *bones=c->bones+ch->bone0;const JgcChan *channels=NULL;const unsigned char *keys=NULL;
    unsigned frames=0;uint32_t bits;memcpy(&bits,&frame,sizeof bits);
    if(motion>=0) {
        const Chars *owner=chars_motion_owner(c);const JgcMotion *clip=c->motions+motion;
        channels=(const JgcChan *)chars_data(owner,clip->chan0);keys=chars_data(owner,owner->h->off_keys);
        frames=clip->nframes;
    }
    unsigned slot=native_cursor_recent;
    for(unsigned i=0;i<native_cursor_count;i++) {
        NativeCursor *entry=native_cursors+slot;
        if(entry->bones==bones && entry->channels==channels && entry->keys==keys && entry->frame==bits && entry->frames==frames) {
            native_cursor_recent=slot;return &entry->cursor;
        }
        if(++slot==native_cursor_count)slot=0;
    }
    slot=native_cursor_count<NATIVE_CURSORS ? native_cursor_count++:native_cursor_next++%NATIVE_CURSORS;
    NativeCursor *entry=native_cursors+slot;entry->bones=bones;entry->channels=channels;entry->keys=keys;entry->frame=bits;entry->frames=frames;
    chars_sample_prepare(c,ch,motion,frame,&entry->cursor);native_cursor_recent=slot;return &entry->cursor;
}
void jgr_native_chars_sample_pose(const Chars*c,const JgcChar*ch,int motion,float frame,unsigned bone,CharPose*pose)
{
    const CharSampleCursor *cursor=native_cursor(c,ch,motion,frame);
    const JgcBone *bind=cursor->bones+bone;
    memcpy(pose->pos,bind->pos,sizeof pose->pos);memcpy(pose->ang,bind->ang,sizeof pose->ang);
    memcpy(pose->scl,bind->scl,sizeof pose->scl);pose->ev=bind->ev;pose->keyed=0;
    if(!cursor->channels)return;
    const JgcChan*cn=cursor->channels+bone;
    if(cn->nang) {
        const short *base=(const short *)(cursor->keys+cn->off_ang);
        const short *a=base+(cursor->k<(int)cn->nang?(unsigned)cursor->k:cn->nang-1)*3;
        const short *b=base+(cursor->k1<(int)cn->nang?(unsigned)cursor->k1:cn->nang-1)*3;
        for(unsigned k=0;k<3;k++)pose->ang[k]=a[k]+(int)((short)(b[k]-a[k])*cursor->t);
        pose->keyed|=2;pose->ev&=~2u;
    }
    unsigned counts[2]={cn->npos,cn->nscl},offsets[2]={cn->off_pos,cn->off_scl};
    float*outputs[2]={pose->pos,pose->scl};
    for(unsigned kind=0;kind<2;kind++)if(counts[kind]){
        const float*base=(const float*)(cursor->keys+offsets[kind]);
        const float*a=base+(cursor->k<(int)counts[kind]?(unsigned)cursor->k:counts[kind]-1)*3;
        const float*b=base+(cursor->k1<(int)counts[kind]?(unsigned)cursor->k1:counts[kind]-1)*3;
        for(unsigned k=0;k<3;k++)outputs[kind][k]=source_fmac(b[k]-a[k],cursor->t,a[k]);
        unsigned mask=kind?4u:1u;pose->keyed|=mask;pose->ev&=~mask;
    }
}
void jgr_native_chars_blend_pose(CharPose*a,const CharPose*b,float weight)
{
    if(weight==0.f)return;
    if(weight==1.f){*a=*b;return;}
    for(unsigned k=0;k<3;k++){
        a->pos[k]=source_fmac(b->pos[k]-a->pos[k],weight,a->pos[k]);
        a->scl[k]=source_fmac(b->scl[k]-a->scl[k],weight,a->scl[k]);
        a->ang[k]+=(int)((short)(b->ang[k]-a->ang[k])*weight);
    }
    a->ev&=b->ev|~7u;a->keyed|=b->keyed;
}
void jgr_native_chars_overlay_pose(const Chars*c,const JgcChar*ch,int motion,float frame,unsigned bone,float weight,CharPose*pose)
{
    CharPose other;jgr_native_chars_sample_pose(c,ch,motion,frame,bone,&other);
    const JgcChan*cn=(const JgcChan*)chars_data(chars_motion_owner(c),c->motions[motion].chan0)+bone;
    if(!cn->npos){memcpy(other.pos,pose->pos,12);other.ev=(other.ev&~1u)|(pose->ev&1u);}
    if(!cn->nang){memcpy(other.ang,pose->ang,12);other.ev=(other.ev&~2u)|(pose->ev&2u);}
    if(!cn->nscl){memcpy(other.scl,pose->scl,12);other.ev=(other.ev&~4u)|(pose->ev&4u);}
    other.keyed|=pose->keyed;jgr_native_chars_blend_pose(pose,&other,weight);
}
