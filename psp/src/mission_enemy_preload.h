#ifndef MISSION_ENEMY_PRELOAD_H
#define MISSION_ENEMY_PRELOAD_H
#include <stdint.h>
#include <stddef.h>
#include "mission.h"
#include "group_data.h"

/* Loading-only adapter. Inspect the selected mission's event references,
   including dormant triggers, without executing events or scanning unrelated
   missions in the same image/group bank. Missing data simply keeps the usual
   lazy resource path; this is never an authority for spawning gameplay. */
typedef struct {
    uint64_t groups, formations;
    int waves, traffic;
} MissionEnemyReferences;

static inline int mission_enemy_range(const MissionState *m,uint32_t address,unsigned bytes)
{
    return m && m->img && (!bytes || address<=UINT32_MAX-(bytes-1)) &&
           address>=m->base && bytes<=m->len &&
           address-m->base<=m->len-bytes;
}
static inline uint32_t mission_enemy_word(const MissionState *m,uint32_t address)
{
    return mission_enemy_range(m,address,4) ? group_u32(m->img+(address-m->base)):0;
}
static inline void mission_enemy_events(const MissionState *m,uint32_t event,
                                        MissionEnemyReferences *refs)
{
    /* run_events executes at most 128 complete 32-byte commands. */
    for(unsigned n=0;n<128 && mission_enemy_range(m,event,32);n++) {
        uint32_t code=mission_enemy_word(m,event),arg=mission_enemy_word(m,event+4);
        if(!code)break;
        if((code==2 || code==3) && (m->task_parents&1) && arg<64)
            refs->groups|=UINT64_C(1)<<arg;
        /* Main's zone hook can activate already loaded formation members,
           but does not create the dedicated Gang actors for an EV6 list. */
        if(code==6 && arg>=1000 && arg<1064)
            refs->formations|=UINT64_C(1)<<(arg-1000);
        if(code==0x21 && arg<16 && mission_enemy_word(m,m->ra+0x50))refs->waves=1;
        if(code==9 && (m->task_parents&4))refs->traffic=1;
        if(event>UINT32_MAX-32u)break;
        event+=32;
    }
}
static inline uint64_t mission_enemy_group_kinds(const void *groups,unsigned bytes,
                                                unsigned index,int formations_only)
{
    GroupRecord record;uint64_t kinds=0;
    if(group_record_read(groups,bytes,(int)index,&record) || (!formations_only && !record.supported))return 0;
    const unsigned char *p=record.subs;
    for(unsigned i=0;i<record.count;i++) {
        unsigned type=group_u32(p),count=group_u32(p+40);
        const unsigned char *entries=p+44;p=entries+count*28;
        if(type>=26 && type<=30) {
            /* Entry zero is the invisible formation anchor. For basic
               police, riot police and agents, nonzero poses are vehicles. */
            int member=0;
            for(unsigned j=1;j<count;j++)
                if((type!=26 && type!=27 && type!=29) || !group_u32(entries+j*28+24))member=1;
            if(member)kinds|=UINT64_C(1)<<type;
        } else if(!formations_only && count && type<=32)kinds|=UINT64_C(1)<<type;
    }
    return kinds;
}

/* Bits retain GRP1 creator kinds (0..32); bit44 denotes the separate WAV1
   police lineups. Those lineups currently have no attack callbacks. This
   mask is for attack resource preparation: anchor-only/parked formations
   are excluded. The final-arena controller is separate from GRP1 kind23. */
static inline MissionEnemyReferences mission_preload_references(const MissionState *m)
{
    MissionEnemyReferences refs={0};
    if(!m || !m->active || !mission_enemy_range(m,m->ra,0x64))return refs;
    /* Current record trigger tables mirror mission_tick's layouts, limits,
       and terminators. Latches/active masks can change later, so do not use
       their present values to discard future attacks. */
    static const struct { unsigned field,stride,event,limit,negative; } lists[]={
        {0x34,20,16,256,0},{0x38,24,20,64,0},{0x3c,12,8,64,1},
        {0x40,16,12,256,0},{0x44,8,4,64,1},
        {0x48,16,12,0,0},{0x4c,16,12,0,0},{0x50,12,8,0,0}
    };
    for(unsigned i=0;i<sizeof lists/sizeof *lists;i++) {
        uint32_t at=mission_enemy_word(m,m->ra+lists[i].field);
        unsigned limit=lists[i].limit ? lists[i].limit:m->len/lists[i].stride;
        for(unsigned n=0;n<limit && mission_enemy_range(m,at,lists[i].stride);n++) {
            uint32_t first=mission_enemy_word(m,at);
            if(lists[i].negative ? (first&UINT32_C(0x80000000))!=0:first==0)break;
            mission_enemy_events(m,mission_enemy_word(m,at+lists[i].event),&refs);
            if(at>UINT32_MAX-lists[i].stride)break;
            at+=lists[i].stride;
        }
    }
    for(unsigned field=0x54;field<=0x5c;field+=4)
        mission_enemy_events(m,mission_enemy_word(m,m->ra+field),&refs);
    /* +60 registers event lists for persistent group/object ownership. */
    uint32_t table=mission_enemy_word(m,m->ra+0x60);
    for(unsigned n=0;n<m->len/4 && mission_enemy_range(m,table,4);n++) {
        uint32_t event=mission_enemy_word(m,table);if(!event)break;
        mission_enemy_events(m,event,&refs);
        if(table>UINT32_MAX-4u)break;
        table+=4;
    }
    return refs;
}
static inline uint64_t mission_enemy_preload_kinds(const MissionState *m,
                                                  const void *groups,size_t bytes)
{
    if(bytes>UINT32_MAX)return 0;
    MissionEnemyReferences refs=mission_preload_references(m);
    uint64_t kinds=refs.waves ? UINT64_C(1)<<44:0;
    for(unsigned i=0;i<64;i++) {
        uint64_t bit=UINT64_C(1)<<i;
        if((refs.groups|refs.formations)&bit)
            kinds|=mission_enemy_group_kinds(groups,(unsigned)bytes,i,(refs.groups&bit)==0);
    }
    return kinds;
}
static inline int mission_preload_traffic(const MissionState *m)
{return mission_preload_references(m).traffic;}
#endif
