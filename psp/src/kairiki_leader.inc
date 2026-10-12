/* 08ba30 / 1b99c0: Kairiki's route owner stops advancing during a blow.
   Navigation uses the same recovered graph/link decoder as 071260. */
static void kairiki_leader_tick(Gang *g,const Graphs *graphs,const float *player,float player_speed,int off_graph,int disabled)
{
    if((g->flags&0x10) && g->state<8){for(int i=0;i<g->nmem;i++)g->m[i].flags|=0x10;g->state=8;}
    else if((g->flags&2) && g->state<8)g->state=5;
    if(g->state<8){memcpy(g->target,player,12);g->target_player=1;}
    GangCombat *c=g->combat;
    /* 06e238 rejects grind, damage, tagging and transition states. */
    int eligible=!c || (c->player_state!=3 && c->player_state!=4 && c->player_state!=7 && c->player_state!=8 && c->player_state!=10 && c->player_state!=11 && (c->player_state!=2 || c->grip_eligible));
    switch(g->state) {
    case 0:g->state=1;g->flags=0x101;g->cnt=g->timer=0;g->route_brake=g->par[12];break;
    case 1:
        g->flags&=~0x100;
        if(!disabled && gang_triggered(g,player)){g->flags&=~1;g->state=2;if(c && c->sound)c->sound(c->context,0x60000,g->pos);}
        break;
    case 2:
        g->timer=0;
        if(off_graph || (g->node=graph_node_at(g->graph,g->pos))<0)decelerate(g);
        else g->state=plan_route(g,graphs)==0?4:3;
        break;
    case 3:
        if(!disabled && !off_graph && eligible && gang_triggered(g,player)){g->flags|=2;g->state=5;break;}
        if(g->timer++>(int)g->par[2])g->state=2;
        if(!follow_target(g)){decelerate(g);g->state=2;}
        else {accelerate(g,g->par[3]);g->cnt++;}
        break;
    case 4:
        if(!disabled && eligible && gang_triggered(g,player)){g->flags|=2;g->state=5;break;}
        g->cnt++;
        if(riot_approach(g,player_speed)<0){g->state=2;g->flags&=~0x40;}
        break;
    case 5:
        if(!(g->flags&2))g->state=6;else g->flags|=0x20000;
        break;
    case 6:g->cnt=0;g->flags&=~0x20800;g->state=2;break;
    case 7:
        riot_approach(g,player_speed);
        if(g->cnt++>=(int)g->par[1]){g->flags&=~0x10000;g->cnt=0;g->state=g->saved;}
        break;
    case 8:if(!plan_departure(g,graphs,player))g->flags|=0x20;g->state=9;break;
    case 9:
        g->speed=fminf(g->speed+2*g->par[6],2*g->par[3]);
        if(!follow_target(g)){g->speed=0;g->flags|=0x20;}break;
    }
    if((g->state==3 || g->state==4) && g->cnt>=(int)g->par[0]){g->flags|=0x10000;g->saved=g->state;g->state=7;g->cnt=0;}
}
