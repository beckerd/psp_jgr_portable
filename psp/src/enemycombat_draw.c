#include "ge_submit.h"
#include "enemycombat.h"
#include "source_trs.h"
#include "enemyshot_transform.h"
#include "skyclip.h"
#include "charpose.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_enemy_hardpoints.inc"
#else
#include "generated/source_data/enemy_hardpoints.inc"
#endif
#include <string.h>
#include <math.h>
#include <pspgu.h>
#include <pspgum.h>
static void root_at(float *m,const float *p,int pitch,int yaw)
{
    const float zero[3]={0},one[3]={1,1,1};int32_t a[3]={0,yaw,0};
    memset(m,0,64);m[0]=m[5]=m[10]=m[15]=1;
    source_trs_apply(p,a,one,m,player_fsca_sin);
    a[0]=-pitch;a[1]=0;source_trs_apply(zero,a,one,m,player_fsca_sin);
}
/* 0fd4e4: three torso arcs and animated glows at the two hand anchors.
   Reuse one twelve-bone attachment traversal for all five effects. */
void enemycombat_draw_denki(EnemyCombatWorld *w,Gang *g,const float planes[6][4],int advance)
{
    if(g->mode!=6 || (g->flags&0x10120) || !w->models.h || !w->actors || !w->actors->h || w->actors->h->n_chars<=41)return;
    GangMember *m=g->m;Chars *actors=w->actors;const JgcChar *ch=actors->chars+41;
    if(ch->nbones!=12)return;float root[16],matrices[12][16],point[3][3];CharPose pose[12];gang_member_matrix(g,m,root);
    for(unsigned b=0;b<12;b++)chars_sample_pose(actors,ch,ch->motion0+(m->anim<0?0:m->anim),m->frame,b,pose+b);
    chars_attachment_matrices(actors,ch,pose,root,matrices);
    for(int i=0;i<3;i++){
        unsigned h=14+i;const float *a=matrices[ENEMY_HARDPOINTS[h].bone],*p=ENEMY_HARDPOINTS[h].pos;
        for(int j=0;j<3;j++)point[i][j]=a[j*4]*p[0]+a[j*4+1]*p[1]+a[j*4+2]*p[2]+a[j*4+3];
    }
    for(int i=0;i<3;i++)if(m->denki.arc[i]>0){
        root_at(root,point[2],0,m->denki.yaw[i]);
        for(int pass=0;pass<2;pass++)chars_draw_material_pass_clipped(&w->models,13+i*10+m->denki.texture,root,-1,0,pass,planes);
    }
    if(g->state!=5)for(int i=0;i<2;i++){
        root_at(root,point[i],0,0);root[0]=root[5]=root[10]=m->denki.scale;
        for(int pass=0;pass<2;pass++)chars_draw_material_pass_clipped(&w->models,43,root,0,m->denki.frame,pass,planes);
    }
    if(advance)m->denki.texture=(m->denki.texture+1)%10;
}
/* 07bcd8 substitutes the authored disintegrating body during anim 20. */
void enemycombat_draw_bomb(EnemyCombatWorld *w,const Gang *g,const GangMember *m,const float planes[6][4])
{
    if(!w->models.h || w->models.h->n_chars<=44 || (m->flags&0x120))return;
    float root[16];gang_member_matrix(g,m,root);
    for(int pass=0;pass<2;pass++)chars_draw_material_pass_clipped(&w->models,44,root,0,m->frame,pass,planes);
}
int enemycombat_draw_strong(EnemyCombatWorld *w,const Gang *g,const GangMember *m,const float root[16],const float planes[6][4])
{
    if(!w->burning_models.h)return 0;
    int body=gang_kairiki_body(m);
    if(body>=0)for(int pass=0;pass<2;pass++)chars_draw_material_pass_clipped(&w->burning_models,body,root,0,m->frame,pass,planes);
    if(m->anim==20 || m->anim==21) {
        if(m->police.pose_frame>=0)for(int pass=0;pass<2;pass++)chars_draw_material_pass_clipped(&w->burning_models,m->anim==20?2:4,root,0,m->police.pose_frame,pass,planes);
        if(m->police.gas_pose_frame>=0)for(int pass=0;pass<2;pass++)chars_draw_material_pass_clipped(&w->burning_models,3,root,0,m->police.gas_pose_frame,pass,planes);
    }
    return body>=0;
}
void enemycombat_draw_player_fire(EnemyCombatWorld *w,const float view[16],const float planes[6][4])
{
    const PlayerBurn *b=&w->player_burn;
    if(b->remaining>0 && b->scale>0 && w->flame_models.h) {
        float root[16]={0};
        /* 043e74: billboard, Z rotation 0x8000, uniform positive scale.
           eventflare contains the same two meshes/eight images as PLAY's
           effect slots 20..35. Reflect the original camera's Y/Z axes. */
        for(int j=0;j<3;j++) {
            for(int k=0;k<3;k++)root[j*4+k]=view[k*4+j]*b->scale*(k==1?1.f:-1.f);
            root[j*4+3]=b->pos[j];
        }
        root[15]=1;
        /* 234fda uses the spark's 23471e strip path. Keep both faces,
           including the mirrored second mesh. The EVENT bank's material
           culling would otherwise hide half of this animation. Scope the
           override to this attachment; other owners retain their flags. */
        Chars *c=&w->flame_models;
        const JgcBone *bone=c->bones+c->chars[b->image].bone0;
        if(bone->nbatch!=1)return;
        JgcBatch *batch=c->batch+bone->batch0;unsigned short flags=batch->flags;
        batch->flags|=2;
        for(int pass=0;pass<2;pass++)chars_draw_material_pass_clipped(&w->flame_models,b->image,root,-1,0,pass,planes);
        batch->flags=flags;
    }
}
static void fire_draw(EnemyCombatWorld *w,const float view[16],const float planes[6][4])
{
    if(!w->fires || !w->burning_models.h)return;
    for(int n=0;n<ENEMY_FIRE_CAPACITY;n++) {
        EnemyFire *s=w->fires+n;if(!s->token || (s->flags&2))continue;
        for(int i=s->first;i<s->last;i++) {
            const float *p=s->point[i];float root[16]={0};
            float z=-(view[8]*p[0]+view[9]*p[1]+view[10]*p[2]+view[11]);if(z>500)continue;
            int tip=i==s->last-1;
            float scale=(s->variant?.075f:.05f)*(i+1)*(tip?1.f:s->size[i]);
            for(int j=0;j<3;j++){for(int k=0;k<3;k++)root[j*4+k]=-view[k*4+j]*scale;root[j*4+3]=p[j];}root[15]=1;
            int right=s->direction[i][0]*view[0]+s->direction[i][2]*view[2]>0;
            int mesh=tip?4+((s->flags&0x18)?0:2)+right:s->mesh[i]+right;
            int phase=tip?s->tip_image:(s->image-i)&7;
            const JgcChar *image=w->burning_models.chars+5+phase;
            unsigned texture=w->burning_models.batch[w->burning_models.bones[image->bone0].batch0].tex;
            unsigned short map[10];for(int t=0;t<10;t++)map[t]=texture;
            chars_draw_material_alpha_clipped(&w->burning_models,5+mesh,root,s->alpha[i],map,planes);
        }
    }
    if(w->flame_models.h)for(int i=0;i<ENEMY_BURN_CAPACITY;i++) {
        const EnemyBurn *b=w->burns+i;if(!b->life || b->scale<=0)continue;
        float d2=0,root[16]={0};for(int j=0;j<3;j++){float d=b->pos[j]-w->gang.target[j];d2+=d*d;}
        if(d2>=500.f*500.f)continue;
        for(int j=0;j<3;j++){for(int k=0;k<3;k++)root[j*4+k]=-view[k*4+j]*b->scale;root[j*4+3]=b->pos[j];}root[15]=1;
        for(int pass=0;pass<2;pass++)chars_draw_material_pass_clipped(&w->flame_models,b->mesh*8+b->image,root,-1,0,pass,planes);
    }
}
void enemycombat_draw_hands(EnemyCombatWorld *w,const GangMember *member,const float root[16],const CharPose *pose,const float view[16],const float planes[6][4])
{
    const EnemyHandFlames *f=&member->police.flames;Chars *c=w->actors;
    if(!f->ready || f->scale<=0 || !w->flame_models.h || !c || !c->h || c->h->n_chars<=32 || c->chars[32].nbones!=12)return;
    float matrices[12][16];chars_attachment_matrices(c,c->chars+32,pose,root,matrices);
    for(int i=0;i<2;i++) {
        const float *a=matrices[9+i*2];float p[3],distance=0,m[16]={0};
        for(int j=0;j<3;j++)p[j]=a[j*4+3]-3.27f*a[j*4+1]+2.77f*a[j*4+2];
        for(int j=0;j<3;j++){float v=view[j*4]*p[0]+view[j*4+1]*p[1]+view[j*4+2]*p[2]+view[j*4+3];distance+=v*v;}
        if(distance>=500.f*500.f)continue;
        /* 28094c billboard and scale (s,-s,-s), as in EVENT's flare.
           The PSP view already reflects the original camera's Y/Z. */
        for(int j=0;j<3;j++){for(int k=0;k<3;k++)m[j*4+k]=-view[k*4+j]*f->scale;m[j*4+3]=p[j];}m[15]=1;
        for(int pass=0;pass<2;pass++)chars_draw_material_pass_clipped(&w->flame_models,f->mesh[i]*8+f->frame[i],m,-1,0,pass,planes);
    }
}
static void taban_quad(const JgsVert q[4],const float planes[6][4],int clip)
{
    static const unsigned char order[2][3]={{0,1,2},{2,1,3}};
    JgsVert clipped[54];int count=0;
    for(int i=0;i<2;i++) {
        JgsVert t[3]={q[order[i][0]],q[order[i][1]],q[order[i][2]]};
        if(clip)count+=skyclip_triangle(t,planes,clipped+count);
        else {memcpy(clipped+count,t,sizeof t);count+=3;}
    }
    if(!count)return;
    void *v=sceGuGetMemory(count*sizeof *clipped);memcpy(v,clipped,count*sizeof *clipped);
    ge_submit_aux(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,0,v);
}
/* 09d010 / 1c86b4: untextured, additive red strip, .1-unit half-width.
   The unlocked sight continues fifty units beyond the sampled target and
   fades to zero alpha. Clip it before submitting to the GE: a gun behind
   the camera must never send an unbounded screen-space triangle. */
void enemycombat_draw_sniper(EnemyCombatWorld *w,const Gang *g,const float view[16],const float planes[6][4])
{
    if(g->mode!=5 || g->state!=3 || (g->flags&0x120) || !w->gang.anchor || !w->gang.history_valid)return;
    float from[3];if(!w->gang.anchor(w->gang.context,g,10,from))return;
    const float *to=(g->flags&0x20000)?w->gang.target:w->gang.history[g->fire_cursor];
    float delta[3],length=0;
    for(int k=0;k<3;k++){delta[k]=to[k]-from[k];length+=delta[k]*delta[k];}
    if(!isfinite(length) || length<.0001f)return;
    int locked=(g->flags&0x20000)!=0;float end[3];length=sqrtf(length);
    for(int k=0;k<3;k++)end[k]=to[k]+delta[k]*(50.f/length);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGumUpdateMatrix();
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_DEPTH_TEST);sceGuDepthMask(GU_TRUE);sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_FIX,0,0xffffff);
    for(int segment=0;segment<(locked?1:2);segment++) {
        JgsVert q[4];
        for(int edge=0;edge<2;edge++)for(int side=0;side<2;side++) {
            const float *point=segment?(edge?end:to):(edge?to:from);
            float d=(side?-1.f:1.f)*.1f;
            q[edge*2+side]=(JgsVert){0,0,(segment && edge)?0x000000ff:0xff0000ff,
                point[0]+view[4]*d,point[1]+view[5]*d,point[2]+view[6]*d};
        }
        taban_quad(q,planes,1);
    }
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);sceGuEnable(GU_CULL_FACE);sceGuFrontFace(GU_CW);
}
/* 0fb6ec: two crossed .3-unit core strips in the lower texture half,
   then the additive swept trail in the upper half. Bound every submission
   to the PSP frustum, including segments crossing the near plane. */
void enemycombat_draw_taban(EnemyCombatWorld *w,const Gang *g,const float view[16],const float planes[6][4])
{
    const TabanAttack *a=&g->taban;
    if(g->mode!=9 || (g->flags&0x120) || g->state<7 || g->state>9 || !a->ready || a->extent<2 || w->taban_texture.n!=1)return;
    const Tex2d *t=w->taban_texture.t;float root[16];gang_member_matrix(g,g->m,root);
    float right[3]={view[0],0,view[2]},n=hypotf(right[0],right[2]);
    if(n>0){right[0]/=n;right[2]/=n;}
    float current[20][3],previous[20][3];
    for(int i=0;i<a->extent;i++)for(int j=0;j<3;j++) {
        const float *r=root+j*4;
        current[i][j]=r[0]*a->point[i][0]+r[1]*a->point[i][1]+r[2]*a->point[i][2]+r[3];
        previous[i][j]=r[0]*a->previous[i][0]+r[1]*a->previous[i][1]+r[2]*a->previous[i][2]+r[3];
    }
    /* A chain wholly inside the frustum needs no per-triangle clipping.
       Crossed core strips extend at most .15 along each world axis. */
    float lo[3],hi[3];memcpy(lo,current[0],12);memcpy(hi,current[0],12);
    for(int i=0;i<a->extent;i++)for(int j=0;j<3;j++) {
        if(!isfinite(current[i][j]) || !isfinite(previous[i][j]))return;
        lo[j]=fminf(lo[j],fminf(current[i][j],previous[i][j])-.15f);
        hi[j]=fmaxf(hi[j],fmaxf(current[i][j],previous[i][j])+.15f);
    }
    int clip=0;
    for(int p=0;p<6;p++) {
        float low=planes[p][3],high=low;
        for(int j=0;j<3;j++){float n=planes[p][j];low+=n*(n>=0?lo[j]:hi[j]);high+=n*(n>=0?hi[j]:lo[j]);}
        if(high<0)return;if(low<0)clip=1;
    }
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGumUpdateMatrix();
    sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_DEPTH_TEST);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_ALPHA_TEST);
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);
    sceGuTexMode(GU_PSM_4444,0,0,0);sceGuTexImage(0,t->w,t->h,t->w,t->px);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    sceGuTexScale(1,1);sceGuTexOffset(0,0);
    for(int pass=0;pass<3;pass++) {
        if(pass==2){sceGuDepthMask(GU_TRUE);sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_FIX,0,0xffffff);}
        for(int i=1;i<a->extent;i++) {
            JgsVert q[4];
            for(int end=0;end<2;end++)for(int side=0;side<2;side++) {
                int k=i-1+end;JgsVert *v=q+end*2+side;
                v->u=end;v->v=pass==2?side*.5f:.5f+side*.5f;v->color=0xffffffff;
                const float *p=pass==2 && !side?previous[k]:current[k];
                float off=(side?1.f:-1.f)*.15f;
                v->x=p[0];v->y=p[1];v->z=p[2];
                if(pass<2){v->x+=right[0]*off;v->z+=right[2]*off;v->y+=(pass?-off:off);}
                else {unsigned alpha=k?(unsigned)(255.f*a->alpha[k]*a->alpha[k]):0;v->color=0xffffff|(alpha<<24);}
            }
            taban_quad(q,planes,clip);
        }
    }
    sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);sceGuDisable(GU_TEXTURE_2D);sceGuEnable(GU_CULL_FACE);sceGuFrontFace(GU_CW);
}
/* 0a9e48 uses thirty history samples and crossed 1.5-unit strips,
   rotated around camera yaw. One GE strip per side keeps the trail cheap. */
static void draw_trail(Chars *c,const EnemyMissile *s,const float *right)
{
    const JgcChar *ch=c->chars+9+s->variant;const JgcBone *bone=c->bones+ch->bone0;
    while(!bone->nbatch && bone<c->bones+ch->bone0+ch->nbones-1)bone++;
    if(!bone->nbatch || !chars_bind_material_texture(c,c->batch[bone->batch0].tex))return;
    float x=right[0],z=right[2],length=sqrtf(x*x+z*z);if(length>0){x/=length;z/=length;}
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGumUpdateMatrix();
    sceGuEnable(GU_TEXTURE_2D);sceGuDisable(GU_CULL_FACE);sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_DEPTH_TEST);sceGuDepthMask(GU_TRUE);sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    typedef struct {float u,v;unsigned color;float x,y,z;} Vertex;
    for(int axis=0;axis<2;axis++) {
        Vertex *v=sceGuGetMemory(60*sizeof *v);
        for(int i=0;i<30;i++)for(int edge=0;edge<2;edge++) {
            const float *p=s->trail[(s->trail_at+i+1)%30];
            float width=(i+1)*(1.5f/30.f)*(edge?1.f:-1.f);
            v[i*2+edge]=(Vertex){edge,(i/30.f)+s->texture_v,0xffffffff,
                p[0]+width*x,p[1]+width*(axis?1.f:-1.f),p[2]+width*z};
        }
        ge_submit_aux(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,60,0,v);
    }
    sceGuDepthMask(GU_FALSE);
}
static void warning_draw(EnemyCombatWorld *w,const float *view)
{
    const EnemyWarning *b=&w->warning;Chars *c=&w->warning_models;
    if(!c->h || b->scale<=0)return;
    float head[3];for(int r=0;r<3;r++)head[r]=view[r*4]*b->head[0]+view[r*4+1]*b->head[1]+view[r*4+2]*b->head[2]+view[r*4+3];
    if(head[2]>=-5 || head[0]*head[0]+head[1]*head[1]+head[2]*head[2]>500.f*500.f)return;
    ScePspFMatrix4 matrix;float *v=(float *)&matrix;
    memset(v,0,64);v[0]=v[5]=v[10]=v[15]=1;sceGuSetMatrix(GU_VIEW,&matrix);
    /* 044988's source camera helpers also reflect Y. PSP view space
       already has positive Y upwards: retain X reflection only, placing
       the dot below the bar and the tail towards the player's head. */
    float root[16]={-b->scale,0,0,head[0],0,b->scale,0,head[1],0,0,1,head[2]+2,0,0,0,1};
    CharPose rest[3];const JgcChar *ch=c->chars;
    for(unsigned i=0;i<ch->nbones && i<3;i++)chars_sample_pose(c,ch,b->kind?ch->motion0:-1,b->frame,i,rest+i);
    if(ch->nbones==3)chars_draw_material(c,0,root,rest,0,0,1,NULL);
    for(int r=0;r<4;r++)for(int col=0;col<4;col++)v[col*4+r]=view[r*4+col];
    sceGuSetMatrix(GU_VIEW,&matrix);
}
/* 0fe120 rotates the private ten-entry texture list each simulation tick;
   0fe2d8 adds three fragments at the player's captured contact position. */
static void draw_wave(Chars *c,const EnemyWave *s)
{
    if(c->h->n_chars<44 || c->h->n_tex>32)return;
    unsigned short map[32],ids[10];
    for(unsigned i=0;i<c->h->n_tex;i++)map[i]=i;
    for(unsigned i=0;i<10;i++){
        const JgcChar *ch=c->chars+13+i;const JgcBone *bone=c->bones+ch->bone0;
        ids[i]=c->batch[bone->batch0].tex;
    }
    unsigned phase=(unsigned)s->age%10;
    for(unsigned i=0;i<10;i++)map[ids[i]]=ids[(i+phase)%10];
    float root[16];root_at(root,s->pos,s->pitch,s->yaw);
    chars_draw_material(c,s->state==2?12:11,root,0,0,s->frame,1,map);
    if(s->state==2){
        root_at(root,s->hit_pos,0,0);
        for(unsigned i=0;i<3;i++)chars_draw_material(c,13+i*10+phase,root,0,-1,0,1,0);
    }
}
void enemycombat_draw(EnemyCombatWorld *w,const float *view,const float planes[6][4])
{
    warning_draw(w,view);fire_draw(w,view,planes);
    /* 07a4b8 rotates the cloud around camera yaw (not camera pitch). */
    w->gas_camera_yaw=player_bam_atan2(-view[2],view[0]);
    if(w->gas_models.h)for(int i=0;i<ENEMY_GAS_CAPACITY;i++) {
        const EnemyGas *s=w->gases.gas+i;if(!s->active || s->state!=3)continue;
        /* 07a588: the 20-unit cloud sphere is a 500-unit 0c9d50 cut. */
        if(-(view[8]*s->pos[0]+view[9]*s->pos[1]+view[10]*s->pos[2]+view[11])>500.f)continue;
        float matrix[16];root_at(matrix,s->pos,0,w->gas_camera_yaw);
        int model=s->skin*4+s->variant*2;
        if(chars_actor_visible(&w->gas_models,model,matrix))
            chars_draw_material(&w->gas_models,model,matrix,NULL,0,s->frame,1,NULL);
        if(chars_actor_visible(&w->gas_models,model+1,matrix))
            chars_draw_material(&w->gas_models,model+1,matrix,NULL,0,s->frame,s->alpha,NULL);
    }
    Chars *c=&w->models;if(!c->h)return;
    float root[16];
    for(unsigned i=0;i<ENEMY_SHOT_CAPACITY;i++) {
        const EnemyShot *s=w->shots.shots+i;
        if(!s->active || !s->visible || s->state!=5)continue;
        enemyshot_transform(root,s,player_fsca_sin);chars_draw_material(c,0,root,0,0,s->frame,1,0);
    }
    for(unsigned i=0;i<ENEMY_MISSILE_CAPACITY;i++) {
        const EnemyMissile *s=w->missiles.missiles+i;if(!s->active)continue;
        draw_trail(c,s,view);
        int first=1+s->variant*3;
        if(s->state<3) {
            root_at(root,s->pos,s->angles[0],s->angles[1]);chars_draw_material(c,first,root,0,0,s->frame,1,0);
        }
        root_at(root,s->origin,s->angles[0],s->angles[1]);
        if(s->flash_frame<c->motions[c->chars[first+1].motion0].nframes)
            chars_draw_material(c,first+1,root,0,0,s->flash_frame,1,0);
        if(s->smoke_frame<c->motions[c->chars[first+2].motion0].nframes)
            chars_draw_material(c,first+2,root,0,0,s->smoke_frame,1+s->alpha,0);
    }
    for(unsigned i=0;i<16;i++)if(w->blasts[i].active) {
        int age=w->blasts[i].age;root_at(root,w->blasts[i].pos,0,w->blasts[i].yaw);
        if(age>=0 && age<(int)c->motions[c->chars[7].motion0].nframes)
            chars_draw_material(c,7,root,0,0,age,age>4?1.f-(age-4)*.1f:1,0);
        if(age>=0)chars_draw_material(c,8,root,0,0,age,age>49?1.f-(age-49)*0x1.111112p-5f:1,0);
    }
    for(unsigned i=0;i<ENEMY_WAVE_CAPACITY;i++) {
        const EnemyWave *s=w->waves.waves+i;if(!s->active)continue;
        draw_wave(c,s);
    }
}
