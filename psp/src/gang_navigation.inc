/* Shared ground navigation; no decoded game data. Included by gang.c. */
int graph_node_at(const GGraph *g, const float *pos)
{
    if (!g) return -1;
    float p[3] = { pos[0], pos[1], pos[2] };
    if (g->has_xf) {   /* FUN_8c22f2e0: the graph carries a matrix, the position is tested in its local frame */
        const float *m = g->xf; float x = pos[0] - m[3], y = pos[1] - m[7], z = pos[2] - m[11];
        p[0] = m[0] * x + m[4] * y + m[8] * z; p[1] = m[1] * x + m[5] * y + m[9] * z; p[2] = m[2] * x + m[6] * y + m[10] * z;
    }
    for (int i = 0; i < g->n; i++) {
        const GNode *nd = &g->nodes[i];
        if (nd->min[0] <= p[0] && p[0] < nd->max[0] && nd->min[1] <= p[1] && p[1] < nd->max[1] && nd->min[2] <= p[2] && p[2] < nd->max[2]) return i;
    }
    return -1;
}
/* path descriptor chain (rec +0x90: {node, waypoint*, flag, next*}; 0x8c2c54f0 marks the head) */
#define PATH_MAX 16
static int path_search(const Graphs *G, const GGraph *g, const float *target, int cur, int prev, int dst, int *out, int depth)
{
    /* FUN_8c06e520: from `cur` try the links (skipping `prev`); a link to `dst` ends the path; otherwise the three links nearest to the
       target (by their entry points) are tried depth-first in order; cycles through nodes already on the path are skipped */
    if (depth >= PATH_MAX - 1) return 0;
    const GNode *nd = &g->nodes[cur]; float best[3] = { -1.f, -1.f, -1.f }; int bnode[3] = { -1, -1, -1 };
    for (int k = 0; k < nd->nlinks; k++) {
        int ln = G->link[nd->link0 + k]; if (ln == prev || ln < 0 || ln >= g->n) continue;
        if (ln == dst) { out[depth] = dst; out[depth + 1] = -1; return 1; }
        int onpath = 0; for (int j = 0; j < depth; j++) if (out[j] == ln) onpath = 1; if (onpath) continue;
        const float *lp = &G->lpos[3 * (nd->link0 + k)];
        float d = (target[0] - lp[0]) * (target[0] - lp[0]) + (target[1] - lp[1]) * (target[1] - lp[1]) + (target[2] - lp[2]) * (target[2] - lp[2]);
        for (int i = 0; i < 3; i++) { if (best[i] < 0.f || d < best[i]) { for (int j = 2; j > i; j--) { best[j] = best[j - 1]; bnode[j] = bnode[j - 1]; } best[i] = d; bnode[i] = ln; break; } }
    }
    for (int i = 0; i < 3; i++) {
        if (bnode[i] < 0) return 0;
        out[depth] = bnode[i];
        if (path_search(G, g, target, bnode[i], cur, dst, out, depth + 1)) return 1;
    }
    return 0;
}
/* 06e520/06e7f4 keep both the node centre and the authored link portal.
   The final centre is a sentinel for 071f40: direct approach takes over
   once its entry portal is reached. Other route controllers keep their
   existing cursor representation. */
static int formation_route(const Gang *g)
{return g->kind==3 || g->kind==4 || g->kind==6 || g->kind==9 || g->kind==25;}
static const float *route_portal(const Gang *g)
{
    if(!g->routes || !g->graph || g->node<0 || g->node>=g->graph->n || g->pathi>=g->npath)return NULL;
    const GNode *node=g->graph->nodes+g->node;
    for(int k=0;k<node->nlinks;k++) {
        int at=node->link0+k;
        if(at>=0 && at<g->routes->nlinks && g->routes->link[at]==g->path[g->pathi])return g->routes->lpos+3*at;
    }
    return NULL;
}
static void route_begin(Gang *g,const Graphs *routes)
{
    g->routes=routes;g->route_phase=0;
    if(!formation_route(g))return;
    const float *portal=route_portal(g);
    if(portal) {
        const float *centre=g->graph->nodes[g->node].pos;float dot=0;
        for(int k=0;k<3;k++)dot+=(centre[k]-g->pos[k])*(portal[k]-g->pos[k]);
        if(dot<0)g->route_phase=1; /* already beyond the first centre */
    }
}
/* FUN_8c06e36c: 0 = the target is in the group's node (go straight), 1 = a path to the target's node was built (FUN_8c06e7f4), 2 = target off the graph */
static int plan_route(Gang *g, const Graphs *G)
{
    int tn = graph_node_at(g->graph, g->target);
    if (tn < 0) { g->npath = 0; return 2; }
    if (tn == g->node) { g->npath = 0; return 0; }
    if (g->node < 0) { g->npath = 0; return 2; }
    int path[PATH_MAX]; int ok = path_search(G, g->graph, g->target, g->node, -1, tn, path, 0);
    g->npath = 0;
    if (ok) { for (int i = 0; i < PATH_MAX && path[i] >= 0; i++) g->path[g->npath++] = path[i]; }
    else { g->path[0] = g->node; g->npath = 1; }
    g->pathi = 0; route_begin(g,G); return 1;
}
/* 06f1d0: departure tries the three links farthest from the player,
   looking three nodes ahead, skipping only the immediately previous node. */
static int departure_search(const Graphs *G,const GGraph *graph,const float *player,int cur,int prev,int *out,int depth)
{
    if(depth>=3)return 1;
    if(!G || !graph || cur<0 || cur>=graph->n)return 0;
    const GNode *node=graph->nodes+cur;float best[3]={-1,-1,-1};int links[3]={-1,-1,-1};
    for(int k=0;k<node->nlinks;k++) {
        int at=node->link0+k;if(at<0 || at>=G->nlinks)continue;
        int next=G->link[at];if(next==prev || next<0 || next>=graph->n)continue;
        const float *p=G->lpos+at*3;float d=0;
        for(int j=0;j<3;j++){float v=p[j]-player[j];d+=v*v;}
        for(int j=0;j<3;j++)if(best[j]<0 || d>best[j]) {
            for(int n=2;n>j;n--){best[n]=best[n-1];links[n]=links[n-1];}
            best[j]=d;links[j]=next;break;
        }
    }
    for(int k=0;k<3 && links[k]>=0;k++) {
        out[depth]=links[k];if(departure_search(G,graph,player,links[k],cur,out,depth+1))return 1;
    }
    return 0;
}
static int plan_departure(Gang *g,const Graphs *G,const float *player)
{
    float d=0;for(int j=0;j<3;j++){float v=g->pos[j]-player[j];d+=v*v;}
    g->npath=g->pathi=0;
    if((g->flags&0x100) || d>g->par[16]*g->par[16] || !g->graph)return 0;
    g->node=graph_node_at(g->graph,g->pos);
    if(!departure_search(G,g->graph,player,g->node,-1,g->path,0))return 0;
    g->npath=3;g->target_player=0;memcpy(g->target,g->graph->nodes[g->path[2]].pos,12);
    g->flags&=~0x12;route_begin(g,G);return 1;
}
/* the point the group drives to: the next path node's waypoint (node +0x18), or the target itself when in the same node */
static void route_point(const Gang *g, float *pt)
{
    if (g->npath > 0 && g->pathi < g->npath) {
        const float *portal=formation_route(g) && g->route_phase?route_portal(g):NULL;
        int node=formation_route(g)?g->node:g->path[g->pathi];
        memcpy(pt,portal?portal:g->graph->nodes[node].pos,12);
    } else memcpy(pt,g->target,12);
}
/* FUN_8c06da4c: turn the heading toward the target direction by at most `maxturn` BAM per tick */
static void steer(float *dir, const float *delta, int maxturn)
{
    float ty = atan2f(delta[0], delta[2]), cy = atan2f(dir[0], dir[2]), d = ty - cy;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    float mx = maxturn * (6.2831853f / 65536.f); if (d > mx) d = mx; if (d < -mx) d = -mx;
    cy += d; dir[0] = sinf(cy); dir[2] = cosf(cy);
}
/* FUN_8c071f40: steer toward the current route point and advance by the speed; returns 0 when the route point is reached
   (the speed law of FUN_8c0720dc is measured from the Flycast gang trace, see NOTES) */
static int follow_target(Gang *g)
{
    if(formation_route(g) && !g->graph)return 0;
    float pt[3]; route_point(g, pt);
    float delta[3] = { pt[0] - g->pos[0], 0.f, pt[2] - g->pos[2] }; float d2 = delta[0] * delta[0] + delta[2] * delta[2];
    if(g->type==7 || (g->kind==3 || g->kind==4 || g->kind==6 || g->kind==9 || g->kind==25) || g->kind==21) {
        /* 0720dc/0723bc: source BAM turn and graph-contained candidate.
           The route proxy is not a wall capsule; members own wall contact. */
        int a[3]={0,(int)lrintf(atan2f(g->dir[0],g->dir[2])*10430.378350470453f)&65535,0};
        int wanted[3]={0,a[1],0};heli_vector_angles(delta,wanted);
        a[1]=heli_turn(a[1],wanted[1],(int)g->par[P_MAXTURN]);
        g->dir[0]=heli_sin(a[1]);g->dir[2]=heli_cos(a[1]);
        float next[3]={g->pos[0]+g->dir[0]*g->speed,g->pos[1],g->pos[2]+g->dir[2]*g->speed};
        if(g->state==5 || g->state==7) {
            if(graph_node_at(g->graph,next)<0)return 0;
        }
    } else steer(g->dir, delta, (int)g->par[P_MAXTURN]);
    if (g->type!=7 && g->kind!=3 && g->kind!=4 && g->kind!=6 && g->kind!=9 && g->kind!=25 && g->kind!=21 && gang_wall) { float a[3] = { g->pos[0], g->pos[1] + 6.f, g->pos[2] }, b[3] = { g->pos[0] + g->dir[0] * 8.f, g->pos[1] + 6.f, g->pos[2] + g->dir[2] * 8.f }; if (gang_wall(g_wall_ctx, a, b)) { float y = atan2f(g->dir[0], g->dir[2]) + 0.5f; g->dir[0] = sinf(y); g->dir[2] = cosf(y); } }
    g->pos[0] += g->dir[0] * g->speed; g->pos[2] += g->dir[2] * g->speed;
    if(formation_route(g))g->pos[1]+=g->vy;
    if (g->npath > 0 && g->pathi < g->npath) {
        int here = graph_node_at(g->graph, g->pos);
        if(formation_route(g)) {
            if(here>=0)memcpy(g->lastnode,g->pos,12);
            if(d2<=g->speed*g->speed) {
                if(g->route_phase || !route_portal(g)) {
                    g->node=g->path[g->pathi++];g->route_phase=0;
                } else g->route_phase=1;
            }
            return g->pathi<g->npath;
        }
        if (here >= 0) memcpy(g->lastnode, g->graph->nodes[here].pos, 12);
        if (g->kind==21 ? d2<=g->speed*g->speed : (here == g->path[g->pathi] || d2 < 6.f * 6.f)) { g->pathi++; }
        return g->pathi < g->npath;
    }
    return d2 > 4.f * 4.f;
}
static void accelerate(Gang *g, float limit) { if (g->speed < limit) { g->speed += g->par[P_ACC]; if (g->speed > limit) g->speed = limit; } }
static void decelerate(Gang *g) { if (g->speed > 0.f) { g->speed -= g->par[P_DEC]; if (g->speed < 0.f) g->speed = 0.f; } }

static float riot_approach(Gang *g,float player_speed)
{
    /* 0720dc: approach the linked tail/player, preserving the heading when
       the next point is outside the navigation graph. This is independent
       of the patrol waypoint cursor (071f40). */
    const float *p=g->par;
    float delta[3]={g->target[0]-g->pos[0],0,g->target[2]-g->pos[2]};
    float distance=hypotf(delta[0],delta[2]);
    float speed=g->formation_lead?g->formation_lead->speed:fabsf(player_speed);
    if(g->flags&0x10000)decelerate(g);
    else if(distance<p[g->formation_lead?10:11]) {
        if(distance<g->route_brake){g->route_brake=p[13];g->speed=fminf(distance,fminf(speed,p[4]));}
        else {g->route_brake=p[12];if(g->speed>speed)g->speed=fmaxf(speed,g->speed-p[7]);}
    } else {g->route_brake=p[12];accelerate(g,p[g->flags&2?4:3]);}
    int a[3]={0,(int)lrintf(atan2f(g->dir[0],g->dir[2])*10430.378350470453f)&65535,0},wanted[3]={0,a[1],0};
    heli_vector_angles(delta,wanted);a[1]=heli_turn(a[1],wanted[1],(int)p[21]);
    float sx=heli_sin(a[1]),cz=heli_cos(a[1]);
    float next[3]={g->pos[0]+sx*g->speed,g->pos[1]+g->vy,g->pos[2]+cz*g->speed};
    int node=graph_node_at(g->graph,next);if(node<0)return -1.f;
    memcpy(g->lastnode,g->pos,12);memcpy(g->pos,next,12);g->node=node;
    g->dir[0]=sx;g->dir[2]=cz;
    return distance-g->speed;
}
