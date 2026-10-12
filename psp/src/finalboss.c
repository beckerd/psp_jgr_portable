#include "finalboss.h"
#include "player.h"
#include <string.h>
#include <math.h>
unsigned finalboss_random15(void *state)
{
    /* 013a3c. Caller owns the shared random stream, not the animation. */
    uint32_t *s=state;*s=*s*0x41c64e6du+0x3039u;return (*s>>16)&0x7fff;
}
void finalboss_init(FinalBoss *s,const Chars *bank)
{
    memset(s,0,sizeof *s);s->motion=s->old_motion=2;s->blend=1.f;
    s->voice_ticks=210;
    for(unsigned i=0;i<20;i++)chars_sample_pose(bank,bank->chars,bank->chars[0].motion0+2,0,i,s->pose+i);
    memcpy(s->entry,s->pose,sizeof s->pose);
}
int finalboss_voice(FinalBoss *s,int phase,int playing,unsigned (*random15)(void *),void *ctx)
{
    if(s->voice_active){s->voice_active=!!playing;return -1;}
    if(s->voice_ticks-->0)return -1;
    s->voice_ticks=(int)(210.f-(float)phase*21.f);
    float r=(float)random15(ctx)/32768.f;
    int ref=phase==0 ? (r<.7f ? 1:0):phase==1 ? (r<.3f ? 1:0):
            phase==2 ? (r<.5f ? 0:3):phase==3 ? (r<.7f ? 3:5):(r<.3f ? 3:5);
    s->voice_active=1;
    return 0x170000+ref;
}
void finalboss_animation(FinalBoss *s,const Chars *bank,unsigned (*random15)(void *),void *ctx)
{
    const JgcChar *ch=bank->chars;
    if((unsigned)s->motion>=5 || ch->nbones!=20 || ch->nmotions!=5)return;
    if(s->motion!=s->old_motion) {
        s->old_motion=s->motion;s->frame=s->blend=0.f;
        memcpy(s->entry,s->pose,sizeof s->entry); /* 0bdab0 freezes displayed pose */
    }
    int mi=ch->motion0+s->motion;
    if(s->blend<1.f) {
        s->blend+=.2f;
        for(unsigned i=0;i<20;i++) {
            CharPose next;chars_sample_pose(bank,ch,mi,(float)(int)s->frame,i,&next);
            if(s->blend>1.f)s->pose[i]=next;
            else { s->pose[i]=s->entry[i];chars_blend_pose(s->pose+i,&next,s->blend); }
        }
        return; /* original does not advance the clip during the blend */
    }
    for(unsigned i=0;i<20;i++)chars_sample_pose(bank,ch,mi,(float)(int)s->frame,i,s->pose+i);
    s->frame+=1.f;
    float end=(float)(bank->motions[mi].nframes-1);
    if(s->frame>end) {
        s->frame=end;
        if(s->motion==2) {
            /* 0ecb7c: strict random/32768 < .1, NOT a 50/50 choice. */
            if(random15 && (float)random15(ctx)/32768.f<.1f)s->motion=3;
            else s->frame=0.f;
        } else if(s->motion==4)s->frame=0.f;
        else s->motion=2; /* callbacks 0ecb6c, 0ecb74, 0ecbc0 */
    }
}
void finalboss_clocks(FinalBoss *s,const Chars *bank,int phase,int cinematic)
{
    if(phase<5) {
        if(++s->texture_clock>10){s->texture_clock=0;s->texture_rotation=(s->texture_rotation+1)%16;}
        if(!cinematic || phase<1) {
            if(++s->orbit>=bank->motions[bank->chars[3].motion0].nframes)s->orbit=0;
        } else s->orbit=0;
        if(++s->left>=bank->motions[bank->chars[1].motion0].nframes)s->left=0;
        if(++s->right>=bank->motions[bank->chars[1].motion0+1].nframes)s->right=0;
    } else {
        s->motion=4;
        if(++s->texture_clock>30){s->texture_clock=0;s->final_rotation^=1;}
    }
}
static int attachments(const Chars *bank,unsigned model,float frame,float collapse,const float *world,
                       const unsigned *bones,unsigned count,float *out)
{
    if(!bank || !bank->h || model>=bank->h->n_chars || !world || !count || !isfinite(frame))return -1;
    for(unsigned i=0;i<16;i++)if(!isfinite(world[i]))return -1;
    const JgcChar *ch=bank->chars+model;float mats[64][16];unsigned last=0;
    if(!ch->nmotions)return -1;
    for(unsigned i=0;i<count;i++) {
        if(bones[i]>=ch->nbones || bones[i]>=64)return -1;
        if(bones[i]>last)last=bones[i];
    }
    for(unsigned i=0;i<=last;i++) {
        CharPose p;float local[16];
        chars_sample_pose(bank,ch,ch->motion0,frame,i,&p);
        if(collapse>=0 && ch->nmotions>1)chars_overlay_pose(bank,ch,ch->motion0+1,collapse,i,1.f,&p);
        if(p.ev&1)memset(p.pos,0,sizeof p.pos);
        if(p.ev&2)memset(p.ang,0,sizeof p.ang);
        if(p.ev&4)p.scl[0]=p.scl[1]=p.scl[2]=1.f;
        int32_t angles[3]={p.ang[0],p.ang[1],p.ang[2]};
        player_scene_matrix(p.pos,angles,p.scl,local);
        int parent=bank->bones[ch->bone0+i].parent-(int)ch->bone0;
        if(parent>=(int)i)return -1;
        const float *a=parent>=0 ? mats[parent]:world;
        for(int r=0;r<4;r++)for(int c=0;c<4;c++) {
            mats[i][r*4+c]=0.f;
            for(int k=0;k<4;k++)mats[i][r*4+c]+=a[r*4+k]*local[k*4+c];
        }
    }
    for(unsigned i=0;i<count;i++)memcpy(out+i*16,mats[bones[i]],64);
    return 0;
}
void finalboss_attachments(const Chars *bank,float frame,const float *world,float out[2][16])
{ static const unsigned bones[2]={4,7};attachments(bank,3,frame,-1,world,bones,2,out[0]); }
int finalboss_central_attachment(const Chars *bank,float frame,const float *world,float out[16])
{ static const unsigned bone=63;return attachments(bank,4,frame,-1,world,&bone,1,out); }
int finalboss_platform_attachment(const Chars *bank,unsigned model,float frame,float collapse,const float *world,float out[16])
{ static const unsigned bone=1;return model<4 ? attachments(bank,model,frame,collapse,world,&bone,1,out):-1; }
