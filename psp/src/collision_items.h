#ifndef JGR_COLLISION_ITEMS_H
#define JGR_COLLISION_ITEMS_H
/* Optional CIP1 suffix: source owner/range records followed by ordered
   source strip boxes. Keep the old cell index and logical IDs unchanged. */
typedef struct {unsigned address,kind,first,count;} ColItemOwner;
struct ColItems {
    unsigned nowner,npatch,bytes,first,ntri;void *data;
    ColItemOwner *owners;ColPatch *patches;unsigned *owner_ids;
    ColPatch *owner_boxes;unsigned short *triangle_patches;
    ColItemProfile *profile;
};
static void items_free(ColItems *s)
{if(s){free(s->profile);free(s->data);free(s);}}
static int items_load(Collision *c,SceUID fd,SceOff remaining,unsigned nworld)
{
    if(!remaining)return 0;
    if(remaining<16)return -8;
    unsigned h[4];if(read_exact(fd,h,16))return -9;
    if(h[0]!=0x31504943u || !h[1] || h[1]>65535 || !h[2] || h[2]>65535 || h[3])return -8;
    unsigned bytes=h[1]*16+h[2]*36;
    if(remaining!=16+(SceOff)bytes)return -8;
    if(nworld>=(unsigned)c->nbase)return -8;
    ColItems *s=calloc(1,sizeof *s);if(!s)return -3;
    s->nowner=h[1];s->npatch=h[2];s->first=nworld;s->ntri=c->nbase-nworld;
    unsigned resident=bytes+s->nowner*sizeof(ColPatch)+s->ntri*2;
    s->data=memalign(16,resident);
    s->profile=calloc(s->nowner,sizeof *s->profile);
    if(!s->data || !s->profile){items_free(s);return -3;}
    int err=read_exact(fd,s->data,bytes);if(err){items_free(s);return err;}
    s->owners=s->data;s->patches=(ColPatch *)(s->owners+s->nowner);
    s->owner_ids=(unsigned *)(s->patches+s->npatch);
    s->owner_boxes=(ColPatch *)(s->owner_ids+s->npatch);
    s->triangle_patches=(unsigned short *)(s->owner_boxes+s->nowner);
    s->bytes=sizeof *s+resident+s->nowner*sizeof *s->profile;
    unsigned next=0,owner=0;
    for(unsigned i=0;i<s->nowner;i++) {
        ColItemOwner *o=s->owners+i;
        if(!o->count || o->count>(unsigned)c->nbase || o->first>(unsigned)c->nbase-o->count ||
           (i && o->first!=next))goto invalid;
        next=o->first+o->count;
        ColPatch *b=s->owner_boxes+i;b->first=o->first;b->count=o->count;
        for(int k=0;k<3;k++){b->low[k]=INFINITY;b->high[k]=-INFINITY;}
    }
    if(s->owners[0].first!=nworld || next!=(unsigned)c->nbase)goto invalid;
    next=s->owners[0].first;
    for(unsigned i=0;i<s->npatch;i++) {
        ColPatch *p=s->patches+i;
        if(s->owner_ids[i]>=s->nowner || !p->count || p->first!=next)goto invalid;
        owner=s->owner_ids[i];ColItemOwner *o=s->owners+owner;
        if(p->first<o->first || p->first>=o->first+o->count || p->count>o->first+o->count-p->first)goto invalid;
        for(int k=0;k<3;k++)if(!isfinite(p->low[k]) || !isfinite(p->high[k]) || p->low[k]>p->high[k])goto invalid;
        for(unsigned j=0;j<p->count;j++) {
            const ColTri *t=static_triangle(c,p->first+j);
            for(int k=0;k<9;k++)if(t->v[k]<p->low[k%3] || t->v[k]>p->high[k%3])goto invalid;
            s->triangle_patches[p->first+j-nworld]=i;
        }
        ColPatch *b=s->owner_boxes+owner;
        for(int k=0;k<3;k++){if(p->low[k]<b->low[k])b->low[k]=p->low[k];if(p->high[k]>b->high[k])b->high[k]=p->high[k];}
        next+=p->count;
    }
    if(s->owners[0].first!=nworld || next!=(unsigned)c->nbase)goto invalid;
    c->items=s;return 0;
invalid:items_free(s);return -8;
}
static const ColPatch *item_find(ColItems *s,unsigned id,ColItemProfile **profile)
{
    *profile=NULL;if(!s || id-s->first>=s->ntri)return NULL;
    unsigned patch=s->triangle_patches[id-s->first];
    *profile=s->profile+s->owner_ids[patch];return s->patches+patch;
}
static int item_outside(const ColPatch *p,const float *lo,const float *hi)
{
    for(int k=0;k<3;k++)if(p->high[k]<lo[k] || p->low[k]>hi[k])return 1;
    return 0;
}
static unsigned item_exact_begin(ColItemProfile *p,int body)
{
    if(!p)return 0;
    if(body)p->body_exact++;else p->ray_exact++;
    /* Sample one in 32 exact tests. Report raw sample count/time, never
       label a preempted sample as complete per-object execution time. */
    if(((p->body_exact+p->ray_exact)&31)!=1)return 0;
    p->samples++;return collision_clock()+1;
}
static void item_exact_end(ColItemProfile *p,unsigned stamp)
{if(stamp)p->sample_us+=collision_clock()-(stamp-1);}
int col_profile_items(const Collision *c,ColItemProfile *out,int capacity)
{
    ColItems *s=c?c->items:NULL;if(!s)return 0;int count=0;
    for(int k=0;k<capacity;k++) {
        unsigned best=0,score=0;
        for(unsigned i=0;i<s->nowner;i++) {
            unsigned n=s->profile[i].candidates+s->profile[i].patch_skipped;
            if(n>score){best=i;score=n;}
        }
        if(!score)break;
        out[count]=s->profile[best];memcpy(out+count,s->owners+best,sizeof(ColItemOwner));count++;
        memset(s->profile+best,0,sizeof *s->profile);
    }
    memset(s->profile,0,s->nowner*sizeof *s->profile);return count;
}
#endif
