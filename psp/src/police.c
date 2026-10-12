#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "police.h"
#include "police_hud.h"

static unsigned rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static float rdf(const unsigned char *p) { unsigned u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

void police_free(Police *P)
{
    for (int r = 0; r < P->nrec; r++) { PolRoute *R = &P->rec[r]; free(R->pt); free(R->ar); free(R->sg); free(R->mem); free(R->adj); }
    free(P->data); memset(P, 0, sizeof *P); P->level = -1;
}

int police_load(Police *P, const char *path)
{
    police_free(P);
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc(n); if (!d || fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return -2; }
    fclose(f);
    if (memcmp(d, "POL1", 4)) { free(d); return -3; }
    int nrec = rd32(d + 4); if (nrec > 32) nrec = 32; P->nrec = nrec;
    const unsigned char *q = d + 8;
    for (int r = 0; r < nrec; r++) {
        PolRoute *R = &P->rec[r];
        R->npts = rd32(q); R->nareas = rd32(q + 4); R->nsegs = rd32(q + 8); R->nmem = rd32(q + 12); R->nadj = rd32(q + 16); q += 20;
        R->pt = calloc(R->npts + 1, sizeof(PolPt)); R->ar = calloc(R->nareas + 1, sizeof(PolArea)); R->sg = calloc(R->nsegs + 1, sizeof(PolSeg));
        R->mem = calloc(R->nmem + 1, sizeof(PolMem)); R->adj = calloc(R->nadj + 1, sizeof(PolAdj));
        for (int i = 0; i < R->npts; i++) { PolPt *p = &R->pt[i]; for (int k = 0; k < 3; k++) p->pos[k] = rdf(q + 4 * k); for (int k = 0; k < 12; k++) p->quad[k] = rdf(q + 12 + 4 * k); p->r2 = rdf(q + 60); p->nadj = rd32(q + 64); p->adj0 = rd32(q + 68); p->seg = -1; q += 72; }
        for (int i = 0; i < R->nareas; i++) { PolArea *a = &R->ar[i]; for (int k = 0; k < 3; k++) a->pos[k] = rdf(q + 4 * k); for (int k = 0; k < 12; k++) a->quad[k] = rdf(q + 12 + 4 * k); a->r2 = rdf(q + 60); a->pt = rd32(q + 64); a->pt2 = rd32(q + 68); q += 72; }
        for (int i = 0; i < R->nsegs; i++) { PolSeg *s = &R->sg[i]; for (int k = 0; k < 3; k++) s->pos[k] = rdf(q + 4 * k); s->kind = rd32(q + 12); s->start = rd32(q + 16); s->nmem = rd32(q + 20); s->mem0 = rd32(q + 24); for (int k = 0; k < 12; k++) s->geom[k] = rdf(q + 28 + 4 * k); q += 76; }
        for (int i = 0; i < R->nmem; i++) { PolMem *m = &R->mem[i]; for (int k = 0; k < 9; k++) (&m->pos[0])[k] = rdf(q + 4 * k); q += 36; }
        for (int i = 0; i < R->nadj; i++) { R->adj[i].idx = rd32(q); R->adj[i].cost = rdf(q + 4); q += 8; }
    }
    free(d); P->level = -1; P->seg = -1; P->trig_dist = 10000.f;
    return 0;
}

/* FUN_8c0d8238 (task init) */
void police_reset(Police *P) { P->flags = 0; P->trig = 0; P->cnt = P->cnt2 = 0; P->trig_dist = 10000.f; P->seg = -1; P->attack = 0; P->sp = 0; P->stack[0] = -1; }

/* FUN_8c0d8120: event 0x1f loads record idx, 0x20 clears (also drops the pursuit and shows level 5 = all clear) */
void police_route(Police *P, int idx)
{
    if (idx < 0 || idx >= P->nrec || P->rec[idx].npts == 0) { P->route = 0; P->flags &= ~2; P->seg = -1; police_level_set(P, 5); return; }
    P->route = &P->rec[idx]; printf("police: route %d (%d pts %d segs)\n", idx, P->route->npts, P->route->nsegs);
}

/* FUN_8c0d8080: the nearest reporting NPC of the tick wins (distance below the running minimum, reset to 10000 each tick) */
void police_report(Police *P, float dist, int a, int b)
{
    if (!P->route || (P->flags & 1)) return;
    if (dist < P->trig_dist) { P->trig_dist = dist; P->trig_a = a; P->trig_b = b; P->trig = 1; }
}

/* FUN_8c0d88d0: point inside the quad (XZ, triangles p0-p1-p2 and p3-p1-p2, any winding); y must lie in [p0.y, p1.y] when they differ */
static int cross_sign(float ax, float az, float bx, float bz) { float c = ax * bz - az * bx; return c < 0.f ? -1 : c > 0.f ? 1 : 0; }
static int in_tri(const float *p, const float *a, const float *b, const float *c)
{
    int s0 = cross_sign(a[0] - p[0], a[2] - p[2], b[0] - p[0], b[2] - p[2]);
    int s1 = cross_sign(b[0] - p[0], b[2] - p[2], c[0] - p[0], c[2] - p[2]);
    int s2 = cross_sign(c[0] - p[0], c[2] - p[2], a[0] - p[0], a[2] - p[2]);
    return (s0 <= 0 && s1 <= 0 && s2 <= 0) || (s0 >= 0 && s1 >= 0 && s2 >= 0);
}
static int in_quad(const float *p, const float *q)
{
    if (q[1] != q[4] && (p[1] < q[1] || q[4] < p[1])) return 0;
    return in_tri(p, q, q + 3, q + 6) || in_tri(p, q + 9, q + 3, q + 6);
}
/* FUN_8c0d8a54 (+ FUN_8c0d8bd8): the player's movement segment prev->cur crosses the line g0-g1 in XZ (y within [g0.y, g1.y] when they differ) */
static int cross_line(const float *cur, const float *prev, const float *g)
{
    if (g[1] != g[4] && (cur[1] < g[1] || g[4] < cur[1])) return 0;
    float ax = prev[0], az = prev[2], bx = cur[0], bz = cur[2], cx = g[0], cz = g[2], dx = g[3], dz = g[5];
    float d1 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax), d2 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
    float d3 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx), d4 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);
    return ((d1 < 0.f && d2 > 0.f) || (d1 > 0.f && d2 < 0.f)) && ((d3 < 0.f && d4 > 0.f) || (d3 > 0.f && d4 < 0.f));
}

/* FUN_8c0d90a8: the route point whose quad holds the player (within r2), else the area lookup (point + alternative) */
static void nearest_point(const Police *P, int *pt, int *alt)
{
    const PolRoute *R = P->route; *pt = -1; *alt = -1;
    for (int i = 0; i < R->npts; i++) {
        const PolPt *p = &R->pt[i]; float dx = P->pos[0] - p->pos[0], dz = P->pos[2] - p->pos[2];
        if (dx * dx + dz * dz > p->r2) continue;
        if (in_quad(P->pos, p->quad)) { *pt = i; return; }
    }
    for (int i = 0; i < R->nareas; i++) {
        const PolArea *a = &R->ar[i]; float dx = P->pos[0] - a->pos[0], dz = P->pos[2] - a->pos[2];
        if (dx * dx + dz * dz > a->r2) continue;
        if (in_quad(P->pos, a->quad)) { *pt = a->pt; *alt = a->pt2; return; }
    }
}

/* FUN_8c0d8d24: breadth-first search over the route graph from the player's point (start < 0) or a given point, stopping at the first
   point that begins a cordon segment; the stack then holds the path cordon point -> ... -> player point (levels of at most 16 nodes) */
static void path_build(Police *P, int start)
{
    const PolRoute *R = P->route; int n = R->npts;
    float cost[64]; short parent[64];
    for (int i = 0; i < n && i < 64; i++) { cost[i] = -1.f; parent[i] = -1; R->pt[i].seg = -1; }
    for (int j = 0; j < R->nsegs; j++) if (R->sg[j].start >= 0 && R->sg[j].start < n) R->pt[R->sg[j].start].seg = j;
    int a = start, b = -1;
    if (a < 0) nearest_point(P, &a, &b);
    if (a < 0 || a >= n) { P->stack[0] = -1; P->sp = 0; return; }
    signed char lvl[16][16]; int nl = 0, cur = 0;
    { float dx = P->pos[0] - R->pt[a].pos[0], dz = P->pos[2] - R->pt[a].pos[2]; cost[a] = sqrtf(dx * dx + dz * dz); lvl[0][nl++] = a; }
    if (b >= 0 && b < n) { float dx = P->pos[0] - R->pt[b].pos[0], dz = P->pos[2] - R->pt[b].pos[2]; cost[b] = sqrtf(dx * dx + dz * dz); lvl[0][nl++] = b; }
    lvl[0][nl] = -1;
    for (;;) {
        int found = -1, nn = 0, nxt = (cur + 1) & 15;
        for (int k = 0; lvl[cur][k] >= 0; k++) {
            int c = lvl[cur][k];
            if (R->pt[c].seg >= 0) { found = c; break; }
            for (int e = 0; e < R->pt[c].nadj; e++) {
                const PolAdj *A = &R->adj[R->pt[c].adj0 + e]; int m = A->idx; if (m < 0 || m >= n) continue;
                float nc = cost[c] + A->cost;
                if (cost[m] < 0.f || nc < cost[m]) { cost[m] = nc; parent[m] = c; if (nn < 15) lvl[nxt][nn++] = m; }
            }
        }
        if (found >= 0) {
            int k = 0; P->stack[k] = found;
            while (found != a && found != b && k < 14) { found = parent[found]; if (found < 0) break; P->stack[++k] = found; }
            if (found == a && b >= 0 && k < 14) P->stack[++k] = b;   /* the DC appends the alternative start after the main one */
            P->sp = k; return;
        }
        if (nn < 1 || nn > 15) { P->stack[0] = -1; P->sp = 0; return; }
        lvl[nxt][nn] = -1; cur = nxt;
    }
}

/* FUN_8c0d87dc: keep the stack in step with the player's current route point; out = next point toward the cordon; returns sp == 0 */
static int stack_update(Police *P, int *out)
{
    const PolRoute *R = P->route;
    if (P->sp < 0) path_build(P, -1);
    else {
        int i;
        for (i = 0; i < R->npts; i++) {
            const PolPt *p = &R->pt[i]; float dx = P->pos[0] - p->pos[0], dz = P->pos[2] - p->pos[2];
            if (dx * dx + dz * dz > p->r2) continue;
            if (in_quad(P->pos, p->quad)) break;
        }
        if (i != R->npts && i != P->stack[P->sp]) {
            if (P->sp == 0 || P->stack[P->sp - 1] != i) path_build(P, i); else P->sp--;
        }
    }
    *out = P->sp != 0 ? P->stack[P->sp - 1] : P->stack[0];
    return P->sp == 0;
}

/* task 0x5a: FUN_8c049b9c creates it at a position with a level; FUN_8c049d34 moves it; FUN_8c049c7c sets the level */
static void level_create(Police *P, const float *pos, int level)
{
    /* 0494d8 starts compact, not expanded. Preserve the shared source
       factor across task recreation; 049d9c resets it in compact mode. */
    float factor=P->arrow.factor;memset(&P->arrow,0,sizeof P->arrow);P->arrow.factor=factor;
    P->arrow_tick=-1;memcpy(P->marker,pos,12);P->level=level;P->lmotion=-1;P->arrow_frame=0;
    police_hud_select(level,&P->lmotion,&P->lstate,&P->lframe);P->lvis=0;
}
void police_level_set(Police *P, int level) { if ((unsigned)level >= 6 || P->level < 0) return; P->level = level; police_hud_select(level,&P->lmotion,&P->lstate,&P->lframe); }

void police_alert_place(Police *P, float x, float y, float z, int level) { if ((unsigned)level >= 6) return; float pos[3] = { x, y, z }; if (P->level < 0) level_create(P, pos, level); else { memcpy(P->marker, pos, 12); P->lmotion=-1;police_level_set(P,level); } }
void police_alert_kill(Police *P) { P->level = -1; }
void police_observe_cordon(Police *P,const float view[16],int fov,float (*sin_bam)(int))
{
    /* 0d84f0 writes 049d70 only on a visibility edge. Execution of the
       alert task consumes this on its following tick. */
    if(!P->route || (P->flags&10)!=10 || (unsigned)P->seg>=(unsigned)P->route->nsegs)return;
    const float *pos=P->route->sg[P->seg].pos;float v[3];
    for(int r=0;r<3;r++)v[r]=(float)((double)view[r*4]*pos[0]+(double)view[r*4+1]*pos[1]+(double)view[r*4+2]*pos[2]+view[r*4+3]);
    int visible=police_arrow_visible(v[0],v[1],-v[2],fov,sin_bam);
    if(visible && !(P->flags&4)){if(P->level>=0)P->lvis=3;P->flags|=4;}
    else if(!visible && (P->flags&4)){if(P->level>=0)P->lvis=2;P->flags&=~4;}
}

/* FUN_8c0d8688: 1 when the player is inside a cordon segment; otherwise places the alert marker on the next route point toward the cordon */
static int seg_test(Police *P)
{
    const PolRoute *R = P->route; int i;
    for (i = 0; i < R->nsegs; i++) {
        const PolSeg *s = &R->sg[i];
        int hit = s->kind == 0 ? in_quad(P->pos, s->geom) : cross_line(P->pos, P->prev, s->geom);
        if (hit) { P->seg = i; return 1; }
    }
    int local, at_end = stack_update(P, &local); const float *mpos;
    if (!at_end) {
        if (local < 0 || local >= R->npts) { P->flags &= ~8; return 0; }
        mpos = R->pt[local].pos; P->flags &= ~8;
    } else {
        int sg = local >= 0 && local < R->npts ? R->pt[local].seg : -1;
        if (sg < 0 || sg >= R->nsegs) { P->flags &= ~8; return 0; }
        P->seg = sg; mpos = R->sg[sg].pos; P->flags |= 8;
    }
    if (P->level != 0) { if (P->level == -1) level_create(P, mpos, 1); else memcpy(P->marker, mpos, 12); }
    return 0;
}

/* FUN_8c0d828c */
void police_tick(Police *P, float px, float py, float pz)
{
    memcpy(P->prev,P->pos,12);P->pos[0]=px;P->pos[1]=py;P->pos[2]=pz;
    /* The alert is a separate source task and keeps ticking after the
       route manager drops its route. */
    if (!P->route) {
        if(P->level>=0)police_hud_tick(P->level,&P->lmotion,&P->lstate,&P->lframe,&P->arrow_frame);
        return;
    }
    int lvl = P->level;
    if (!(P->flags & 1)) {
        if (!(P->flags & 2)) {
            if (P->trig) { P->flags |= 2; P->sp = -1; P->cnt = P->cnt2 = 0; seg_test(P); }
        } else {
            P->cnt++;
            if (P->cnt > 900 && !P->trig) { P->flags &= ~2; if (lvl != 0) police_level_set(P, 5); }
            else {
                int r = seg_test(P);
                if (!r) {
                    if (lvl == 0) P->cnt2 = 0;
                    else { P->cnt2++; if (P->cnt2 < 150) { if (lvl != 1) police_level_set(P, 1); } else if (lvl != 3) police_level_set(P, 3); }
                } else {
                    P->flags |= 1; P->flags &= ~2; P->cnt = P->cnt2 = 0; P->attack = 1;
                    if (lvl != 0) police_level_set(P, 2);
                }
            }
            if (++P->frame >= 120) P->frame = 0;
        }
        P->trig = 0; P->trig_dist = 10000.f;
    } else {
        P->cnt++;
        if (P->cnt > 150) {
            const PolSeg *s = P->seg >= 0 && P->seg < P->route->nsegs ? &P->route->sg[P->seg] : 0;
            if (lvl != 0) police_level_set(P, 5);
            int inside = s && s->kind == 0 ? in_quad(P->pos, s->geom) : 0;
            if (!inside) { P->flags &= ~1; P->attack = 0; }
        }
    }
    if(P->level>=0)police_hud_tick(P->level,&P->lmotion,&P->lstate,&P->lframe,&P->arrow_frame);
    if (P->level != P->log_level || (P->flags & 3) != P->log_flags) { P->log_flags = P->flags & 3; printf("police: level %d flags %x seg %d sp %d attack %d marker %.0f,%.0f,%.0f stack %d,%d,%d\n", P->level, P->flags, P->seg, P->sp, P->attack, P->marker[0], P->marker[1], P->marker[2], P->stack[0], P->stack[1], P->stack[2]); P->log_level = P->level; }
}
