#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "finalcoll.h"
#include "player.h"

static int span(unsigned off,unsigned count,unsigned stride,unsigned length)
{ return off<=length && count<=(length-off)/stride; }
void finalcol_free(FinalCol *c)
{ free(c->matrices);free(c->vertices);free(c->world);free(c->previous);free(c->local_matrices);free(c->world_indices);free(c->unchanged_triangles);memset(c,0,sizeof *c); }
int finalcol_init(FinalCol *c,const void *image,unsigned length)
{
    memset(c,0,sizeof *c);
    if(!image || length<sizeof(FinalColHeader))return -1;
    const FinalColHeader *h=image;
    if(memcmp(h->magic,"FCL1",4) || h->length!=length || !h->frames || h->frames>8192 ||
       h->nodes>FINALCOL_MAX_NODES || !h->nodes || h->vertices>100000 || h->triangles>100000 ||
       (h->off_nodes|h->off_vertices|h->off_triangles|h->off_keys)&3 ||
       !span(h->off_nodes,h->nodes,sizeof(FinalColNode),length) ||
       !span(h->off_vertices,h->vertices,12,length) ||
       !span(h->off_triangles,h->triangles,sizeof(FinalColTri),length))return -2;
    c->data=image;c->h=h;c->nodes=(const FinalColNode *)(c->data+h->off_nodes);
    c->local_vertices=(const float *)(c->data+h->off_vertices);
    c->triangles=(const FinalColTri *)(c->data+h->off_triangles);
    for(unsigned i=0;i<h->nodes;i++) {
        const FinalColNode *n=c->nodes+i;
        if(n->piece<0 || n->piece>5 || n->parent < -1 || n->parent>=(int)i ||
           (n->parent>=0 && c->nodes[n->parent].piece!=n->piece) ||
           !span(n->vertex0,n->nvertices,1,h->vertices) || !span(n->tri0,n->ntriangles,1,h->triangles) ||
           (n->angles && (n->angles<h->off_keys || (n->angles&1) || !span(n->angles,h->frames,6,length))))goto invalid;
        for(int k=0;k<3;k++)if(!isfinite(n->pos[k]) || !isfinite(n->scale[k]))goto invalid;
        if(!n->angles && (n->parent<0 ||
           (c->static_nodes[(unsigned)n->parent/32]&(1u<<((unsigned)n->parent%32)))))
            c->static_nodes[i/32]|=1u<<(i%32);
    }
    for(unsigned i=0;i<h->vertices*3;i++)if(!isfinite(c->local_vertices[i]))goto invalid;
    for(unsigned i=0;i<h->triangles;i++) {
        const FinalColTri *t=c->triangles+i;
        if(t->node>=h->nodes || t->dbl&~(COL_PATCH_BIT31|COL_PATCH_REVERSED))goto invalid;
        const FinalColNode *n=c->nodes+t->node;
        for(int k=0;k<3;k++)if(t->v[k]<n->vertex0 || t->v[k]-n->vertex0>=n->nvertices)goto invalid;
    }
    /* Generated banks store each node's triangles contiguously, but keep
       the generic logical-order path for any otherwise valid FCL1 layout. */
    c->triangle_runs=1;unsigned next_triangle=0;
    for(unsigned i=0;i<h->nodes;i++) {
        const FinalColNode *n=c->nodes+i;
        if(n->ntriangles && n->tri0!=next_triangle)c->triangle_runs=0;
        for(unsigned j=0;j<n->ntriangles;j++)if(c->triangles[n->tri0+j].node!=i)c->triangle_runs=0;
        next_triangle+=n->ntriangles;
    }
    if(next_triangle!=h->triangles)c->triangle_runs=0;
    c->matrices=malloc(h->nodes*16*sizeof(float));c->vertices=malloc(h->vertices*12);
    c->world=malloc(h->triangles*sizeof(ColTri));
    c->previous=malloc(h->triangles*sizeof(ColTri));
    if(!c->matrices || !c->vertices || !c->world || !c->previous) {finalcol_free(c);return -4;}
    /* Optional: low memory retains the identical uncached sampler. */
    c->local_matrices=malloc(h->nodes*16*sizeof(float));
    c->world_indices=malloc(h->triangles*3*sizeof(unsigned));
    unsigned triangle_words=(h->triangles+31)/32,vertex_words=(h->vertices+31)/32;
    c->unchanged_triangles=malloc((triangle_words+vertex_words)*sizeof(uint32_t));
    if(c->unchanged_triangles)c->unchanged_vertices=c->unchanged_triangles+triangle_words;
    return 0;
invalid: memset(c,0,sizeof *c);return -3;
}
int finalcol_bind_overrides(FinalCol *c,const void *image,unsigned length)
{
    if(!c->h || !image || length<24)return -1;
    const unsigned char *data=image;const uint32_t *h=image;
    if(memcmp(data,"FCO1",4) || h[5]!=length || h[1]>c->h->nodes || !h[2] || h[2]>8192 ||
       (h[3]|h[4])&3 || !span(h[3],h[1],16,length) || h[4]<h[3]+h[1]*16)return -2;
    const uint32_t *nodes=(const uint32_t *)(data+h[3]);
    for(unsigned i=0;i<h[1];i++) {
        if(nodes[i*4]!=c->nodes[i].source || c->nodes[i].piece>=4)return -3;
        for(int j=1;j<4;j++) {
            unsigned off=nodes[i*4+j],stride=j==2 ? 6:12;
            if(off && (off<h[4] || off&1 || !span(off,h[2],stride,length)))return -3;
            if(off && j!=2)for(unsigned k=0;k<h[2]*3;k++) {
                float v;memcpy(&v,data+off+k*4,4);if(!isfinite(v))return -3;
            }
        }
    }
    c->override_data=data;c->override_nodes=nodes;c->override_count=h[1];c->override_frames=h[2];
    c->static_pieces=0;memset(c->local_valid,0,sizeof c->local_valid);return 0;
}
int finalcol_sample(FinalCol *c,const float frames[6],unsigned active)
{ return finalcol_sample_overrides(c,frames,active,0,0); }
static int sample(FinalCol *c,const float frames[6],unsigned active,const float collapse_frames[4],unsigned collapse_mask,const float *world)
{
    if(!c->h || !frames)return -1;
    for(int i=0;i<6;i++)if(!isfinite(frames[i]))return -2;
    if(world)for(int i=0;i<16;i++)if(!isfinite(world[i]))return -2;
    if(collapse_mask) {
        if(!collapse_frames || !c->override_data || collapse_mask&~15u)return -3;
        for(int i=0;i<4;i++)if(!isfinite(collapse_frames[i]))return -2;
    }
    /* Deleting a collapsed piece shifts compact triangle indices. Preserve
       the other pieces' carry history rather than freezing their motion for
       one update. A newly activated collider still starts with zero carry. */
    int previous_valid=c->nworld && (c->active&active)==active;
    int publish_indices=c->world_indices && (!c->nworld || c->active!=active);
    /* A supplied root always resamples, even when it happens to be identity.
       Collapse entry/exit and piece reactivation each require a fresh pose.
       The arena's retry path clears nworld to invalidate both carry history
       and these cached transforms. No topology or arithmetic is replaced. */
    unsigned reuse=c->nworld && !world ? c->static_pieces&active&~collapse_mask:0;
    unsigned output_static=previous_valid && c->previous_active==active ?
        c->previous_static_pieces&reuse:0;
    unsigned history_static=previous_valid ? c->static_pieces&active:0;
    int rebuild_stability=c->unchanged_triangles && (!c->nworld || c->unchanged_active!=active || c->unchanged_pieces!=reuse);
    if(rebuild_stability)memset(c->unchanged_triangles,0,((c->h->triangles+31)/32+(c->h->vertices+31)/32)*sizeof(uint32_t));
    /* The last published world already is the exact history we need.
       Reuse the older history buffer for this tick's output instead of
       copying every contact before overwriting the world. */
    if(previous_valid){ColTri *old=c->previous;c->previous=c->world;c->world=old;}
    for(unsigned i=0;i<c->h->nodes;i++) {
        const FinalColNode *n=c->nodes+i;
        /* The loader requires every parent to precede its child and belong
           to the same piece. Keep all active ancestors, including empty
           nodes, while avoiding alternate/deleted pieces' entire hierarchy.
           Reactivation samples the current clock before publishing geometry. */
        if(!(active&(1u<<n->piece)))continue;
        if((reuse&(1u<<n->piece)) && (c->static_nodes[i/32]&(1u<<(i%32)))) {
            if(rebuild_stability)for(unsigned v=n->vertex0;v<n->vertex0+n->nvertices;v++)
                c->unchanged_vertices[v/32]|=1u<<(v%32);
            continue;
        }
        float *m=c->matrices+i*16;
        unsigned bit=1u<<(i%32);
        int local=c->local_matrices && !n->angles && !(collapse_mask&(1u<<n->piece));
        if(local && (c->local_valid[i/32]&bit))memcpy(m,c->local_matrices+i*16,64);
        else {
            int32_t angles[3];memcpy(angles,n->angle,sizeof angles);
            if(n->angles) {
                float frame=fmodf(frames[n->piece],(float)c->h->frames);if(frame<0.f)frame+=c->h->frames;
                int k=(int)frame,k1=k+1<(int)c->h->frames ? k+1:0;float blend=frame-k;
                const int16_t *a=(const int16_t *)(c->data+n->angles)+k*3;
                const int16_t *b=(const int16_t *)(c->data+n->angles)+k1*3;
                /* 104234: signed 16-bit shortest delta, truncate product toward zero. */
                for(int j=0;j<3;j++)angles[j]=a[j]+(int32_t)((int16_t)(b[j]-a[j])*blend);
            }
            float pos[3],scale[3];memcpy(pos,n->pos,12);memcpy(scale,n->scale,12);
            if(i<c->override_count && collapse_mask&(1u<<n->piece)) {
                const uint32_t *o=c->override_nodes+i*4;
                float frame=fmodf(collapse_frames[n->piece],(float)c->override_frames);
                if(frame<0)frame+=c->override_frames;
                int k=(int)frame,k1=k+1<(int)c->override_frames ? k+1:0;float blend=frame-k;
                for(int ch=0;ch<3;ch++)if(o[ch+1]) {
                    const unsigned char *data=c->override_data+o[ch+1];
                    if(ch==1) {
                        const int16_t *a=(const int16_t *)data+k*3,*b=(const int16_t *)data+k1*3;
                        for(int j=0;j<3;j++)angles[j]=a[j]+(int32_t)((int16_t)(b[j]-a[j])*blend);
                    } else {
                        float *out=ch==0 ? pos:scale;
                        for(int j=0;j<3;j++) {
                            float a,b;memcpy(&a,data+(k*3+j)*4,4);memcpy(&b,data+(k1*3+j)*4,4);
                            out[j]=a+(b-a)*blend;
                        }
                    }
                }
            }
            /* 0be878 ignores the base node evaluation bits. Ninja 27a06c mode 0
               applies T*Rz*Ry*Rx*S. Reuse the recovered SH4 FSCA matrix helper. */
            player_scene_matrix(pos,angles,scale,m);
            if(local){memcpy(c->local_matrices+i*16,m,64);c->local_valid[i/32]|=bit;}
        }
        if(n->parent>=0 || world) {
            float out[16];const float *p=n->parent>=0 ? c->matrices+n->parent*16:world;
            for(int row=0;row<4;row++)for(int col=0;col<4;col++) {
                float v=0;for(int k=0;k<4;k++)v+=p[row*4+k]*m[k*4+col];out[row*4+col]=v;
            }
            memcpy(m,out,sizeof out);
        }
        for(unsigned v=n->vertex0;v<n->vertex0+n->nvertices;v++) {
            const float *p=c->local_vertices+v*3;float *out=c->vertices+v*3;
            for(int k=0;k<3;k++)out[k]=m[k*4]*p[0]+m[k*4+1]*p[1]+m[k*4+2]*p[2]+m[k*4+3];
        }
    }
    int grouped=c->triangle_runs && previous_valid && c->active==active && !publish_indices && !rebuild_stability && output_static;
    c->nworld=0;int old_index=0;
    if(grouped)for(unsigned node=0;node<c->h->nodes;node++) {
        const FinalColNode *n=c->nodes+node;unsigned piece=1u<<n->piece;
        if(!(active&piece))continue;
        if((output_static&piece) && (c->static_nodes[node/32]&(1u<<(node%32)))) {
            c->nworld+=(int)n->ntriangles;continue;
        }
        for(unsigned i=n->tri0;i<n->tri0+n->ntriangles;i++) {
            const FinalColTri *t=c->triangles+i;ColTri *out=c->world+c->nworld++;
            for(int k=0;k<3;k++)memcpy(out->v+k*3,c->vertices+t->v[k]*3,12);
            out->surf=t->surf;out->mask=t->mask;out->dbl=t->dbl;out->src=t->node;
        }
    }
    else
    for(unsigned i=0;i<c->h->triangles;i++) {
        const FinalColTri *t=c->triangles+i;
        unsigned piece=1u<<c->nodes[t->node].piece;
        int old=(c->active&piece) ? old_index++:-1;
        if(!(active&piece))continue;
        if(publish_indices)memcpy(c->world_indices+c->nworld*3,t->v,12);
        /* Removal compacts forward (new index <= old index). Unchanged
           active masks need neither a copy nor a self-assignment. */
        if(previous_valid && old!=c->nworld)c->previous[c->nworld]=c->previous[old];
        if(rebuild_stability && (reuse&piece) && (c->static_nodes[t->node/32]&(1u<<(t->node%32))))
            c->unchanged_triangles[(unsigned)c->nworld/32]|=1u<<((unsigned)c->nworld%32);
        ColTri *out=c->world+c->nworld++;
        /* The alternating output buffer already owns these exact immutable
           corners/flags. Transform reuse alone is insufficient: both the
           buffer's compact topology and its normal-root provenance matter. */
        if((output_static&piece) && (c->static_nodes[t->node/32]&(1u<<(t->node%32))))continue;
        for(int k=0;k<3;k++)memcpy(out->v+k*3,c->vertices+t->v[k]*3,12);
        out->surf=t->surf;out->mask=t->mask;out->dbl=t->dbl;out->src=t->node;
    }
    if(!previous_valid)memcpy(c->previous,c->world,c->nworld*sizeof(ColTri));
    c->active=active;c->static_pieces=world ? 0:active&~collapse_mask;
    c->previous_active=active;
    c->previous_static_pieces=previous_valid ? history_static:c->static_pieces;
    c->unchanged_active=active;c->unchanged_pieces=reuse;
    if(!++c->sample_generation)c->sample_generation=1;
    return c->nworld;
}
int finalcol_sample_overrides(FinalCol *c,const float frames[6],unsigned active,const float collapse_frames[4],unsigned collapse_mask)
{ return sample(c,frames,active,collapse_frames,collapse_mask,0); }
int finalcol_sample_world(FinalCol *c,const float frames[6],unsigned active,const float world[16])
{ return sample(c,frames,active,0,0,world); }
int finalcol_surface(void *context,int triangle,const float *point,float *delta,int *yaw)
{
    FinalCol *c=context;
    if(!c || triangle<0 || triangle>=c->nworld)return 0;
    float current[9],previous[9];
    col_contact_vertices(c->world+triangle,current);
    col_contact_vertices(c->previous+triangle,previous);
    return player_surface_motion(current,previous,point,delta,yaw);
}
