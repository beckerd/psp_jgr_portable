#ifndef JGR_CAR_PULL_H
#define JGR_CAR_PULL_H
#include <math.h>
#include <stdint.h>
#include <string.h>
/* 0c8c50's rear-grip payload. This is distinct from traffic body damage. */
typedef struct {float velocity[3],anchor[3],translation[3];unsigned handle;} CarPullContact;
typedef struct {
    CarPullContact contact;
    float reference[3];
    int heading,released,active,valid;
} CarPull;
/* Original 052a28. Return 1 only for a captured rear grip; far_contact is
   passed to ordinary skating when the contact is too far to hold. */
static inline int car_pull_step(CarPull *p,const float position[3],int *yaw,
    float *speed,float carry[3],int motion,int (*angle)(float,float),int *far_contact)
{
    *far_contact=0;p->active=0;
    if(motion>=4 && motion<=7) {
        if(!p->contact.handle)p->released=0;
        else if(!p->released) {
            int target=angle(-p->contact.velocity[0],-p->contact.velocity[2]);
            int turn=(int16_t)(target-p->heading);if(turn<0)turn=-turn;
            if(turn<10922) { /* shared player tuning +e4 */
                float d[3];for(int k=0;k<3;k++)d[k]=(p->contact.anchor[k]+p->contact.translation[k])-position[k];
                float length=sqrtf((float)((double)d[0]*d[0]+(double)d[1]*d[1]+(double)d[2]*d[2]));
                if(length<20.f) {
                    for(int k=0;k<3;k++)carry[k]+=d[k]*.2f;
                    int delta=(int16_t)(target-*yaw);if(delta>910)delta=910;if(delta< -910)delta=-910;
                    *yaw=(int16_t)(*yaw+delta);
                    const float *v=p->contact.velocity;
                    *speed=sqrtf((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
                    p->heading=target;memcpy(p->reference,p->contact.anchor,12);p->active=1;return 1;
                }
                *far_contact=1;
            }
        }
    }
    p->heading=*yaw;return 0;
}
#endif
