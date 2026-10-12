#ifndef JGR_COLLISION_SOURCE_H
#define JGR_COLLISION_SOURCE_H
#include <stddef.h>
/* World broadphase transcribed from 0b409c / 0b52bc / 0c80e0 /
   0c814c / 0c831c. No new spatial partition or contact deduplication. */
typedef struct {float low[3];unsigned count;float high[3];unsigned first;} ColPatch;
#include "collision_items.h"
typedef struct {unsigned short first,patch;short ymin,ymax;unsigned short mask,key;} ColEntry;
#define COL_CANDIDATE_CAPACITY 128
#define COL_CANDIDATE_SLOTS 16
typedef struct {
    int xmin,xmax,zmin,zmax;float ymin,ymax;unsigned mask,count,valid;
    unsigned short ids[COL_CANDIDATE_CAPACITY];
} ColCandidates;
#ifndef JGR_NO_CANDIDATE_REUSE
/* Overflow keys must not evict successful candidate lists. Eight keys use
   256 fixed bytes on PSP, with no ID arrays or source heap growth. Source
   ownership and retirement prevent an allocator-reused bank from matching. */
#define COL_OVERFLOW_SLOTS 8u
typedef struct {
    const ColSource *source;
    int xmin,xmax,zmin,zmax;float ymin,ymax;unsigned mask;
} ColCandidateOverflow;
static ColCandidateOverflow source_overflows[COL_OVERFLOW_SLOTS];
static unsigned source_overflow_next;
static void source_overflow_forget(const ColSource *s)
{for(unsigned i=0;i<COL_OVERFLOW_SLOTS;i++)if(source_overflows[i].source==s)source_overflows[i].source=NULL;}
#endif
struct ColSource {
    unsigned nworld,npatch,nentry,bytes;int xmin,xmax,zmin,zmax;
    void *data;unsigned *starts;ColPatch *patches;ColEntry *entries;
#ifndef JGR_NO_CANDIDATE_REUSE
    /* Immutable source entries only. Owned by this source bank, so a free
       or replacement also retires the cached lists. No moving providers. */
    ColCandidates candidates[COL_CANDIDATE_SLOTS];unsigned candidate_next;
#endif
};
static void source_free(ColSource *s)
{
    if(!s)return;
#ifndef JGR_NO_CANDIDATE_REUSE
    source_overflow_forget(s);
#endif
    free(s->data);free(s);
}
static int source_load(Collision *c,SceUID fd,unsigned offset,unsigned magic,SceOff length)
{
    if(!offset && !magic)return 0;
    if(magic!=0x31535043u || offset!=16+(unsigned)c->nbase*44 || length-offset<4128)return -8;
    unsigned h[4];if(read_exact(fd,h,16))return -9;
    if(h[0]!=1 || !h[1] || h[1]>65536 || h[1]>(unsigned)c->nbase || !h[2] || h[2]>65535 || h[3]>h[1])return -8;
    unsigned bytes=4112+h[2]*32+h[3]*12;
    if((SceOff)offset+16+bytes>length)return -8;
    ColSource *s=calloc(1,sizeof *s);if(!s)return -3;
    s->data=memalign(16,bytes);if(!s->data){source_free(s);return -3;}
    int err=read_exact(fd,s->data,bytes);if(err){source_free(s);return err;}
    s->nworld=h[1];s->npatch=h[2];s->nentry=h[3];s->bytes=bytes+sizeof *s;
    s->starts=s->data;s->patches=(ColPatch *)(s->starts+1028);s->entries=(ColEntry *)(s->patches+s->npatch);
    if(s->starts[0] || s->starts[1024]!=s->nentry)goto invalid;
    for(unsigned i=0;i<1024;i++)if(s->starts[i]>s->starts[i+1])goto invalid;
    for(unsigned i=0;i<s->npatch;i++) {
        ColPatch *p=s->patches+i;
        if(!p->count || p->count>s->nworld || p->first>s->nworld-p->count)goto invalid;
        for(int k=0;k<3;k++)if(!isfinite(p->low[k]) || !isfinite(p->high[k]) || p->low[k]>p->high[k])goto invalid;
    }
    s->xmin=s->zmin=INT_MAX;s->xmax=s->zmax=INT_MIN;
    unsigned next=0;
    for(unsigned cell=0;cell<1024;cell++)for(unsigned i=s->starts[cell];i<s->starts[cell+1];i++) {
        ColEntry *e=s->entries+i;
        if(e->patch>=s->npatch || e->first!=next || e->ymin>e->ymax)goto invalid;
        ColPatch *p=s->patches+e->patch;if(p->count>s->nworld-next)goto invalid;
        /* The canonical first occurrence must describe identical geometry.
           Validate once at load, never in the per-frame iterator. */
        for(unsigned j=0;j<p->count;j++)if(memcmp(static_triangle(c,next+j),static_triangle(c,p->first+j),44))goto invalid;
        next+=p->count;
        int x=((e->key&255)<<5)|(cell&31),z=((e->key>>8)<<5)|(cell>>5);
        if(x&4096)x-=8192;if(z&4096)z-=8192;
        if(x<s->xmin)s->xmin=x;if(x>s->xmax)s->xmax=x;
        if(z<s->zmin)s->zmin=z;if(z>s->zmax)s->zmax=z;
    }
    if(next!=s->nworld)goto invalid;
    c->source=s;return 0;
invalid:source_free(s);return -8;
}
/* All pointers are 16-byte aligned. Four boxes at once, with the SAME
   separate float subtraction/addition and inclusive faces as 0c80e0.
   Uses only M0/M1, owned by collision_math_begin/end for the whole query. */
static unsigned source_bounds4(const ColPatch *const p[4],const float sphere[4])
{
#if JGR_COLLISION_VFPU && !defined(JGR_SCALAR_PATCH_BOUNDS)
    unsigned bits,t;
#define PB_AXIS(S,ROW,OP,CMP) \
    "vpfxs " S "," S "," S "," S "\nvmov.q C120, C100\n" \
    OP ".q C130, " ROW ", C110\nvcmp.q " CMP ", C120, C130\nvnop\nmfvc %1, $131\nor %0, %0, %1\n"
    __asm__ volatile(
        "lv.q C000, 0(%2)\nlv.q C010, 0(%3)\nlv.q C020, 0(%4)\nlv.q C030, 0(%5)\n"
        "lv.s S100, 0(%6)\nlv.s S101, 4(%6)\nlv.s S102, 8(%6)\nlv.s S103, 12(%6)\n"
        "vpfxs w,w,w,w\nvmov.q C110, C100\nli %0, 0\n"
        PB_AXIS("x","R000","vsub","LT") PB_AXIS("y","R001","vsub","LT") PB_AXIS("z","R002","vsub","LT")
        "lv.q C000, 16(%2)\nlv.q C010, 16(%3)\nlv.q C020, 16(%4)\nlv.q C030, 16(%5)\n"
        PB_AXIS("x","R000","vadd","GT") PB_AXIS("y","R001","vadd","GT") PB_AXIS("z","R002","vadd","GT")
        : "=&r"(bits),"=&r"(t) : "r"(p[0]),"r"(p[1]),"r"(p[2]),"r"(p[3]),"r"(sphere) : "memory");
#undef PB_AXIS
    return (~bits)&15;
#else
    unsigned bits=0;for(int i=0;i<4;i++) {
        int inside=1;for(int k=0;k<3;k++)if(sphere[k]<p[i]->low[k]-sphere[3] || sphere[k]>p[i]->high[k]+sphere[3])inside=0;
        bits|=(unsigned)inside<<i;
    }return bits;
#endif
}
static int source_segment_box(const ColPatch *p,const float *a,const float *b)
{
#ifndef JGR_SEGMENT_BOX_REFERENCE
    /* The original three 2-D region-table lookups reject exactly when both
       endpoints lie beyond the same box face. Inputs and bounds are finite
       (validated by probe setup/load); equality remains inside. */
    for(int k=0;k<3;k++)
        if((a[k]<p->low[k] && b[k]<p->low[k]) ||
           (a[k]>p->high[k] && b[k]>p->high[k]))return 0;
    return 1;
#else
    /* 9 words at 1cddd8 in the original executable, checked by asset tests. */
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_collision_source_h__table_0.inc"
#else

#include "generated/source_data/collision_source_h__table_0.inc"

#endif

    int ac[3],bc[3];for(int k=0;k<3;k++) {
        ac[k]=a[k]<p->low[k]?0:a[k]<=p->high[k]?1:2;
        bc[k]=b[k]<p->low[k]?0:b[k]<=p->high[k]?1:2;
    }
    const int u[3]={0,1,2},v[3]={2,0,1};
    for(int k=0;k<3;k++)if(table[bc[u[k]]*3+bc[v[k]]]&(1u<<(ac[u[k]]*3+ac[v[k]])))return 0;
    return 1;
#endif
}
typedef struct {
    const Collision *c;const float *a,*b;float sphere[4],ymin,ymax;
    unsigned mask,entry,end,first,last,lanes,lane;const ColEntry *batch[4];
    int x,z,xmin,xmax,zmax,source_done;
    int px,pz,pxmin,pxmax,pzmax,at,pend;
    unsigned grid_word,grid_pending;int grid_cell;
    int original; /* last returned ID came from an authored world patch */
    const ColPatch *item_patch,*item_owner;ColItemProfile *item_profile;
    float item_low[3],item_high[3];int item_rejected;
    /* Snapshot protects an outer walk from callback-issued nested queries. */
    unsigned candidate_at,candidate_count;int candidate_active;
    unsigned short candidates[COL_CANDIDATE_CAPACITY];
} ColWalk;
static void source_candidates(ColWalk *w);
static void source_walk_begin(ColWalk *w,const Collision *c,const float *lo,const float *hi,
    const float *sphere,const float *a,const float *b,unsigned mask,int x0,int x1,int z0,int z1)
{
    /* Candidate IDs are initialized only through candidate_count. Clearing
       all 128 unused IDs on every ray/body costs more than small searches. */
    memset(w,0,offsetof(ColWalk,candidates));w->c=c;w->a=a;w->b=b;w->mask=mask;
    memcpy(w->sphere,sphere,16);w->ymin=lo[1];w->ymax=hi[1];
    memcpy(w->item_low,lo,12);memcpy(w->item_high,hi,12);
    w->px=w->pxmin=x0;w->pxmax=x1;w->pz=z0;w->pzmax=z1;
    w->grid_cell=-1;
    const ColSource *s=c->source;
    if(!s){w->source_done=1;return;}
    /* Clamp BEFORE integer conversion; no wrapping of distant queries. */
    float xa=floorf(lo[0]/128.f),xb=floorf(hi[0]/128.f),za=floorf(lo[2]/128.f),zb=floorf(hi[2]/128.f);
    if(xb<s->xmin || xa>s->xmax || zb<s->zmin || za>s->zmax){w->source_done=1;return;}
    w->x=w->xmin=xa<s->xmin?s->xmin:(int)xa;w->xmax=xb>s->xmax?s->xmax:(int)xb;
    w->z=za<s->zmin?s->zmin:(int)za;w->zmax=zb>s->zmax?s->zmax:(int)zb;
    source_candidates(w);
}
static const ColEntry *source_walk_entry_uncached(ColWalk *w)
{
    const ColSource *s=w->c->source;
    for(;;) {
        while(w->entry<w->end) {
            const ColEntry *e=s->entries+w->entry++;collision_profile.source_entries++;
            unsigned key=(((w->x&0x1fe0)>>5)|((w->z&0x1fe0)<<3))&65535;
            if(e->key==key && (w->mask&e->mask) && e->ymin<=w->ymax && e->ymax>=w->ymin)return e;
        }
        if(w->end) {if(++w->x>w->xmax){w->x=w->xmin;w->z++;}}
        if(w->z>w->zmax)return NULL;
        unsigned cell=(w->z&31)*32+(w->x&31);
        w->entry=s->starts[cell];w->end=s->starts[cell+1];
        /* Empty buckets also have to advance (including bucket zero). */
        if(w->entry==w->end){if(++w->x>w->xmax){w->x=w->xmin;w->z++;}w->end=0;}
    }
}
static void source_candidates(ColWalk *w)
{
#ifndef JGR_NO_CANDIDATE_REUSE
    /* Body probes and camera/ground rays share the immutable source grid.
       Cache only candidate IDs; every query still tests its own Y bounds,
       sphere, segment (when present), mask and ordered contact math. */
    if(w->xmax-w->xmin>1 || w->zmax-w->z>1)return;
    ColSource *s=w->c->source;
    unsigned entries=0;
    for(int z=w->z;z<=w->zmax;z++)for(int x=w->xmin;x<=w->xmax;x++) {
        unsigned cell=(z&31)*32+(x&31);entries+=s->starts[cell+1]-s->starts[cell];
    }
    if(entries<16)return;
    float ymin=floorf(w->ymin/64.f)*64.f,ymax=ceilf(w->ymax/64.f)*64.f;
    for(unsigned i=0;i<COL_CANDIDATE_SLOTS;i++) {
        const ColCandidates *c=s->candidates+i;
        if(c->valid && c->xmin==w->xmin && c->xmax==w->xmax && c->zmin==w->z && c->zmax==w->zmax &&
           c->mask==w->mask && c->ymin==ymin && c->ymax==ymax) {
            memcpy(w->candidates,c->ids,c->count*sizeof *c->ids);
            w->candidate_count=c->count;w->candidate_active=1;collision_profile.candidate_hits++;return;
        }
    }
    /* Successful-list hits above incur no overflow lookup. A remembered
       failure leaves the fresh walker on the exact original streaming path,
       with its live Y limits and no partial candidate prefix activated. */
    if(candidate_overflow_enabled)for(unsigned i=0;i<COL_OVERFLOW_SLOTS;i++) {
        const ColCandidateOverflow *c=source_overflows+i;
        if(c->source==s && c->xmin==w->xmin && c->xmax==w->xmax && c->zmin==w->z && c->zmax==w->zmax &&
           c->mask==w->mask && c->ymin==ymin && c->ymax==ymax) {
            collision_profile.candidate_overflow_hits++;return;
        }
    }
    /* Widen only the integer-Y rejection, not the live sphere/segment test.
       Cached entries stay in exact cell/bucket/source order, including all
       aliases. Overflow retries the original streaming walk. Remembering
       that immutable full key avoids building its failed prefix again. */
    float old_min=w->ymin,old_max=w->ymax;int z=w->z;
    w->ymin=ymin;w->ymax=ymax;
    const ColEntry *e;unsigned count=0;
    while((e=source_walk_entry_uncached(w))) {
        if(count==COL_CANDIDATE_CAPACITY)break;
        w->candidates[count++]=(unsigned short)(e-s->entries);
    }
    w->ymin=old_min;w->ymax=old_max;w->x=w->xmin;w->z=z;w->entry=w->end=0;
    if(e) {
        collision_profile.candidate_overflows++;
        if(candidate_overflow_enabled) {
            ColCandidateOverflow *c=source_overflows+(source_overflow_next++&(COL_OVERFLOW_SLOTS-1));
            c->source=s;c->xmin=w->xmin;c->xmax=w->xmax;c->zmin=z;c->zmax=w->zmax;
            c->mask=w->mask;c->ymin=ymin;c->ymax=ymax;
        }
        return;
    }
    ColCandidates *c=s->candidates+(s->candidate_next++&(COL_CANDIDATE_SLOTS-1));
    c->xmin=w->xmin;c->xmax=w->xmax;c->zmin=z;c->zmax=w->zmax;
    c->mask=w->mask;c->ymin=ymin;c->ymax=ymax;c->count=count;c->valid=1;
    memcpy(c->ids,w->candidates,count*sizeof *c->ids);
    w->candidate_count=count;w->candidate_active=1;collision_profile.candidate_misses++;
#else
    (void)w;
#endif
}
static const ColEntry *source_walk_entry(ColWalk *w)
{
    if(!w->candidate_active)return source_walk_entry_uncached(w);
    while(w->candidate_at<w->candidate_count) {
        const ColEntry *e=w->c->source->entries+w->candidates[w->candidate_at++];
        if(e->ymin<=w->ymax && e->ymax>=w->ymin)return e;
    }
    return NULL;
}
static int source_walk_next(ColWalk *w)
{
    while(!w->source_done) {
        if(w->first<w->last){w->original=1;w->item_profile=NULL;collision_profile.source_triangles++;return w->first++;}
        while(w->lane<4) {
            unsigned i=w->lane++;if(!(w->lanes&(1u<<i)))continue;
            const ColEntry *e=w->batch[i];const ColPatch *p=w->c->source->patches+e->patch;
            if(w->a && !source_segment_box(p,w->a,w->b))continue;
            w->first=e->first;w->last=e->first+p->count;break;
        }
        if(w->first<w->last)continue;
        const ColPatch *p[4];unsigned n=0;
        for(;n<4;n++){w->batch[n]=source_walk_entry(w);if(!w->batch[n])break;p[n]=w->c->source->patches+w->batch[n]->patch;}
        if(!n){w->source_done=1;break;}
        for(unsigned i=n;i<4;i++)p[i]=p[0];
        collision_profile.source_patches+=n;collision_profile.patch_batches++;
        w->lanes=source_bounds4(p,w->sphere)&((1u<<n)-1);w->lane=0;
    }
    w->original=0;
    if(world_grid_active(w->c))for(;;) {
        if(w->grid_cell>=0) {
            int id=world_grid_next(w->c,w->grid_cell,&w->grid_word,&w->grid_pending);
            if(id>=0)return id;
        }
        if(w->px>w->pxmax){w->px=w->pxmin;w->pz++;}
        if(w->pz>w->pzmax || w->pxmax<w->pxmin)return -1;
        w->grid_cell=w->pz*w->c->gw+w->px++;w->grid_word=w->grid_pending=0;
    }
    for(;;) {
        if(w->at<w->pend) {
            if(!w->c->items)return col_grid_index(w->c,w->at++);
            int id=col_grid_index(w->c,w->at);
            if(!w->item_patch || (unsigned)id<w->item_patch->first ||
               (unsigned)id>=w->item_patch->first+w->item_patch->count) {
                w->item_patch=item_find(w->c->items,id,&w->item_profile);
                w->item_rejected=0;
                if(w->item_profile) {
                    w->item_profile->patch_tests++;
#ifndef JGR_NO_ITEM_PATCH_REJECT
                    {
                        const ColItems *s=w->c->items;
                        const ColPatch *owner=s->owner_boxes+(w->item_profile-s->profile);
                        if(w->item_owner!=owner) {
                            w->item_owner=owner;
                            if(item_outside(owner,w->item_low,w->item_high)) {
                                w->item_patch=owner;w->item_rejected=1;
                            }
                        }
                        if(!w->item_rejected)w->item_rejected=item_outside(w->item_patch,w->item_low,w->item_high);
                    }
#endif
                }
            }
            if(w->item_patch) {
                ColItemProfile *p=w->item_profile;
                /* Rays and bodies already reject by outward-rounded
                   triangle boxes. A containing owner/patch outside the
                   query box can skip its whole ordered run. */
#ifndef JGR_NO_ITEM_PATCH_REJECT
                if(w->item_rejected) {
                    int low=w->at+1,high=w->at+w->item_patch->count;unsigned end=w->item_patch->first+w->item_patch->count;
                    if(high>w->pend)high=w->pend;
                    while(low<high){int mid=low+(high-low)/2;if((unsigned)col_grid_index(w->c,mid)<end)low=mid+1;else high=mid;}
                    p->patch_skipped+=low-w->at;w->at=low;continue;
                }
#endif
                p->candidates++;
            }
            w->at++;return id;
        }
        if(w->px>w->pxmax){w->px=w->pxmin;w->pz++;}
        if(w->pz>w->pzmax || w->pxmax<w->pxmin)return -1;
        int cell=w->pz*w->c->gw+w->px++;w->at=w->c->cellstart[cell];w->pend=w->c->cellstart[cell+1];
    }
}
#endif
