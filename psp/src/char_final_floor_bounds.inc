/* Optional bounds for the animated Final architecture. The coarse owner
   cube remains the first test. These immutable local AABBs only refine
   crossing batches; source geometry and draw order are never changed. */
#if JGR_FINAL_FLOOR_BOUNDS
typedef struct {unsigned short first,count;} FinalFloorDirectory;
typedef struct {float center[3],extent[3];unsigned short owner,pad;} FinalFloorBound;
static int final_floor_bounds_enabled=1;
static unsigned final_floor_bounds_bytes(const Chars *c)
{
    if(c->h->n_chars!=6 || !c->batch_owner_offsets || c->motion_owner)return 0;
    for(unsigned ci=0;ci<5;ci++) {
        char name[16]={0};snprintf(name,sizeof name,"platform%u",ci);
        if(memcmp(c->chars[ci].name,name,sizeof name))return 0;
    }
    unsigned count=0;
    for(unsigned ci=0;ci<5;ci++) {
        const JgcChar *ch=c->chars+ci;
        if(!ch->nbones || ch->nbones>CHAR_RENDER_MAX_BONES || ch->bone0>c->h->n_bones ||
           ch->nbones>c->h->n_bones-ch->bone0)return 0;
        for(unsigned bi=0;bi<ch->nbones;bi++) {
            const JgcBone *bone=c->bones+ch->bone0+bi;
            if(bone->batch0>c->h->n_batches || bone->nbatch>c->h->n_batches-bone->batch0)return 0;
            for(unsigned j=bone->batch0;j<bone->batch0+bone->nbatch;j++) {
                unsigned n=c->batch_owner_offsets[j+1]-c->batch_owner_offsets[j];
                if(n>CHAR_RENDER_MAX_BONES || count>65535u-n)return 0;
                count+=n;
            }
        }
    }
    if(!count || count>CHAR_FINAL_INDEXED_MAP_BYTES/sizeof(FinalFloorBound))return 0;
    return c->h->n_batches*sizeof(FinalFloorDirectory)+count*sizeof(FinalFloorBound);
}
static void final_floor_bounds_prepare(const Chars *c,FinalIndexedPlan *plan,unsigned offset)
{
    if(!offset)return;
    FinalFloorDirectory *directory=(void *)((unsigned char *)plan+offset);
    FinalFloorBound *bounds=(void *)(directory+c->h->n_batches);
    const unsigned short *owners=(const void *)(c->batch_owner_offsets+c->h->n_batches+1);
    unsigned count=0;
    for(unsigned ci=0;ci<5;ci++) {
        const JgcChar *ch=c->chars+ci;
        for(unsigned bi=0;bi<ch->nbones;bi++) {
            const JgcBone *bone=c->bones+ch->bone0+bi;
            for(unsigned j=bone->batch0;j<bone->batch0+bone->nbatch;j++) {
                const JgcBatch *b=c->batch+j;
                unsigned first=c->batch_owner_offsets[j],end=c->batch_owner_offsets[j+1];
                if(directory[j].count)return; /* Unexpected shared model batch: fail open. */
                directory[j]=(FinalFloorDirectory){count,end-first};
                const unsigned char *vertices=chars_batch_vertices(c,b);
                for(unsigned k=first;k<end;k++) {
                    unsigned owner=owners[k]<ch->nbones ? owners[k]:0;
                    float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
                    for(unsigned v=0;v<b->n_vert;v++) {
                        const SkinVert *src=(const void *)(vertices+v*40u);
                        if((src->bone<ch->nbones ? src->bone:0)!=owner)continue;
                        const float *xyz=&src->x;
                        for(unsigned axis=0;axis<3;axis++) {
                            if(!isfinite(xyz[axis]))return;
                            if(xyz[axis]<low[axis])low[axis]=xyz[axis];
                            if(xyz[axis]>high[axis])high[axis]=xyz[axis];
                        }
                    }
                    FinalFloorBound *bound=bounds+count++;
                    bound->owner=owner;
                    for(unsigned axis=0;axis<3;axis++) {
                        double center=((double)low[axis]+high[axis])*.5;
                        bound->center[axis]=(float)center;
                        double a=(double)high[axis]-bound->center[axis],d=(double)bound->center[axis]-low[axis];
                        bound->extent[axis]=nextafterf((float)(a>d ? a:d),INFINITY);
                        if(!isfinite(bound->center[axis]) || !isfinite(bound->extent[axis]))return;
                    }
                }
            }
        }
    }
    plan->floor_offset=offset; /* Publish only the complete, finite directory. */
}
static void final_floor_batch_codes(const Chars *c,const JgcChar *ch,const JgcBatch *b,
                                   float mats[][16],unsigned *any,unsigned *all)
{
    const FinalIndexedPlan *plan=c->indexed_prepare;
    if(!final_floor_bounds_enabled || !*any || *all || !plan || !plan->floor_offset ||
       plan->header!=c->h || c->vert_stride!=40 || c->motion_owner || ch<c->chars ||
       ch>=c->chars+5)return;
    unsigned batch=(unsigned)(b-c->batch);
    if(batch>=plan->nbatches || plan->batch[batch].nvert!=b->n_vert || plan->batch[batch].flags!=b->flags)return;
    const FinalFloorDirectory *directory=(const void *)((const unsigned char *)plan+plan->floor_offset);
    const FinalFloorBound *bounds=(const void *)(directory+plan->nbatches);
    const FinalFloorDirectory *entry=directory+batch;
    if(!entry->count)return;
    unsigned mask=*any,combined_cross=0,combined_outside=mask;
    for(unsigned i=0;i<entry->count;i++) {
        const FinalFloorBound *bound=bounds+entry->first+i;
        if(bound->owner>=ch->nbones)return;
        const float *m=mats[bound->owner];float center[3],extent[3];
        for(unsigned row=0;row<3;row++) {
            const float *r=m+row*4;
            center[row]=((r[0]*bound->center[0]+r[1]*bound->center[1])+r[2]*bound->center[2])+r[3];
            extent[row]=(fabsf(r[0])*bound->extent[0]+fabsf(r[1])*bound->extent[1])+fabsf(r[2])*bound->extent[2];
            float magnitude=fabsf(r[3]);
            for(unsigned k=0;k<3;k++)magnitude+=fabsf(r[k])*(fabsf(bound->center[k])+bound->extent[k]);
            /* GE matrix coefficients discard eight float mantissa bits.
               Cover that relative error plus ordered float products/sums,
               so a safe CPU bound also contains the GE position. */
            extent[row]+=magnitude*(1.f/32768.f+32.f*FLT_EPSILON)+32.f*FLT_MIN;
            if(!isfinite(center[row]) || !isfinite(extent[row]))return;
        }
        unsigned cross=0,outside=0;
        for(unsigned p=0;p<6;p++)if(mask&(1u<<p)) {
            const float *q=final_floor_guard[p];
            float d=((q[0]*center[0]+q[1]*center[1])+q[2]*center[2])+q[3];
            float reach=(fabsf(q[0])*extent[0]+fabsf(q[1])*extent[1])+fabsf(q[2])*extent[2];
            float magnitude=fabsf(q[3]);
            for(unsigned k=0;k<3;k++)magnitude+=fabsf(q[k])*(fabsf(center[k])+extent[k]);
            reach+=.05f+magnitude*(32.f*FLT_EPSILON)+32.f*FLT_MIN;
            if(!isfinite(d) || !isfinite(reach))return;
            if(d<=reach)cross|=1u<<p;
            if(d < -reach)outside|=1u<<p;
        }
        combined_cross|=cross;combined_outside&=outside;
    }
    *any=combined_cross;*all=combined_outside;
}
#else
static void final_floor_batch_codes(const Chars *c,const JgcChar *ch,const JgcBatch *b,
                                   float mats[][16],unsigned *any,unsigned *all)
{(void)c;(void)ch;(void)b;(void)mats;(void)any;(void)all;}
#endif
