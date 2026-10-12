/* Source visual tasks. Included after the original BAM math in player.c so
   orientations use the same SH-4 lookup/atan, not a second approximation. */
#if JGR_PORTABLE_ASSETS
#include "source_data_player_effects.inc"
#else
#include "generated/source_data/player_effects.inc"
#endif
static float effect_distance(const float *a,const float *b)
{
    float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return sqrtf(x*x+y*y+z*z);
}
static void effect_advance(float *frame,int n)
{
    *frame+=1.f;if (*frame>=n) *frame-=n;
}
static void effect_paint_matrix(float *matrix,const float *origin,const float *target)
{
    float x=origin[0]-target[0],z=origin[2]-target[2];
    int32_t angles[3]={bam_atan2(target[1]-origin[1],sqrtf(x*x+z*z))+32768,bam_atan2(x,z),0};
    const float scale[3]={1,1,1};source_trs(origin,angles,scale,matrix);
}
/* 044bec/044f78: T(current) * Ry(atan(dx,dz)) * Rx(atan(-dy,hypot)) * S. */
static void effect_segment(float *m,const float *current,const float *anchor,float scale)
{
    float x=current[0]-anchor[0],y=current[1]-anchor[1],z=current[2]-anchor[2];
    int yaw=bam_atan2(x,z),pitch=bam_atan2(-y,sqrtf(x*x+z*z));
    float sy=bam_sin(yaw),cy=bam_cos(yaw),sx=bam_sin(pitch),cx=bam_cos(pitch);
    float r[16]={cy,sy*sx,sy*cx,current[0],0,cx,-sx,current[1],-sy,cy*sx,cy*cx,current[2],0,0,0,1};
    for (int i=0;i<3;i++) for (int k=0;k<3;k++) r[i*4+k]*=scale;
    memcpy(m,r,sizeof r);
}
static void effect_grind(SkateBurst *e,int enabled,float speed,const float *root,const float *foot)
{
    e->active=enabled;
    if (!enabled) return;
    float signed_scale=fmaxf(-1.f,fminf(1.f,speed/0x1.284bdap+1f));
    float scale[3]={fabsf(signed_scale),fabsf(signed_scale),signed_scale};
    memcpy(e->matrix,root,sizeof e->matrix);
    for (int r=0;r<3;r++) {
        e->matrix[r*4+3]=foot[r];
        for (int k=0;k<3;k++) e->matrix[r*4+k]*=scale[k];
    }
    effect_advance(&e->frame,12); /* original motion 8c8eb500 */
}
static void effect_tail(SkateEffects *fx,const SkateStreak *w)
{
    for (unsigned i=0;i<32;i++) if (!fx->tails[i].active) {
        SkateTail *t=&fx->tails[i];t->active=1;t->alpha=1.f;t->frame=w->frame;
        memcpy(t->matrix,w->matrix,sizeof t->matrix);return;
    }
}
static void effect_wall(SkateEffects *fx,int side,unsigned flags,int obstruction,
                        const float *body,const float *foot,const float *external)
{
    SkateStreak *w=&fx->wall[side];
    int enabled=(flags&(side ? 0x20 : 0x10))!=0;
    if (enabled && !w->enabled) {
        w->countdown=(flags&2) ? 6 : -1;w->frame=0.f;
        memcpy(w->anchor,w->current,sizeof w->anchor);
    }
    w->enabled=enabled;
    if (enabled && w->countdown!=0 && effect_distance(w->previous_body,body)>.5f && !obstruction) {
        for (int k=0;k<3;k++) w->anchor[k]+=external[k];
        memcpy(w->current,foot,sizeof w->current);
        effect_segment(w->matrix,foot,w->anchor,effect_distance(foot,w->anchor)*.05f);
        effect_advance(&w->frame,41); /* original motion 8c8ead08 */
        if (w->countdown>0) w->countdown--;
        w->active=1;
    } else {
        if (w->active) effect_tail(fx,w); /* 045448 -> 045514 */
        memcpy(w->current,foot,sizeof w->current);w->active=0;
    }
    memcpy(w->previous_body,body,sizeof w->previous_body);
}
/* 045770: boost contact sparks use the completed locomotion frame. */
static int effect_dash_contact(const SkateInput *in,int side)
{
    int frame=(int)in->frame;
    return (in->motion==6 || in->motion==7) && in->boost && !in->obstruction &&
           frame>=0 && frame<28 && SKATE_DASH_CONTACT[side][frame];
}
static int effect_trail_enabled(const Player *p,const SkateInput *in)
{
    const CharTuning *t=p->tun ? p->tun:&BEAT_TUNING;
    /* 0463c0: ramp launch (8), four spin tricks (38..3b), otherwise
       the character's fast-trick threshold. This includes fast jumps. */
    if(in->motion==8)return fabsf(in->speed)>=t->ramp_launch_speed;
    if(in->motion>=0x38 && in->motion<=0x3b)return 1;
    return fabsf(in->speed)>=t->trick_speed;
}
static void effect_ribbon_retire(SkateEffects *fx,SkateRibbon *r)
{
    if(r->count>1)for(int i=0;i<20;i++)if(!fx->trail_tails[i].count){fx->trail_tails[i]=*r;break;}
    memset(r,0,sizeof *r);
}
static void effect_ribbons(SkateEffects *fx,const SkateInput *in,int long_enabled)
{
    for(int i=0;i<20;i++)if(fx->trail_tails[i].count){
        SkateRibbon *r=fx->trail_tails+i;r->count--;r->uv_shift+=1.f/9.f;
        if(r->count<2)r->count=0;
    }
    for(int side=0;side<2;side++){
        for(int j=2;j>0;j--)for(int e=0;e<2;e++)for(int k=0;k<3;k++)
            fx->dash_points[side][j][e][k]=fx->dash_points[side][j-1][e][k]+in->external[k];
        memcpy(fx->dash_points[side][0],in->points+side*2,24);
        SkateRibbon *r=fx->trail+side;
        if(long_enabled){
            if(r->count<10)r->count++;
            memmove(r->points+1,r->points,(r->count-1)*sizeof r->points[0]);
            memcpy(r->points[0],in->points+4+side*2,24);r->enabled=1;
        } else if(r->count)effect_ribbon_retire(fx,r);
    }
    if((in->flags&64) && in->dash){if(fx->dash_age<3)fx->dash_age++;}
    else if(fx->dash_age)fx->dash_age--;
}
static void effect_live_input(const Player *p,SkateInput *in)
{
    memset(in,0,sizeof *in);in->flags=p->motion_flags;in->motion=p->logical_motion;
    in->mirrored=p->mirrored;in->grind_pose=p->grind_pose;in->dash=p->dash_held;
    in->boost=p->boost;in->frame=p->frame;in->speed=p->speed;
    in->obstruction=p->sound_contact_override;
    int32_t angles[3];player_motion_angles(p,angles);
    for(int k=0;k<3;k++)in->travel_angles[k]=angles[k];
    /* Original +684 anchors come from the collision hierarchy, including
       overlays, not the mesh's display/eval-flag hierarchy. */
    /* Short dash history and moving feet must advance even when hidden.
       Long ribbons and stationary grind sparks read their anchors only
       while enabled; avoid transforming those unused points. */
    unsigned used=0xc0f;
    if(effect_trail_enabled(p,in))used|=0xf0;
    if((in->flags&1) && (unsigned)in->grind_pose<6 &&
       SKATE_GRIND_MASK[p->pose_model][!!in->mirrored][in->grind_pose])used|=0x300;
#ifdef JGR_TEST_FULL_EFFECT_ANCHORS
    used=0xfff;
#endif
    unsigned mask=0;for(int j=0;j<12;j++)if(used&(1u<<j))mask|=1u<<SKATE_ANCHORS[p->pose_model][j<8 ? j:j+11].bone;
    float mats[20][16];player_body_matrices_masked(p,mats,mask);
    const float *matrices[12],*points[12];float result[12][3];int n=0;
    for(int j=0;j<12;j++)if(used&(1u<<j)){
        const SkateAnchor *a=SKATE_ANCHORS[p->pose_model]+(j<8 ? j:j+11);
        matrices[n]=mats[a->bone];points[n++]=a->pos;
    }
    source_point_batch(matrices,points,result,n);
    n=0;for(int j=0;j<12;j++)if(used&(1u<<j))memcpy(in->points[j],result[n++],12);
}
static void effect_impact_position(Player *p)
{
    SkateBurst *b=&p->effects.impact;
    float point[3]={p->x,p->y,p->z};
    if(p->pose_bank && p->pose_valid && (unsigned)p->pose_model<16){
        const SkateAnchor *a=SKATE_ANCHORS[p->pose_model]+18;
        float bones[20][16];player_body_matrices_masked(p,bones,1u<<a->bone);
        source_point(bones[a->bone],a->pos,point);
    }
    memset(b->matrix,0,sizeof b->matrix);
    b->matrix[0]=b->matrix[5]=b->matrix[10]=b->matrix[15]=1;
    b->matrix[3]=point[0];b->matrix[7]=point[1];b->matrix[11]=point[2];
}
void player_effects_tick(Player *p,const SkateInput *recorded)
{
    SkateEffects *fx=&p->effects;
    if (!p->pose_bank || !p->pose_valid || (unsigned)p->pose_model>=16) return;
    if(p->state==10 || p->state==11){memset(fx,0,sizeof *fx);return;}
    SkateInput live;if(!recorded){effect_live_input(p,&live);recorded=&live;}
    const SkateInput *in=recorded;
    float body[3]={p->x,p->y,p->z},root[16];player_root_matrix(p,root);
    if (!fx->initialized) {
        SkateBurst impact=fx->impact;
        memset(fx,0,sizeof *fx);fx->impact=impact;fx->initialized=1;fx->bubble.kind=-1;
        for (int side=0;side<2;side++) {
            memcpy(fx->wall[side].current,body,sizeof body);
            memcpy(fx->wall[side].previous_body,body,sizeof body);
            fx->wall[side].countdown=-1;
            memcpy(fx->previous_foot[side],in->points[10+side],12);
            memcpy(fx->wall[side].current,in->points[10+side],12);
            for(int j=0;j<3;j++)memcpy(fx->dash_points[side][j],in->points+side*2,24);
        }
    }
    if(fx->impact.active) {
        /* 043904 follows PLAY+75c, independent of the contact limb/yaw.
           27f13c translates the identity task matrix; no player rotation. */
        SkateBurst *b=&fx->impact;
        if(++b->frame>=15)b->active=0;
        else effect_impact_position(p);
    }
    spray_bubble_tick(&fx->bubble,p->spray_hint_ready,p->spray_hint_kind,p->spray_active);
    if(fx->bubble.scale>0) {
        const SkateAnchor *a=SKATE_ANCHORS[p->pose_model]+28;
        float bones[20][16];player_body_matrices_masked(p,bones,1u<<a->bone);
        const float *m=bones[a->bone],*v=a->pos;
        source_point(m,v,fx->bubble.head);
    }
    /* 047084 uses collision-hierarchy anchors 16/17, not display bones.
       A small spray keeps its accepted GF target even after leaving it. */
    fx->spray.active=!!(p->motion_flags&0x80) || p->small_spray_active;
    if(fx->spray.active) {
        const SkateAnchor *a=SKATE_ANCHORS[p->pose_model]+16;
        float bones[20][16],point[2][3];
        player_body_matrices_masked(p,bones,(1u<<a[0].bone)|(1u<<a[1].bone));
        const float *matrices[2]={bones[a[0].bone],bones[a[1].bone]},*points[2]={a[0].pos,a[1].pos};
        source_point_batch(matrices,points,point,2);
        const float *target=p->motion_flags&0x80 ? point[1]:p->small_spray_target;
        effect_paint_matrix(fx->spray.matrix,point[0],target);
        effect_advance(&fx->spray.frame,10);
    }
    for (unsigned i=0;i<32;i++) if (fx->tails[i].active) {
        SkateTail *t=&fx->tails[i];effect_advance(&t->frame,41);
        t->alpha-=0x1.111112p-4f;if (t->alpha<=0.f) t->active=0;
    }
    for(int i=0;i<24;i++)if(fx->dash_sparks[i].active){
        SkateBurst *b=fx->dash_sparks+i;b->frame+=1.f;if(b->frame>=12)b->active=0;
    }
    for(int i=0;i<16;i++)if(fx->water_sparks[i].active){
        SkateBurst *b=fx->water_sparks+i;if(++b->frame>=15)b->active=0;
    }
    /* 046a24 -> 046b44/046be4. The effect uses the separate water ray's
       contact point, travel yaw, and original speed/random scale law. */
    float water_scale=fabsf(in->speed)/0x1.da12f6p+0f;
    if(in->obstruction && water_scale>.5f)for(int i=0;i<16;i++)if(!fx->water_sparks[i].active){
        SkateBurst *b=fx->water_sparks+i;float scale[3];
        for(int k=0;k<2;k++){
            fx->water_random=fx->water_random*0x41c64e6du+0x3039u;
            scale[k]=water_scale+((fx->water_random>>16)&32767)/32768.f;
        }
        scale[2]=1;int32_t angle[3]={0,dc_yaw(p->yaw)+32768,0};
        source_trs(p->shallow_point,angle,scale,b->matrix);b->frame=1;b->active=1;break;
    }
    effect_ribbons(fx,in,effect_trail_enabled(p,in));
    unsigned mask=(in->flags&1) && (unsigned)in->grind_pose<6 ? SKATE_GRIND_MASK[p->pose_model][!!in->mirrored][in->grind_pose] : 0;
    for (int side=0;side<2;side++) {
        const float *foot=in->points[8+side],*moving=in->points[10+side];
        effect_grind(&fx->grind[side],!!(mask&(side ? 1 : 2)),in->speed,root,foot);
        SkateBurst *b=&fx->trick[side];float distance=effect_distance(moving,fx->previous_foot[side]);
        b->active=(in->flags&8) && !in->obstruction && distance>.5f;
        if(b->active){effect_segment(b->matrix,moving,fx->previous_foot[side],distance*.2f);effect_advance(&b->frame,12);}
        memcpy(fx->previous_foot[side],moving,12);
        effect_wall(fx,side,in->flags,in->obstruction,body,moving,in->external);
        if(effect_dash_contact(in,side))for(int j=0;j<24;j++)if(!fx->dash_sparks[j].active){
            /* Original one-shot snapshots the foot and travel orientation,
               then continues its 12-frame spark animation at that point. */
            float m[16],forward[3],right[3],up[3],unused[3];
            int rx=in->travel_angles[0],rz=in->travel_angles[2],yaw=in->travel_angles[1]+32768;
            rotated_basis(rx,rz,yaw,forward,up);rotated_basis(rx,rz,yaw+16384,right,unused);
            memcpy(m,root,sizeof m);
            for(int k=0;k<3;k++){m[k*4]=right[k];m[k*4+1]=up[k];m[k*4+2]=forward[k];}
            effect_grind(fx->dash_sparks+j,1,in->speed,m,moving);break;
        }
    }
}
static void effects_tick(Player *p){player_effects_tick(p,NULL);}
