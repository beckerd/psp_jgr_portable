#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mission.h"
#include "mission_quad.h"
#include "scene.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_eventnames.inc"
#else
#include "generated/source_data/eventnames.h"
#endif

/* English cutscene dialog (tools/jgs_events.py -> events.txt: NAME<TAB>text, \n = line break) */
static char *ev_text[128]; static char ev_name[128][16]; static int n_ev = -1;
static void events_load(void)
{
    n_ev = 0; SceUID fd = sceIoOpen("events.txt", PSP_O_RDONLY, 0777);  if (fd < 0) return;
    int len = sceIoLseek(fd, 0, PSP_SEEK_END); sceIoLseek(fd, 0, PSP_SEEK_SET); char *buf = malloc(len + 1); if (!buf) { sceIoClose(fd); return; } sceIoRead(fd, buf, len); buf[len] = 0; sceIoClose(fd);
    char *l = buf; while (l && *l && n_ev < 128) { char *nl = strchr(l, '\n'); if (nl) *nl = 0; char *tab = strchr(l, '\t'); if (tab) { *tab = 0; snprintf(ev_name[n_ev], 16, "%s", l); ev_text[n_ev] = tab + 1; n_ev++; } l = nl ? nl + 1 : 0; }
}
static void show_event(MissionState *M, int id)
{
    if (n_ev < 0) events_load();
    M->nsub = 0; if (id < 0 || id >= 112) return; const char *name = EVENT_NAMES[id]; const char *t = 0;
    for (int i = 0; i < n_ev; i++) if (!strcmp(ev_name[i], name)) { t = ev_text[i]; break; }
    if (!t) return;   /* animation-only cutscene (no dialog) */
    while (*t && M->nsub < 6) { char *o = M->sub[M->nsub]; int n = 0; while (*t && n < 63) { if (t[0] == '\\' && t[1] == 'n') { t += 2; break; } o[n++] = *t++; } o[n] = 0; if (n) M->nsub++; }
    M->sub_t = 1.5f + 1.2f * M->nsub;
}

static unsigned rd(const MissionState *M, unsigned addr) { unsigned o = addr - M->base; if (addr < M->base || o + 4 > M->len) return 0; return *(unsigned *)(M->img + o); }
static int  rdi(const MissionState *M, unsigned addr) { return (int)rd(M, addr); }
static void wr(MissionState *M, unsigned addr, unsigned v) { unsigned o = addr - M->base; if (addr < M->base || o + 4 > M->len) return; *(unsigned *)(M->img + o) = v; }
static int  inimg(const MissionState *M, unsigned addr) { return addr >= M->base && addr - M->base + 4 <= M->len; }
int mission_result_pose(const MissionState *M,int area,float position[3],int32_t angles[3])
{
    if(!M || !M->img || !inimg(M,M->ra+0xb8))return 0;
    unsigned p=M->ra+0xa4,a=M->ra+0xb0;
    if(area>=0 && area<256) {
        unsigned pp=rd(M,M->ra+(M->kind==2 ? 0xc4:0xbc));
        unsigned aa=rd(M,M->ra+(M->kind==2 ? 0xc8:0xc0));
        if(pp && aa) {
            pp+=area*12;aa+=area*12;
            if(!inimg(M,pp) || !inimg(M,pp+8) || !inimg(M,aa) || !inimg(M,aa+8))return 0;
            p=pp;a=aa;
        }
    }
    for(int j=0;j<3;j++) {unsigned word=rd(M,p+j*4);memcpy(position+j,&word,4);angles[j]=rdi(M,a+j*4);}
    return 1;
}
static void logev(MissionState *M, const char *s) { printf("ev: %s\n", s); memmove(M->log[1], M->log[0], sizeof M->log - sizeof M->log[0]); snprintf(M->log[0], 40, "%s", s); if (M->nlog < 6) M->nlog++; }

int missions_read(const char *path, MissionRec *out, int max)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return -1;
    char h[8]; sceIoRead(fd, h, 8); int n = *(int *)(h + 4);
    if (memcmp(h, "MIS3", 4) || n <= 0 || n > max) { sceIoClose(fd); return -2; }
    sceIoRead(fd, out, n * sizeof(MissionRec)); sceIoClose(fd); return n;
}

const MissionRec *mission_chain_select(const MissionRec *bank,int count,
                                      const MissionRec *current,int ordinal)
{
    if(!bank || !current || count<=0 || ordinal<0 || ordinal>=count ||
       current->first>=(unsigned)count)return NULL;
    unsigned first=current->first,index=first;
    for(int step=0;step<=ordinal;step++) {
        const MissionRec *r=bank+index;
        if(r->first!=first || r->base!=current->base || r->blob_off!=current->blob_off ||
           r->blob_len!=current->blob_len)return NULL;
        if(step==ordinal)return r;
        /* MIS3 emits each original chain in traversal order. Also rejects
           cycles/corrupt links without following an unbounded image chain. */
        if(r->next<0 || (unsigned)r->next<=index || r->next>=count)return NULL;
        index=(unsigned)r->next;
    }
    return NULL;
}
int mission_handoff(const MissionRec *bank,int count,const MissionRec *current,
                    int record,int set,int set_count,const MissionRec **selected)
{
    if(!selected)return -1;
    *selected=NULL;
    if(record<0 || set<0)return 0;
    if(set>=set_count)return -1;
    *selected=mission_chain_select(bank,count,current,record);
    return *selected ? 1:-1;
}

/* -1 terminated int list membership (FUN_8c03df0c / FUN_8c03df44) */
static int in_list(const MissionState *M, unsigned list, int v)
{ if (!inimg(M, list)) return 0; for (int k = 0; k < 256; k++) { int e = rdi(M, list + k * 4); if (e < 0) return 0; if (e == v) return 1; } return 0; }

static void run_events(MissionState *M, Graffiti *g, unsigned ev);

static unsigned task_word(const MissionState *M,unsigned handle,unsigned offset)
{ return handle && M->task_word ? M->task_word(M->ctx,handle,offset) : 0; }

/* 059c94/059dd8/059e38/059ed0: target a SOURCE EVENT, not a record
   number. 1 forces the trigger; 5 requests departure; 4 deletes outright.
   The completed-parent branches of 1/5 deliberately retain event-3 +0c. */
static void group_event_control(MissionState *M,unsigned event,int code)
{
    if(!(M->task_parents&1) || !inimg(M,event+28))return;
    unsigned kind=rd(M,event);
    if(kind!=2 && kind!=3)return;
    unsigned at=event+(kind==2 ? 8:28),handle=rd(M,at);
    if(!handle)return;
    if(code==4 || (task_word(M,handle,0x6e)&4)) {
        if(M->task_delete)M->task_delete(M->ctx,handle);
        wr(M,at,0);
        if(code==4 && kind==3)wr(M,event+12,0);
    } else {
        if(M->group_signal)M->group_signal(M->ctx,handle,code==5);
        if(code==5){wr(M,at,0);if(kind==3)wr(M,event+12,0);}
    }
}
static void group_event_spawn(MissionState *M,unsigned event)
{
    if(!(M->task_parents&1))return;
    group_event_control(M,event,4); /* original repeated spawn replaces the old parent */
    unsigned kind=rd(M,event),at=event+(kind==2 ? 8:28);
    unsigned handle=M->group_spawn ? M->group_spawn(M->ctx,rdi(M,event+4),event):0;
    wr(M,at,handle);
    if(kind==3 && handle)
        wr(M,event+12,M->group_graffiti ? M->group_graffiti(M->ctx,handle,rdi(M,event+8)):0);
}
static void group_events_delete_all(MissionState *M)
{
    if(!(M->task_parents&1))return;
    unsigned list=rd(M,M->ra+0x60);
    for(unsigned k=0;k<M->len/4 && inimg(M,list) && rd(M,list);k++,list+=4) {
        unsigned event=rd(M,list);
        for(unsigned j=0;j<M->len/32 && inimg(M,event+28) && rd(M,event);j++,event+=32)
            group_event_control(M,event,4);
    }
}

/* 0dd3b4: reset ALL registered routes, including their source event +8.
   Loading another route while an attack is active must not fire immediately. */
static void reset_police_routes(MissionState *M)
{
    unsigned l=rd(M,M->ra+0x50);
    for(unsigned k=0;k<M->len/12 && inimg(M,l+8) && rd(M,l);k++,l+=12) {
        wr(M,l+4,0);wr(M,rd(M,l)+8,0);
    }
}

/* 069608 / 069740: loss of the source actor rearms a latch; a falling
   object flag does NOT. Unknown event types leave their latches untouched. */
static void process_object_flags(MissionState *M,Graffiti *g,unsigned field,unsigned mask)
{
    unsigned l=rd(M,M->ra+field);
    for(unsigned k=0;k<M->len/16 && inimg(M,l+12) && rd(M,l);k++,l+=16) {
        unsigned event=rd(M,l),code=rd(M,event);
        if(code!=2 && code!=3)continue;
        unsigned handle=rd(M,event+(code==2 ? 8:28));
        if(!handle)wr(M,l+8,0);
        /* SH4 r4 is the source actor. r5=list+4 is unused by 0695f0/728;
           Ghidra incorrectly presents the latter as the sole argument. */
        else if((task_word(M,handle,0x6e)&mask) && !rd(M,l+8)) {
            wr(M,l+8,1);run_events(M,g,rd(M,l+12));
        }
    }
}

/* 0dd3e0, after the per-tick tutorial result dispatcher. */
static void process_police_routes(MissionState *M,Graffiti *g)
{
    int status=M->police_status ? M->police_status(M->ctx):-1;
    if(status<0)return;
    if(!(status&1)){M->police_armed=1;return;}
    unsigned l=rd(M,M->ra+0x50);
    for(unsigned k=0;k<M->len/12 && inimg(M,l+8) && rd(M,l);k++,l+=12) {
        unsigned event=rd(M,l);
        if(M->police_armed && rd(M,event)==0x1f && rd(M,event+8) && !rd(M,l+4)) {
            wr(M,l+4,1);run_events(M,g,rd(M,l+8));
        }
    }
}

/* 0aa7c4: source scripts, not active actors, are counted. A completed 0c
   still counts after its actor is deleted; 3's tagged bit also stays latched.
   Do not deduplicate shared script pointers: the original walks each entry. */
static void process_object_tags(MissionState *M)
{
    unsigned l=rd(M,M->ra+0x60);if(!inimg(M,l))return;
    int all=0,required=0;
    for(unsigned k=0;k<M->len/4 && inimg(M,l) && rd(M,l);k++,l+=4) {
        unsigned event=rd(M,l);
        for(unsigned j=0;j<M->len/32 && inimg(M,event+28) && rd(M,event);j++,event+=32) {
            unsigned code=rd(M,event);
            if(code==2 || code==3) {
                unsigned actor=event+(code==2 ? 8:28),handle=rd(M,actor);
                if((M->task_parents&1) && handle && (task_word(M,handle,0x6e)&4)) {
                    if(M->task_delete)M->task_delete(M->ctx,handle);
                    wr(M,actor,0);if(code==3)wr(M,event+12,0);
                }
                if(code==3) {
                    handle=rd(M,event+12);
                    if(task_word(M,handle,0x150))wr(M,event+20,1);
                    if(task_word(M,handle,0x14c))wr(M,event+24,1);
                    if(rd(M,event+24)){all++;if(rd(M,event+16)==1)required++;}
                }
            } else if(code==0xc && (M->task_parents&2) && rd(M,event+12)==1) {
                all++;required++;
            }
        }
    }
    M->object_tagged=all;M->object_required=required;
}

static MissionQuad *mission_quad(MissionState *M, unsigned address)
{
    if (!M->img || address < M->base || address - M->base > M->len ||
        M->len - (address - M->base) < sizeof(MissionQuad) || (address & 3)) return NULL;
    return (MissionQuad *)(M->img + (address - M->base));
}
void mission_position(MissionState *M, float x, float y, float z)
{ M->position[0] = x; M->position[1] = y; M->position[2] = z; }
void mission_rival_result(MissionState *M,unsigned event,int result)
{
    if(inimg(M,event+16) && rd(M,event)==0xc && rd(M,event+16) && (result==1 || result==2))
        wr(M,event+12,result);
}
/* 05a20c: update the shared sample before executing scripts, then walk in
   source order. Scripts may change a later entry's active mask this same call. */
static void process_quads(MissionState *M, Graffiti *g)
{
    unsigned l = rd(M, M->ra + 0x34); if (!inimg(M,l)) return;
    float s[6];
    for (int j = 0; j < 3; j++) {
        s[j] = M->quad_previous[j]; s[3+j] = M->position[j] - s[j];
        M->quad_previous[j] = M->position[j];
    }
    for (int k = 0; k < 256 && inimg(M,l) && rd(M,l); k++, l += 20) {
        MissionQuad *q = mission_quad(M,rd(M,l));
        if (q && rd(M,l+8) && !rd(M,l+12) && mission_quad_near(q,s) &&
            (mission_quad_cross(q,s) & rd(M,l+8))) run_events(M,g,rd(M,l+16));
    }
}

/* FUN_8c0699e0 (ev 0x11) / FUN_8c06bc98 (ev 0x12): set the clear / fail flag unless a result is already decided */
static void ev_clear(MissionState *M) { if (!(M->f38 || M->f3c) && !M->f40 && !M->f34) M->f30 = 1; }
int mission_request_clear(MissionState *M)
{
    if(!M || !M->active || M->phase || M->f38 || M->f3c || M->f40 || M->f34)return 0;
    ev_clear(M);return M->f30!=0;
}
int mission_request_exit(MissionState *M)
{
    if(!M || !M->active || M->phase)return 0;
    M->phase=MISSION_PHASE_EXIT;return 1;
}
static void ev_fail(MissionState *M)  { if (!(M->f38 || M->f3c) && !M->f30 && !M->f34) M->f40 = 1; }

static void path_event_control(MissionState *M,unsigned event,int opcode)
{
    if(!(M->task_parents&4) || !inimg(M,event) || !inimg(M,event+28))return;
    unsigned handle=rd(M,event+28);
    if(!handle)return;
    if(M->path_control)M->path_control(M->path_ctx ? M->path_ctx:M->ctx,handle,opcode);
    if(opcode==0xa)wr(M,event+28,0);
}
static void path_events_delete_all(MissionState *M)
{
    /* 069968: only registered event lists (+60), only opcode 9. Duplicate
       event pointers must observe the first deletion's cleared +1c handle. */
    if(!(M->task_parents&4))return;
    unsigned l=rd(M,M->ra+0x60);
    for(unsigned k=0;k<M->len/4 && inimg(M,l) && rd(M,l);k++,l+=4) {
        unsigned e=rd(M,l);
        for(unsigned j=0;j<M->len/32 && inimg(M,e+28) && rd(M,e);j++,e+=32)
            if(rd(M,e)==9)path_event_control(M,e,0xa);
    }
}
int mission_message_event(MissionState *M,unsigned event)
{
    if(!inimg(M,event+12) || rd(M,event)!=0x10)return -1;
    int voice=rdi(M,event+4),language=rdi(M,event+12);
    if(language!=1)return 0;
    unsigned address=rd(M,event+8);
    if(address<M->base || address-M->base>=M->len){M->message_error=1;return -1;}
    const char *text=(const char *)M->img+(address-M->base);
    unsigned available=M->len-(address-M->base),length=0;
    while(length<available && length<sizeof M->message.text && text[length])length++;
    if(length>=available || !message_hud_show(&M->message,text,length,voice,language)){M->message_error=1;return -1;}
    return 1;
}

static void run_events(MissionState *M, Graffiti *g, unsigned ev)
{
    if (M->phase==MISSION_PHASE_EXIT || !inimg(M, ev)) return;
    for (int n = 0; n < 128; n++, ev += 32) {
        int code = rdi(M, ev); if (code == 0) break;
        int a1 = rdi(M, ev + 4), a2 = rdi(M, ev + 8), a3 = rdi(M, ev + 12);
        char b[40];
        switch (code) {
        case 0x11: ev_clear(M); logev(M, "EV11 MISSION CLEAR"); break;
        case 0x12: ev_fail(M); logev(M, "EV12 MISSION FAIL"); break;
        case 0x19:
            if(mission_request_exit(M))logev(M,"EV19 PHASE 0xB (EXIT)");
            break; /* 0d050c -> 02ff20(b): fade without mission results. */
        case 0x14: M->next_rec = a1; break;                                         /* FUN_8c0ca14c: loader task 0x1c +0x90 = next record of the JMDc chain (FUN_8c041f78) */
        case 0x15: M->next_set = a1; break;                                         /* FUN_8c0ca170: loader task 0x1c +0x94 = next set of the JMSc chain (FUN_8c09fec8) */
        case 0x22: M->retry_rec = a1; break;                                        /* FUN_8c0ca194: loader +0x98 = retry record (FUN_8c0ca1dc reloads it after a fail) */
        case 0x23: M->retry_set = a1; break;                                        /* FUN_8c0ca1b8: loader +0x9c = retry set index */
        case 0x2c: M->flag_2faee8 = 1; break; case 0x2d: M->flag_2faee8 = 0; break;
        case 0x1b: {   /* 0d0578 -> 042030: +34, NOT the +30 pointer table. */
            unsigned l = rd(M, M->ra + 0x34);
            for (int k = 0; k <= a1 && k < 256 && inimg(M, l) && rd(M, l); k++, l += 20)
                if (k == a1) { wr(M, l + 8, a2); break; }
        } break;
        case 0x26: M->rival_cur = a1; break;                                        /* Selector only: source 059fec resolves set +20 table[a1] into 2c55a8; descriptor consumer still unported. */
        case 0xe: case 0x1c:
            if(code==0xe){M->scene_request=a1;if(!scene_supported(a1) && !scene_dialogue_mode(a1) && !scene_bespoke_supported(a1))show_event(M,a1);}else M->scene_preload=a1;
            snprintf(b,40,"EV%X %s",code,a1>=0&&a1<112 ? EVENT_NAMES[a1]:"?");logev(M,b);break;
        case 0x10: mission_message_event(M,ev);break;
        case 0xf: snprintf(b, 40, "EV0F DEMO %08X", a1); logev(M, b); break;        /* FUN_8c018e7c: scripted demo playback (task 0x1f) */
        case 6: if (M->ped_zone) M->ped_zone(M->ctx, a1, a2, 1); else if (M->ped_list) M->ped_list(M->npcs, a1, 1); break;                /* FUN_8c05a038: spawn pedestrian list a1 (zone gating a2 vs the current zone not ported: spawned immediately) */
        case 7: if (M->ped_zone) M->ped_zone(M->ctx, a1, a2, 0); else if (M->ped_list) M->ped_list(M->npcs, a1, 0); break;                /* FUN_8c05a0e8: remove pedestrian list a1 */
        case 9:
            if((M->task_parents&4) && !rd(M,ev+28) && M->path_spawn && inimg(M,ev+28)) {
                unsigned args[7];for(int j=0;j<7;j++)args[j]=rd(M,ev+4+j*4);
                unsigned handle=M->path_spawn(M->path_ctx ? M->path_ctx:M->ctx,ev,args);
                if(handle)wr(M,ev+28,handle);
            }
            break;
        case 2: case 3: group_event_spawn(M,ev); snprintf(b,40,"EV%X GROUP %d",code,a1);logev(M,b);break;
        case 1: case 4: case 5: group_event_control(M,(unsigned)a1,code);break;
        case 0x13: group_events_delete_all(M);break;
        case 0xa: case 0xb:path_event_control(M,(unsigned)a1,code);break;
        case 0x24:path_events_delete_all(M);break;
        case 0xc:
            if(!rd(M,ev+16) && M->rival_spawn) {
                int handle=M->rival_spawn(M->ctx,a1,a2,ev);
                if(handle>0){wr(M,ev+16,(unsigned)handle);wr(M,ev+12,0);}
            }
            snprintf(b,40,"EVC RIVAL slot %d tbl %d",a1,a2);logev(M,b);break;
        case 0xd: /* 069b54: a1 is the ORIGINAL event address, not a slot. */
            if(inimg(M,(unsigned)a1+16) && rd(M,a1)==0xc && rd(M,a1+16)) {
                if(M->rival_delete)M->rival_delete(M->ctx,(unsigned)a1);
                wr(M,a1+16,0);
            }
            break;
        case 0x17: case 0x18: case 0x1d: case 0x1e: { int a4 = rdi(M, ev + 16); if (M->police) M->police(M->ctx, code, a1, a2, a3, a4); if (code == 0x17 || code == 0x1d) M->police_level = code == 0x17 ? 0 : a4; else M->police_level = -1;
            snprintf(b, 40, code == 0x17 || code == 0x1d ? "EV%X ALERT OBJ %d,%d,%d" : "EV%X ALERT OFF", code, a1, a2, a3); logev(M, b); break; }   /* FUN_8c0d0474 / FUN_8c0da1dc: the police alert object (task 0x5a: FUN_8c049b9c) placed at the event's position with level 0 / a4; FUN_8c0d04e8 / FUN_8c0da280 remove it */
        case 0x21: if (M->wave) M->wave(M->ctx, 2000 + a1); snprintf(b, 40, "EV21 POLICE WAVE %d", a1); logev(M, b); break;   /* 0dd540: arrival list indexed by manager route segment */
        case 0x1f: /* 0dd4b8: invalid/missing routes leave the current route and latches alone. */
            if(M->police_route && M->police_route(M->ctx,a1)) {
                reset_police_routes(M);M->police_armed=0;wr(M,ev+8,1);
            }
            snprintf(b,40,"EV1F POLICE %d",a1);logev(M,b);break;
        case 0x20: /* 0dd510: clearing does not change 1cf658. */
            if(M->police_route && M->police_route(M->ctx,-1))reset_police_routes(M);
            logev(M,"EV20 POLICE CLEAR");break;
        case 0x27: case 0x28: case 0x29:
            if(M->train)M->train(M->ctx,code,(unsigned)a1,(unsigned)a2,(unsigned)a3);
            break;
        case 0x2a: case 0x2b: snprintf(b, 40, "EV%X TASK", code); logev(M, b); break;                    /* tasks 0x8ac / 0xa03 */
        default: snprintf(b, 40, "EV%X ?", code); logev(M, b); break;
        }
        if(M->phase==MISSION_PHASE_EXIT)return;
    }
}

/* FUN_8c03db94: per area, create the spots of the stage's area list that are not excluded; required ones count toward completion,
   pre-tagged ones (JMDc +0x24 != 0) start covered by the rival gang's graffiti (FUN_8c03e540 variant). */
static void setup_spots(MissionState *M, Graffiti *g)
{
    graffiti_reset(g);
    unsigned req = rd(M, M->ra + 0x64), exc = rd(M, M->ra + 0x68), pre = rd(M, M->ra + 0x6c);
    int pretag_on = rd(M, M->ra + 0x24) != 0;
    M->nreq = 0;
    for (int i = 0; i < g->n; i++) {
        Spot *s = &g->spots[i]; int a = s->area;
        unsigned lr = inimg(M, req) ? rd(M, req + a * 4) : 0, le = inimg(M, exc) ? rd(M, exc + a * 4) : 0, lp = inimg(M, pre) ? rd(M, pre + a * 4) : 0;
        s->active = !in_list(M, le, s->idx); s->required = s->active && in_list(M, lr, s->idx); s->pretag = s->active && pretag_on && in_list(M, lp, s->idx);
        s->tagged = 0; if (s->required) M->nreq++;
    }
}

int mission_start(MissionState *M, const MissionRec *r, const char *path, Graffiti *g)
{
    /* These four words belong to loader task 1c, not the per-record mission
       processors. 0ca2ac initializes them to zero ONCE; 0ca1dc copies the
       retry pair to the next pair. Keep them across same-image phases. */
    int same=M->active && M->rec && M->rec->first==r->first &&
        M->rec->blob_off==r->blob_off && M->rec->blob_len==r->blob_len;
    int next_rec=same?M->next_rec:0,next_set=same?M->next_set:0;
    int retry_rec=same?M->retry_rec:0,retry_set=same?M->retry_set:0;
    void (*train)(void *,int,unsigned,unsigned,unsigned)=M->train;
    int (*route)(void *,int)=M->police_route;
    int (*status)(void *)=M->police_status;
    unsigned (*word)(void *,unsigned,unsigned)=M->task_word;
    void (*del)(void *,unsigned)=M->task_delete;unsigned parents=M->task_parents;
    unsigned (*gs)(void *,int,unsigned)=M->group_spawn;
    unsigned (*gg)(void *,unsigned,int)=M->group_graffiti;
    void (*signal)(void *,unsigned,int)=M->group_signal;
    unsigned (*pathspawn)(void *,unsigned,const unsigned[7])=M->path_spawn;
    void (*pathcontrol)(void *,unsigned,int)=M->path_control;
    void *pathctx=M->path_ctx;void (*pathreset)(void *)=M->path_reset;
    if(pathreset)pathreset(pathctx ? pathctx:M->ctx);
    free(M->img);M->img=NULL;
    void *npcs = M->npcs; void (*pl)(void *, int, int) = M->ped_list; void (*wv)(void *, int) = M->wave;
    void *ctx = M->ctx; int (*rs)(void *,int,int,unsigned)=M->rival_spawn;void (*remove)(void *,unsigned)=M->rival_delete;
    void (*pz)(void *, int, int, int) = M->ped_zone; void (*pc)(void *, int, int, int, int, int) = M->police; memset(M, 0, sizeof *M); M->police = pc; M->rec = r; M->npcs = npcs; M->ped_list = pl; M->wave = wv; M->ctx = ctx; M->rival_spawn = rs; M->rival_delete=remove;M->ped_zone = pz;
    M->train=train;M->police_route=route;M->police_status=status;M->task_word=word;M->task_delete=del;M->task_parents=parents;
    M->group_spawn=gs;M->group_graffiti=gg;M->group_signal=signal;
    M->path_spawn=pathspawn;M->path_control=pathcontrol;M->path_ctx=pathctx;M->path_reset=pathreset;
    message_hud_init(&M->message);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return -1;
    M->img = malloc(r->blob_len); if (!M->img) { sceIoClose(fd); return -2; }
    sceIoLseek(fd, r->blob_off, PSP_SEEK_SET); int got=sceIoRead(fd, M->img, r->blob_len); sceIoClose(fd);
    if(got<0 || (unsigned)got!=r->blob_len || r->rec_off>r->blob_len ||
       r->blob_len-r->rec_off<0xf0 || memcmp(M->img+r->rec_off,"JMDc",4)) {
        free(M->img);M->img=NULL;return -3;
    }
    M->base = r->base; M->len = r->blob_len; M->ra = r->base + r->rec_off; M->active = 1; M->kind = r->kind;
    M->next_rec=next_rec;M->next_set=next_set;M->retry_rec=retry_rec;M->retry_set=retry_set;
    M->scene_request=M->scene_preload=-1;
    M->time_total = M->time_left = r->time_sec * 30;                  /* FUN_8c0c2254(+0xe4 * 0x1e) */
    if (g) setup_spots(M, g);
    /* FUN_8c059288: list registration resets the per-entry fired flags */
    { unsigned l = rd(M, M->ra + 0x38); for (int k = 0; inimg(M, l) && k < 64 && rd(M, l) != 0; k++, l += 24) wr(M, l + 16, 0); }        /* cond list [4] */
    { unsigned l = rd(M, M->ra + 0x40); for (int k = 0; inimg(M, l) && k < 64 && rd(M, l) != 0; k++, l += 16) { wr(M, l + 4, 0); wr(M, l + 8, 0); } }
    /* 042030 returns +34; 05a158 copies each quad's initial direction mask.
       +30 is an independent pointer table and must not be overwritten here.
       05a158 does NOT clear entry +12. Its previous position is 042114 (+70),
       initialized before the first slot-0 processor, not the first live sample. */
    for (int j = 0; j < 3; j++) {
        unsigned bits = rd(M,M->ra+0x70+j*4); float v; memcpy(&v,&bits,4);
        M->position[j] = M->quad_previous[j] = v;
    }
    { unsigned l = rd(M, M->ra + 0x34); for (int k = 0; inimg(M, l) && k < 256 && rd(M, l) != 0; k++, l += 20) {
        MissionQuad *q = mission_quad(M,rd(M,l)); if (q) mission_quad_prepare(q);
        wr(M, l + 8, rd(M, l + 4));
    } }
    /* 069578 / 0696b0: clear the two object-flag lists' fired latches. */
    for (int field = 0x48; field <= 0x4c; field += 4) {
        unsigned l = rd(M, M->ra + field);
        for (int k = 0; inimg(M, l) && k < 256 && rd(M, l); k++, l += 16) wr(M, l + 8, 0);
    }
    /* 0dd2c8 only clears list latches, unlike an actual route load. */
    M->police_armed=1;
    { unsigned l=rd(M,M->ra+0x50);
      for(unsigned k=0;k<M->len/12 && inimg(M,l+8) && rd(M,l);k++,l+=12)wr(M,l+4,0); }
    /* 0aa724 registration clears graffiti activity/tagged latches. */
    { unsigned l=rd(M,M->ra+0x60);
      for(unsigned k=0;k<M->len/4 && inimg(M,l) && rd(M,l);k++,l+=4) {
        unsigned e=rd(M,l);
        for(unsigned j=0;j<M->len/32 && inimg(M,e+28) && rd(M,e);j++,e+=32)
            if(rd(M,e)==3){wr(M,e+20,0);wr(M,e+24,0);}
      } }
    run_events(M, g, rd(M, M->ra + 0x5c));                          /* init events */
    return 0;
}

/* FUN_8c03e0f4: tagged required spots per area (+0x84); FUN_8c03e0ac: sum */
static void count_tags(MissionState *M, Graffiti *g)
{
    int c[6] = {0}; M->ntagged = 0;
    for (int i = 0; i < g->n; i++) { Spot *s = &g->spots[i]; if (s->active && s->required && s->tagged && s->area < 6) { c[s->area]++; M->ntagged++; } }
    for (int a = 0; a < 6; a++) { M->tag_prev[a] = M->tag_cur[a]; M->tag_cur[a] = c[a]; }
    M->tot_prev = M->tot_cur; M->tot_cur = M->ntagged + M->object_required + M->extra_tagged;
}

void mission_tick(MissionState *M, Graffiti *g, int player_caught)
{
    if (!M->active || M->phase) return;
    /* FUN_8c0c24a8: mission timer */
    if (M->time_left > 0 && --M->time_left == 0) { if (M->kind == 2) { if (!(M->f38 || M->f3c) && !M->f30 && !M->f34 && !M->f40) M->f38 = 1; } else { if (!(M->f38 || M->f3c) && !M->f30 && !M->f34 && !M->f40) M->f3c = 1; } logev(M, "TIME UP"); }
    /* FUN_8c059748: the list processors run round-robin, one per frame (mod 9) */
    int slot = M->seq; M->seq = (M->seq + 1) % 9;
    if (slot == 0) process_quads(M,g);
    /* Source tests (counter + offset) % 9 == 0, NOT counter % 9 == offset. */
    if (slot == 8) {   /* FUN_8c05d6d8: tag-count thresholds {count, area 0..4 | 6 total, event}, -1 terminated */
        count_tags(M, g);
        unsigned l = rd(M, M->ra + 0x3c);
        for (int k = 0; inimg(M, l) && k < 64; k++, l += 12) {
            int cnt = rdi(M, l); if (cnt < 0) break; int area = rdi(M, l + 4); unsigned ev = rd(M, l + 8);
            if (area >= 0 && area <= 4) { if (M->tag_prev[area] < cnt && cnt <= M->tag_cur[area]) run_events(M, g, ev); }
            else if (area == 6) { if (M->tot_prev < cnt && cnt <= M->tot_cur) run_events(M, g, ev); }
        }
    } else if (slot == 7) {   /* FUN_8c05d5b8: conditions {type, idx, area, obj, fired, event}: type 1 = spot (area, idx) tagged */
        unsigned l = rd(M, M->ra + 0x38);
        for (int k = 0; inimg(M, l) && k < 64; k++, l += 24) {
            int type = rdi(M, l); if (type == 0) break; if (rd(M, l + 20) == 0) continue;
            int hit = 0;
            if (type == 1) { int idx = rdi(M, l + 4), area = rdi(M, l + 8); for (int i = 0; i < g->n; i++) { Spot *s = &g->spots[i]; if (s->active && s->area == area && s->idx == idx && s->tagged) { hit = 1; break; } } }
            else if(type==2)hit=task_word(M,rd(M,rd(M,l+12)+12),0x154)!=0; /* 0aa900 */
            else continue; /* unknown types preserve their latch */
            if (!hit) wr(M, l + 16, 0); else if (rd(M, l + 16) == 0) { wr(M, l + 16, 1); run_events(M, g, rd(M, l + 20)); }
        }
    } else if(slot==6) { /* 069860: completed mode-3 rival, once per registration */
        unsigned l=rd(M,M->ra+0x40);
        for(int k=0;k<256 && inimg(M,l+12) && rd(M,l);k++,l+=16) {
            unsigned event=rd(M,l);
            if(!rd(M,l+4) && inimg(M,event+16) && rd(M,event)==0xc && rd(M,event+16) && rd(M,event+12)==1) {
                wr(M,l+4,1);run_events(M,g,rd(M,l+12));
            }
        }
    } else if (slot == 5) {   /* FUN_8c05d864: score triggers {score, event}, -1 terminated, on the score 0x8c1793f0 (FUN_8c035f80) */
        M->time_prev = M->time_cur; M->time_cur = M->score;
        unsigned l = rd(M, M->ra + 0x44);
        for (int k = 0; inimg(M, l) && k < 64; k++, l += 8) { int t = rdi(M, l); if (t < 0) break; if (M->time_prev < t && t <= M->time_cur) run_events(M, g, rd(M, l + 4)); }
    } else if(slot==4)process_object_flags(M,g,0x48,1);
    else if(slot==3)process_object_flags(M,g,0x4c,2);
    if(M->phase==MISSION_PHASE_EXIT)return;
    /* 059748 samples 09b314 after slots 0/8/7/6/5/4/3, before 2/1.
       Player callbacks only write the shared result word. */
    mission_trial_result(M,g,M->trial_result);
    if(M->phase==MISSION_PHASE_EXIT)return;
    if(slot==2)process_police_routes(M,g);
    if(slot==1)process_object_tags(M);
    if(M->phase==MISSION_PHASE_EXIT)return;
    if (M->sub_t > 0.f) { M->sub_t -= 1.f / 30.f; if (M->sub_t <= 0.f) M->nsub = 0; }
    /* FUN_8c06bdf0 head: result decision */
    if (M->f30 || M->f38) { M->phase = 1; logev(M, "RESULT: CLEAR"); return; }          /* FUN_8c06d868 -> FUN_8c06d718 */
    if (M->f3c || M->f40 || player_caught) { M->phase = 2; M->fail_code = player_caught ? 0x14 : 0x13; logev(M, "RESULT: FAIL"); return; }   /* FUN_8c06d5e4 -> FUN_8c06d640 */
    if (M->f34) { M->phase = 1; return; }                                              /* FUN_8c06d8a0 */
}

/* FUN_8c0361ec (results task, states 0x19/0x17/0xd): 0360d8 first maps
   mission kind through 17942e, then 1793f8[result_class*9] enables bonuses ([2] life,
   [4] time, [3] cans): life = (int)(health/max * LIFE_K) * 30 (+LIFE_K when untouched), time = remaining seconds * 30, cans = cans * 100 */
#define LIFE_K 100.0f

#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_mission_c__result_class_0.inc"
#else

#include "generated/source_data/mission_c__RESULT_CLASS_0.inc"

#endif


#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_mission_c__kind_flags_0.inc"
#else

#include "generated/source_data/mission_c__KIND_FLAGS_0.inc"

#endif

void mission_results(MissionState *M, int score, int health, int health_max, int cans)
{
    if (M->res_done) return; M->res_done = 1;
    int result_class=(unsigned)M->kind<10 ? RESULT_CLASS[M->kind]:5;
    const unsigned char *f = KIND_FLAGS[result_class];
    M->res_life = f[2] && health_max > 0 ? (int)((float)health / (float)health_max * LIFE_K) * 30 + (health == health_max ? 3000 : 0) : 0;   /* DAT_8c03651c = {100.0f, 3000} */
    M->res_time = f[4] ? (M->time_left / 30) * 30 : 0;
    M->res_cans = f[3] ? cans * 100 : 0;
    M->res_total = score + M->res_life + M->res_time + M->res_cans;
    /* Class 4 (kind 8, Jet Tech) goes directly from state 17 to d:
       elapsed ticks replace the total; it never enters life/can tally. */
    if(result_class==4) {
        M->res_life=M->res_cans=0;
        M->res_time=M->res_total=M->time_total-M->time_left;
    }
    /* FUN_8c0380b8 / FUN_8c038118: five ordered thresholds at JMDc +0x2c.
       Normal results rank by total points; kind 8 ranks elapsed ticks against
       descending time limits.  Rank 5 is the best graffiti rank. */
    M->res_rank = 0;
    { unsigned p = rd(M, M->ra + 0x2c);
      for (int i = 0; i < 5 && inimg(M, p + i * 4); i++) {
          int t = rdi(M, p + i * 4); if (t < 0) break;
          if (M->kind == 8 ? M->time_total - M->time_left <= t * 30 : t <= M->res_total) M->res_rank++;
          else break;
      }
    }
}
void mission_trial_result(MissionState *M, Graffiti *g, int result)
{
    if (!M->active) return;
    if (result == 1 && !M->trial_done1) { M->trial_done1 = 1; unsigned s = rd(M, M->ra + 0x54); if (inimg(M, s)) run_events(M, g, s); logev(M, "TRIAL OK"); }
    if (result == 2 && !M->trial_done2) { M->trial_done2 = 1; unsigned s = rd(M, M->ra + 0x58); if (inimg(M, s)) run_events(M, g, s); logev(M, "TRIAL FAIL"); }
}
void mission_path_released(MissionState *M,unsigned event,unsigned handle)
{
    if(!M || !handle || event>UINT32_MAX-28u)return;
    if(inimg(M,event) && inimg(M,event+28) && rd(M,event)==9 && rd(M,event+28)==handle)wr(M,event+28,0);
}
void mission_free(MissionState *M)
{
    if(M->path_reset)M->path_reset(M->path_ctx ? M->path_ctx:M->ctx);
    free(M->img);memset(M,0,sizeof *M);
}

unsigned mission_word(const MissionState *M, unsigned off) { if (!M->img || M->ra + off + 4 > M->base + M->len) return 0; return *(const unsigned *)(M->img + (M->ra - M->base) + off); }
