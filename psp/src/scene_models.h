#ifndef JGR_SCENE_MODELS_H
#define JGR_SCENE_MODELS_H
/* The EVENT actor's skeleton and texture-list identity are independent of
   its raw motion pointer. Bind each source mesh once for the entire scene. */
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_scene_models.inc"
#else
#include "generated/source_data/scene_models.inc"
#endif
static int scene_load_bank(Chars *bank,const char *file)
{
    int err=chars_load(bank,file);
    if(err){chars_free(bank);}
    return err;
}
static int scene_load_models(ScenePlay *p)
{
    unsigned n;const SceneModelBinding *b=scene_model_bindings(p->vm.id,&n);
    if(!n)return 0;
    p->models=calloc(n,sizeof *p->models);if(!p->models)return -1;p->model_count=n;
    for(unsigned i=0;i<n;i++) {
        SceneModel *m=p->models+i;m->binding=b+i;
        char path[80];const char *file=b[i].file;
        int rival=p->vm.mission_context && b[i].type>=18 && b[i].type<=20;
        if(rival){snprintf(path,sizeof path,"eventplay%02d.jgc",b[i].type);file=path;}
        if(scene_load_bank(&m->bank,file))return -1;
        m->loaded=1;
        if(rival) {
            snprintf(path,sizeof path,"eventplay%02d_mouth.jgc",b[i].type);
            if(scene_load_bank(&m->mouth,path))return -1;
            m->mouth_loaded=1;
        }
    }
    return 0;
}
static SceneModel *scene_find_model(ScenePlay *p,int type,uint32_t root,uint32_t texlist)
{
    for(unsigned i=0;i<p->model_count;i++) {
        SceneModel *m=p->models+i;const SceneModelBinding *b=m->binding;
        if(b->type==type && (type>=0 || (b->root==root && b->texlist==texlist)))return m;
    }
    return NULL;
}
static int scene_draw_model(ScenePlay *p,SceneModel *model,const SceneActor *a,uint32_t njs,int cel,const SceneTypedSound *voice)
{
    if(!model || !model->loaded)return 0;
    const Chars *c=&model->bank;const JgcChar *ch=c->chars;
    if(ch->nbones>64)return 0;
    CharPose poses[64];float matrix[16];
    for(unsigned bone=0;bone<ch->nbones;bone++) {
        chars_sample_pose(c,ch,-1,0,bone,poses+bone);
        if(njs) {if(!scene_sample_njs(&p->vm,njs,a->frame,bone,cel,poses+bone))return 0;}
        else if(a->raw_motion && !scene_sample_cnk(&p->vm,a->raw_motion,a->frame,bone,poses+bone))return 0;
    }
    player_scene_matrix(a->pos,a->angle,a->scale,matrix);
    /* 0cc29c selects 5/6 for the SECOND (shadow) draw. 0cc7c8's
       first body call is always mode 0; using the shadow mode here made
       all alpha-flagged police surfaces depthless and their hulls black. */
    if(model->binding->type>=0)chars_draw_event_actor(&model->bank,0,matrix,poses);
    else if(cel || model->mouth_loaded)chars_draw_one_pose(&model->bank,0,matrix,poses,-1,0,0);
    else chars_draw_material(&model->bank,0,matrix,poses,0,0,1,NULL);
    if(model->mouth_loaded && voice) {
        float mouth[16];if(!scene_mouth_matrix(c,0,poses,matrix,mouth,player_fsca_sin))return 0;
        chars_draw_one_xf(&model->mouth,0,mouth,voice->mouth_mode,voice->mouth_frame);
    }
    return 1;
}
static int scene_draw_models(ScenePlay *p)
{
    for(unsigned i=0;i<30;i++) {
        SceneActor *a=p->vm.actors+i;
        if(a->active) {
            SceneModel *m=scene_find_model(p,a->type,0,0);
            if(!scene_draw_model(p,m,a,0,m && (m->binding->flags&2),p->vm.typed_sound+i))return 0;
        }
        SceneObject *o=p->vm.objects+i;a=&o->transform;
        if(a->active && !scene_draw_model(p,scene_find_model(p,-1,o->root,o->texlist),a,o->njs_motion,(o->flags&2)!=0,NULL))return 0;
    }
    return 1;
}
static int scene_attachment(ScenePlay *p,int slot,unsigned bone,float out[3])
{
    if((unsigned)slot>=30)return 0;
    const SceneActor *a=p->vm.actors+slot;
    SceneModel *model=scene_find_model(p,a->type,0,0);
    if(!a->active || !model)return 0;
    const Chars *c=&model->bank;const JgcChar *ch=c->chars;if(bone>=ch->nbones || ch->nbones>64)return 0;
    float root[16],mat[64][16];player_scene_matrix(a->pos,a->angle,a->scale,root);
    for(unsigned i=0;i<=bone;i++) {
        CharPose pose;chars_sample_pose(c,ch,-1,0,i,&pose);
        if(a->raw_motion && !scene_sample_cnk(&p->vm,a->raw_motion,a->frame,i,&pose))return 0;
        int parent=c->bones[ch->bone0+i].parent-(int)ch->bone0;if(parent>=(int)i)return 0;
        memcpy(mat[i],parent<0?root:mat[parent],64);
        float pos[3],scale[3];int32_t angle[3];
        for(unsigned j=0;j<3;j++){pos[j]=pose.ev&1?0:pose.pos[j];angle[j]=pose.ev&2?0:pose.ang[j];scale[j]=pose.ev&4?1:pose.scl[j];}
        source_trs_apply(pos,angle,scale,mat[i],player_fsca_sin);
    }
    /* 1d0090/1d00ac are the two original nozzle offsets, bones 9/11. */
    for(unsigned j=0;j<3;j++)out[j]=mat[bone][j*4+3]-3.27f*mat[bone][j*4+1]+2.77f*mat[bone][j*4+2];
    return 1;
}
static void scene_flare(ScenePlay *p,unsigned model,const float pos[3])
{
    const float *f=p->vm.camera.forward;
    float len=sqrtf(f[0]*f[0]+f[2]*f[2]);if(len<.0001f)return;
    float zlen=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);if(zlen<.0001f)return;
    float x[3]={f[2]/len,0,-f[0]/len},z[3]={f[0]/zlen,f[1]/zlen,f[2]/zlen};
    float y[3]={z[1]*x[2],z[2]*x[0]-z[0]*x[2],-z[1]*x[0]},m[16]={0};
    for(unsigned j=0;j<3;j++){m[j*4]=x[j];m[j*4+1]=-y[j];m[j*4+2]=z[j];m[j*4+3]=pos[j];}m[15]=1;
    chars_draw_material(&p->flare,model,m,NULL,-1,0,1,NULL);
}
static void scene_draw_aux(ScenePlay *p)
{
    for(unsigned i=0;i<32;i++) {
        const struct SceneAuxEffect *e=p->vm.aux+i;if(!e->active)continue;
        if(e->kind==0x5b && p->blast_loaded) {
            int32_t angle[3]={0,player_bam_atan2(p->vm.camera.forward[0],p->vm.camera.forward[2]),0};
            const float scale[3]={1,1,1};float m[16];player_scene_matrix(e->pos[0],angle,scale,m);
            unsigned variant=p->vm.id==73?2:0;
            const JgcChar *ch=p->blast.chars+variant;
            float last=p->blast.motions[ch->motion0].nframes-1.f;
            if(e->frame<last-2)chars_draw_material(&p->blast,variant,m,NULL,0,e->frame,1,NULL);
            float alpha=e->frame>=60?1.f-(e->frame-60)/59.f:1.f;if(alpha<0)alpha=0;
            chars_draw_material(&p->blast,variant+1,m,NULL,0,e->frame,alpha,NULL);
        } else if(p->flare_loaded && e->kind==0x5d) {
            /* PSP flame strip follows both original dense endpoint tracks.
               The Dreamcast's procedural ribbon tessellation is separate. */
            for(unsigned j=0;j<16;j++){float pos[3];for(unsigned k=0;k<3;k++)pos[k]=e->pos[0][k]+(e->pos[1][k]-e->pos[0][k])*(j/15.f);scene_flare(p,((unsigned)e->age+j)&7,pos);}
        } else if(p->flare_loaded && e->slot>=0) {
            for(unsigned bone=9;bone<=11;bone+=2){float pos[3];if(scene_attachment(p,e->slot,bone,pos))scene_flare(p,e->mesh*8+e->frame,pos);}
        } else if(p->flare_loaded)scene_flare(p,e->mesh*8+e->frame,e->pos[0]);
    }
}
#endif
