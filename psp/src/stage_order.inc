/* Bounded opaque queue. Reorder only disjoint projected bounds, retaining
   every overlapping pair's original order (including coplanar wall detail).
   Alpha, near-plane crossings and cell changes remain ordering barriers. */
#define STAGE_ORDER_WINDOW 16
typedef struct {const JgsBatch *batch;const StageBatchBound *bound;float rect[4];unsigned mask,projected;} StageOrderEntry;
typedef struct {
    StageOrderEntry entry[STAGE_ORDER_WINDOW];unsigned count;
    int tex,two,cell_x,cell_z;
    const float *clip;float radius_x,radius_y,radius_w;
} StageOrder;
static void ordered_draw(Stage *s,StageOrder *order,const JgsBatch *b,unsigned mask,
    const float planes[6][4],const float guard[6][4],int cullmode)
{
    {CITY_SCOPE(s,CITY_MATERIAL);
    if((b->flags&JGS_B_NOTEX) || stage_flat) {
        if(order->tex!=-2){sceGuDisable(GU_TEXTURE_2D);order->tex=-2;}
    } else {
        if(order->tex==-2)sceGuEnable(GU_TEXTURE_2D);
        if(b->tex!=order->tex){bind_tex(s,b->tex);order->tex=b->tex;}
    }
    int two=(b->flags&JGS_B_2SIDED) || cullmode==0;
    if(two!=order->two){if(two)sceGuDisable(GU_CULL_FACE);else sceGuEnable(GU_CULL_FACE);order->two=two;}
    }
    draw_batch(s,b,planes,guard,mask);
}
static int stage_rect_disjoint(const float a[4],const float b[4])
{return a[2]<b[0] || b[2]<a[0] || a[3]<b[1] || b[3]<a[1];}
static int stage_project_bound(const StageOrder *q,const StageBatchBound *b,float rect[4])
{
    const float *m=q->clip;
    float w=m[12]*b->cx+m[13]*b->cy+m[14]*b->cz+m[15],rw=q->radius_w*b->r;
    float lo=w-rw,hi=w+rw;if(!(lo>.001f) || !isfinite(hi))return 0;
    for(int axis=0;axis<2;axis++) {
        unsigned a=axis*4;
        float c=m[a]*b->cx+m[a+1]*b->cy+m[a+2]*b->cz+m[a+3];
        float r=(axis ? q->radius_y:q->radius_x)*b->r;
        float v0=(c-r)/lo,v1=(c-r)/hi,v2=(c+r)/lo,v3=(c+r)/hi;
        /* Two PSP pixels plus numerical slack on either side. */
        float margin=axis ? 4.f/272.f:4.f/480.f;
        rect[axis]=fminf(v0,v1)-margin;rect[axis+2]=fmaxf(v2,v3)+margin;
        if(!isfinite(rect[axis]) || !isfinite(rect[axis+2]))return 0;
    }
    return 1;
}
static const float *order_rect(StageOrder *q,unsigned index)
{
    StageOrderEntry *e=q->entry+index;
    if(!e->projected) {
        if(!stage_project_bound(q,e->bound,e->rect)) {
            e->rect[0]=e->rect[1]=-INFINITY;e->rect[2]=e->rect[3]=INFINITY;
        }
        e->projected=1;
    }
    return e->rect;
}
static void flush_order(Stage *s,StageOrder *q,const float planes[6][4],const float guard[6][4],int cullmode)
{
    while(q->count) {
        unsigned pick=0;
        if(q->entry[0].batch->tex!=q->tex)for(unsigned i=1;i<q->count;i++)if(q->entry[i].batch->tex==q->tex) {
            unsigned j=0;for(;j<i;j++)if(!stage_rect_disjoint(order_rect(q,i),order_rect(q,j)))break;
            if(j==i){pick=i;profile.sorted_batches++;break;}
        }
        StageOrderEntry e=q->entry[pick];
        for(unsigned i=pick+1;i<q->count;i++)q->entry[i-1]=q->entry[i];
        q->count--;
        ordered_draw(s,q,e.batch,e.mask,planes,guard,cullmode);
    }
}
