/* Original mode-2 graffiti arrows: two rigid, untextured parts; only the
   root position/scale is keyed. Keep the GE's original one-weight vertex
   transform, material order and sidedness. No mesh reduction or clock change. */
#include "pose_sample.h"
typedef struct {unsigned weight;DrawVert v;} ArrowVertex;
typedef struct {float frame,local[2][6];unsigned model,valid;} ArrowPose;
typedef struct {
    const Chars *bank;int valid;
    ArrowVertex vertices[2][2][14];
    float bounds[2][2][6];
    ArrowPose poses[16];unsigned next;
} ArrowCache;
static ArrowCache arrow_cache __attribute__((aligned(64)));
static float arrow_planes[6][4];
static int arrow_planes_valid,arrow_enabled=1,arrow_state;
static unsigned arrow_started;
static CharArrowProfile arrow_totals,arrow_previous;
typedef char arrow_workspace_budget[(sizeof(ArrowCache)+sizeof arrow_planes+
    sizeof arrow_totals+sizeof arrow_previous+64<=CHAR_ARROW_BYTES) ? 1:-1];
static void forget_arrows(const Chars *c)
{if(arrow_cache.bank==c){arrow_cache.bank=NULL;arrow_cache.valid=0;}}
void chars_arrow_mode(int enabled){arrow_enabled=enabled;}
void chars_arrow_totals(CharArrowProfile *p){*p=arrow_totals;}
void chars_arrow_profile(CharArrowProfile *p)
{
#define ARROW_DELTA(field) p->field=arrow_totals.field-arrow_previous.field
    ARROW_DELTA(scopes);ARROW_DELTA(requests);ARROW_DELTA(drawn);ARROW_DELTA(culled);
    ARROW_DELTA(reference);ARROW_DELTA(pose_built);ARROW_DELTA(pose_reused);
    ARROW_DELTA(draw_calls);ARROW_DELTA(total_us);
#undef ARROW_DELTA
    arrow_previous=arrow_totals;
}
void chars_arrow_projection(const float *p)
{
    arrow_planes_valid=0;if(!p)return;
    for(unsigned i=0;i<16;i++)if(!isfinite(p[i]))return;
    /* Eight pixels outside each edge. This rejects only a fully invisible
       animated mesh; it never shortens the source distance/owner gates. */
    for(unsigned axis=0;axis<3;axis++)for(unsigned side=0;side<2;side++)
        for(unsigned k=0;k<4;k++)arrow_planes[axis*2+side][k]=
            p[12+k]*(axis==0 ? 1.f+16.f/SCR_W:axis==1 ? 1.f+16.f/SCR_H:1.f)+
            (side ? -p[axis*4+k]:p[axis*4+k]);
    arrow_planes_valid=1;
}
static int arrow_prepare(Chars *c)
{
    if(arrow_cache.bank==c)return arrow_cache.valid;
    memset(&arrow_cache,0,sizeof arrow_cache);arrow_cache.bank=c;
    if(!c->h || c->h->n_chars!=2 || c->h->n_bones!=4 || c->h->n_batches!=4 ||
       c->h->n_tex || c->h->n_motions!=2 || c->vert_stride<28 || c->motion_owner)return 0;
    for(unsigned ci=0;ci<2;ci++) {
        const JgcChar *ch=c->chars+ci;
        if(ch->bone0!=ci*2 || ch->nbones!=2 || ch->nmotions!=1 || ch->motion0!=ci)return 0;
        const JgcMotion *mo=c->motions+ci;
        const JgcChan *cn=(const JgcChan *)chars_data(c,mo->chan0);
        if(mo->nframes!=120 || cn[0].npos!=120 || cn[0].nscl!=120 || cn[0].nang ||
           cn[1].npos || cn[1].nscl || cn[1].nang)return 0;
        for(unsigned bi=0;bi<2;bi++) {
            const JgcBone *bone=c->bones+ch->bone0+bi;
            const JgcBatch *b=c->batch+ci*2+bi;
            if(bone->parent!=(bi ? (int)ch->bone0:-1) || bone->batch0!=ci*2+bi || bone->nbatch!=1 ||
               bone->ang[0] || bone->ang[1] || bone->ang[2] ||
               b->flags!=(bi ? 0x59c:0x59e) || b->n_vert!=(bi ? 14:7) || b->n_idx!=(bi ? 30:15))return 0;
            const unsigned char *src=chars_batch_vertices(c,b);
            float *bounds=arrow_cache.bounds[ci][bi];
            for(unsigned j=0;j<b->n_vert;j++) {
                const SkinVert *v=(const SkinVert *)(src+j*c->vert_stride);
                if(v->bone!=bi || !isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z))return 0;
                ArrowVertex *out=arrow_cache.vertices[ci][bi]+j;
                out->weight=128;out->v=(DrawVert){v->u,v->v,v->color,v->x,v->y,v->z};
                const float xyz[3]={v->x,v->y,v->z};
                for(unsigned a=0;a<3;a++) {
                    if(!j || xyz[a]<bounds[a])bounds[a]=xyz[a];
                    if(!j || xyz[a]>bounds[a+3])bounds[a+3]=xyz[a];
                }
            }
            const unsigned short *indices=chars_batch_indices(c,b);
            for(unsigned j=0;j<b->n_idx;j++)if(indices[j]>=b->n_vert)return 0;
        }
    }
#ifdef __PSP__
    sceKernelDcacheWritebackRange(&arrow_cache,sizeof arrow_cache);
#endif
    arrow_cache.valid=1;return 1;
}
static const ArrowPose *arrow_pose(Chars *c,unsigned model,float frame)
{
    for(unsigned i=0;i<16;i++) {
        const ArrowPose *p=arrow_cache.poses+i;
        if(p->valid && p->model==model && p->frame==frame){arrow_totals.pose_reused++;return p;}
    }
    ArrowPose *p=arrow_cache.poses+(arrow_cache.next++&15);p->model=model;p->frame=frame;p->valid=1;
    CharSampleCursor cursor;chars_sample_prepare(c,c->chars+model,model,frame,&cursor);
    for(unsigned i=0;i<2;i++) {
        CharPose pose;float local[16];chars_sample_prepared(&cursor,i,&pose);
        chars_local_matrix(local,pose.ev,pose.pos,pose.ang,pose.scl);
        for(unsigned a=0;a<3;a++){p->local[i][a]=local[a*5];p->local[i][3+a]=local[a*4+3];}
    }
    arrow_totals.pose_built++;return p;
}
static void arrow_matrices(float mats[2][16],const float *root,const ArrowPose *pose)
{
    /* Sparse diagonal affine products in the same hierarchy order. Keep
       the intermediate root rounding BEFORE applying the child's offset. */
    memset(mats,0,128);mats[0][15]=mats[1][15]=1.f;
    for(unsigned i=0;i<2;i++) {
        const float *parent=i ? mats[0]:root,*local=pose->local[i];
        for(unsigned a=0;a<3;a++) {
            mats[i][a*5]=0.f+parent[a*5]*local[a];
            float v=0.f+parent[a*5]*local[3+a];
            mats[i][a*4+3]=v+parent[a*4+3];
        }
    }
}
static int arrow_visible(unsigned model,float mats[2][16])
{
    if(!arrow_planes_valid)return 1;
    float lo[3],hi[3];
    for(unsigned b=0;b<2;b++)for(unsigned a=0;a<3;a++) {
        const float *bounds=arrow_cache.bounds[model][b],*m=mats[b];
        float x=m[a*5]*bounds[a]+m[a*4+3],y=m[a*5]*bounds[a+3]+m[a*4+3];
        if(!isfinite(x) || !isfinite(y))return 1;
        if(x>y){float t=x;x=y;y=t;}
        if(!b || x<lo[a])lo[a]=x;
        if(!b || y>hi[a])hi[a]=y;
    }
    for(unsigned p=0;p<6;p++) {
        const float *q=arrow_planes[p];float d=q[3],pad=.001f;
        for(unsigned a=0;a<3;a++) {
            float far=q[a]*(q[a]<0 ? lo[a]:hi[a]);d+=far;
            pad+=fabsf(q[a])*.25f+fabsf(far)*1e-5f;
        }
        if(isfinite(d) && d < -pad)return 0;
    }
    return 1;
}
void chars_arrows_begin(void)
{arrow_started=render_clock();arrow_state=0;arrow_totals.scopes++;}
void chars_draw_arrow(Chars *c,int model,const float *root,float frame)
{
    arrow_totals.requests++;
    if(!arrow_enabled || (unsigned)model>=2 || !isfinite(frame) || background_planes || material_planes ||
       recovery_active || spray_hud_lighting || hud_reciprocal_scale!=1.f || hud_depth_shift!=0.f || !arrow_prepare(c))goto reference;
    for(unsigned r=0;r<4;r++)for(unsigned k=0;k<4;k++)
        if(!isfinite(root[r*4+k]) || (r!=k && k!=3 && root[r*4+k]!=0.f))goto reference;
    if(root[15]!=1.f)goto reference;
    float mats[2][16];arrow_matrices(mats,root,arrow_pose(c,model,frame));
    if(!arrow_visible(model,mats)){arrow_totals.culled++;return;}
    if(!arrow_state) {
        float identity[16];m_ident(identity);gu_model(identity);
        sceGuEnable(GU_DEPTH_TEST);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_BLEND);
        sceGuDepthMask(GU_FALSE);sceGuDisable(GU_TEXTURE_2D);cur_tex=-1;arrow_state=1;
    }
    for(unsigned bi=0;bi<2;bi++) {
        const JgcBatch *b=c->batch+model*2+bi;
        if(!bi)sceGuDisable(GU_CULL_FACE);
        else {sceGuEnable(GU_CULL_FACE);sceGuFrontFace(GU_CCW);}
#if CHAR_GE_SKINNING
        /* Identical one-weight format and arithmetic to the general path.
           Immutable vertices are published once per bank, never per arrow. */
        ge_submit_bone(0,mats[bi]);bone_uploads++;
        ge_submit_draw(GU_TRIANGLES,VFMT|GU_WEIGHT_8BIT|GU_WEIGHTS(1),b->n_idx,
            chars_batch_indices(c,b),arrow_cache.vertices[model][bi]);
        c->drawn_tris+=b->n_idx/3;ge_skin_batches++;ge_skin_vertices+=b->n_vert;
        weighted_vertices+=b->n_vert;if(max_weights<1)max_weights=1;
#else
        draw_skin(c,b,skin_batch(c,c->chars+model,b,mats),chars_batch_indices(c,b));
#endif
        arrow_totals.draw_calls++;
    }
    arrow_totals.drawn++;return;
reference:
    chars_draw_effect(c,model,root,frame,1.f);arrow_state=0;
    arrow_totals.reference++;arrow_totals.drawn++;arrow_totals.draw_calls+=2;
}
void chars_arrows_end(void)
{
    if(arrow_state)state_end();arrow_state=0;
    arrow_totals.total_us+=render_clock()-arrow_started;
}
