#ifndef JGR_ENEMY_DAMAGE_H
#define JGR_ENEMY_DAMAGE_H
/* USA 06e238, followed by the extra 07ca9c/0867d4/09ab40 dash gate.
   These are source main-state words, not the port's recovery enum. */
static inline int enemy_bullet_vulnerable(int state,int air_trick,int boost)
{
    switch(state) {
    case 2:return !air_trick;
    case 3:case 4:case 7:case 8:case 10:case 11:return 0;
    case 1:return !boost;
    default:return 1;
    }
}
/* 042424/04248c dispatch through 17ec20/17ec50. The health charge
   (character +28) and shared +0 invincibility are applied by the owner. */
typedef struct {int motion,airborne;float speed,brake,velocity,gravity;} VehicleResponse;
static inline VehicleResponse vehicle_response(int mode,int airborne,float speed,
                                               float jump_height,int recovery_ticks)
{
    VehicleResponse r={0};int group=mode/3,n=recovery_ticks>>1;
    if((unsigned)mode>=12 || n<=0)return r;
    if(airborne || group==0 || group==2) {
        r.airborne=1;r.gravity=jump_height/(n*(n+1)/2.f);
        r.velocity=(group==1 || group==3) ? 0.f:r.gravity*n;
        r.speed=group==0 ? -0x1.da12f6p+0f:group==2 ? 0x1.da12f6p+0f:speed*.3f;
        r.motion=group==0 ? 84:group==1 ? 83:87;
    } else {
        r.brake=(group==1 ? speed:-speed)/18.f;
        r.speed=group==1 ? -speed:speed;
        r.motion=group==1 ? (mode==3 ? 83:79):(mode==9 ? 82:80);
    }
    return r;
}
#endif
