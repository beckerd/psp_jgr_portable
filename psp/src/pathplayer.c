#include "pathplayer.h"
#include "player_body.h"
#include "traffic_contact.h"
int pathplayer_sample(PathPlayerContacts *s,const PathWorld *w,const Player *p)
{
    if(!s || !w || !p)return -1;
    s->contacts=s->limbs=s->handle=s->group=0;s->speed=0;s->mode=-1;s->yaw=0;
    if(!w->count){s->valid=0;return 0;}
    /* No body query occurs below: consume the shared immutable geometry
       directly, including spheres already prepared by street contacts. */
    const PlayerBodyGeometry *body=player_body_geometry(p,0xfffff);
    if(!body){s->valid=0;return -2;}
    const float (*m)[16]=body->matrices,(*spheres)[4]=body->spheres;
    float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
    PathBodyHit nearest={0};unsigned nearest_order=UINT32_MAX,nearest_bone=20;
    for(unsigned bone=0;bone<20;bone++){
        const float *sphere=spheres[bone];
        if(sphere[3]>0)for(int r=0;r<3;r++) {
            float lo=sphere[r]-2.f*sphere[3],hi=sphere[r]+2.f*sphere[3];
            if(lo<low[r])low[r]=lo;
            if(hi>high[r])high[r]=hi;
        }
    }
    unsigned handles[PATHWORLD_CAPACITY];
    unsigned candidates=pathworld_body_candidates(w,low,high,handles);
    for(unsigned bone=0;bone<20;bone++){
        const PlayerBodyNode *node=&PLAYER_BODY[p->pose_model][bone];
        /* All 320 source descriptor +48 contact anchors are zero (asserted
           by the exporter). Sphere offsets can be nonzero and are separate. */
        float center[3]={m[bone][3],m[bone][7],m[bone][11]};
        const float *sphere=spheres[bone];
        if(!s->valid)memcpy(s->previous[bone],center,12);
        else for(int k=0;k<3;k++)s->previous[bone][k]+=p->portal_translation[k];
        if(sphere[3]>0){
            PathBodyHit hits[PATHWORLD_CAPACITY];
            int n=pathworld_candidate_contacts(w,handles,candidates,sphere,center,s->previous[bone],node->attribute,1,0x7ef,hits,PATHWORLD_CAPACITY);
            for(int i=0;i<n;i++){
                const PathBodyHit *h=hits+i;if(h->contact.other_flags!=24)continue;
                unsigned order=0;while(order<w->count && w->slots[w->order[order]].task.handle!=h->handle)order++;
                if(!s->contacts || h->distance<nearest.distance ||
                   (h->distance==nearest.distance && (order<nearest_order || (order==nearest_order && bone<nearest_bone)))){
                    nearest=*h;nearest_order=order;nearest_bone=bone;
                }
                unsigned group=traffic_limb_group(node->attribute);
                s->group=!s->group ? group:s->group==group ? group:1;
                if(h->contact.other_speed>s->speed)s->speed=h->contact.other_speed;
                s->contacts++;s->limbs|=1u<<bone;
            }
        }
        memcpy(s->previous[bone],center,12);
    }
    s->valid=1;s->samples++;
    if(s->contacts){
        /* 0559ac rotates (0,0,-10) by Rx * Rz * Ry using the travel
           angles, not the independently smoothed render/body angles. */
        const float zero[3]={0},one[3]={1,1,1};int32_t a[3],axis[3]={0};
        float root[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        player_motion_angles(p,a);
        for(int i=0;i<3;i++){
            int k=i==0 ? 0:i==1 ? 2:1;axis[k]=a[k];
            source_trs_apply(zero,axis,one,root,player_fsca_sin);axis[k]=0;
        }
        float forward[3]={-10*root[2],-10*root[6],-10*root[10]};
        s->handle=nearest.handle;
        s->mode=traffic_contact_direction(&nearest.contact,s->group,forward,player_bam_atan2,&s->yaw);
    }
    return (int)s->contacts;
}
