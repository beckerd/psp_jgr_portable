/* Completed original staff performances. The existing STAFF parent still
   owns row lifetimes and cards; movement/camera/pose do not re-run physics. */
#define STAFF_ROUTE_META_LIMIT (128u*1024u)
static void staff_route_free(StaffRoute *r)
{
    title_route_free(&r->route);
    free(r->metadata);
    memset(r,0,sizeof *r);
}
static unsigned staff_route_word(const unsigned char *p)
{unsigned value;memcpy(&value,p,4);return value;}
static int staff_route_load(StaffRoute *out,const char *path,const char *meta,unsigned character)
{
    StaffRoute r={0};int error=title_route_load(&r.route,path);
    if(error)return error;
    FILE *f=fopen(meta,"rb");
    if(!f){staff_route_free(&r);return -1;}
    unsigned char header[32];long length=0;error=-2;
    if(fread(header,1,sizeof header,f)!=sizeof header || memcmp(header,"SRT1",4) ||
       staff_route_word(header+4)!=1 || staff_route_word(header+8)!=character ||
       r.route.character!=character || staff_route_word(header+12)!=r.route.count ||
       staff_route_word(header+16)!=9 || staff_route_word(header+28))goto fail;
    r.nspray=staff_route_word(header+20);r.ngraffiti=staff_route_word(header+24);
    if(r.nspray<9 || r.nspray>r.route.count || r.ngraffiti>STAFF_ROUTE_META_LIMIT/32 ||
       r.nspray>STAFF_ROUTE_META_LIMIT/32)goto fail;
    unsigned size=32+9*sizeof(StaffRouteSegment)+r.nspray*sizeof(StaffRouteSpray)+r.ngraffiti*sizeof(StaffRouteGraffiti);
    if(size>STAFF_ROUTE_META_LIMIT || fseek(f,0,SEEK_END) || (length=ftell(f))!=(long)size || fseek(f,0,SEEK_SET))goto fail;
    r.metadata=malloc(size);if(!r.metadata){error=-3;goto fail;}
    if(fread(r.metadata,1,size,f)!=size)goto fail;
    r.segments=(const StaffRouteSegment *)(r.metadata+32);
    r.sprays=(const StaffRouteSpray *)(r.segments+9);
    r.graffiti=(const StaffRouteGraffiti *)(r.sprays+r.nspray);
    unsigned end=0;
    for(unsigned i=0;i<9;i++) {
        const StaffRouteSegment *s=r.segments+i;
        if(s->row!=i+2 || s->first_frame!=end || !s->count || s->count>r.route.count-end ||
           s->replay_tick0>8 || !s->player_tick0 || s->player_tick0>8 || s->reserved)goto fail;
        end+=s->count;
        TitleSample sample;
        if(!title_route_sample(&r.route,s->first_frame,0,&sample) || sample.part!=i || sample.tick!=s->player_tick0 ||
           !title_route_sample(&r.route,end-1,0,&sample) || sample.part!=i || sample.tick!=s->player_tick0+s->count-1)goto fail;
    }
    if(end!=r.route.count)goto fail;
    unsigned starts=0;
    for(unsigned i=0;i<r.nspray;i++) {
        const StaffRouteSpray *s=r.sprays+i;
        if(s->frame>=end || (i && s->frame<=r.sprays[i-1].frame) || s->active>1 || s->variant>1 ||
           s->ref>65535 || !isfinite(s->pose_frame))goto fail;
        for(unsigned k=0;k<3;k++)if(!isfinite(s->target[k]))goto fail;
        if(starts<9 && s->frame==r.segments[starts].first_frame)starts++;
    }
    if(starts!=9)goto fail;
    for(unsigned i=0;i<r.ngraffiti;i++) {
        const StaffRouteGraffiti *g=r.graffiti+i;
        if(g->frame>=end || g->area>65535 || g->index>65535 || g->complete>1 || g->started>1 || g->revealing>2)goto fail;
        if(i) {
            const StaffRouteGraffiti *p=g-1;
            if(g->frame<p->frame || (g->frame==p->frame &&
               (g->area<p->area || (g->area==p->area && g->index<=p->index))))goto fail;
        }
    }
    fclose(f);staff_route_free(out);*out=r;return 0;
fail:
    fclose(f);staff_route_free(&r);return error;
}
static int staff_route_sample(StaffRoute *r,unsigned row,unsigned tick,TitleSample *sample)
{
    if(!r->metadata || row<2 || row>10)return 0;
    const StaffRouteSegment *s=r->segments+row-2;
    /* A source callback that precedes the first rendered sample holds its
       first pose. The exporter records this offset explicitly; never infer
       it from drive latency or interpolate across task destruction. */
    unsigned local=tick>s->replay_tick0 ? tick-s->replay_tick0:0;
    if(local>=s->count)local=s->count-1;
    unsigned frame=s->first_frame+local;
    if(r->have_frame && frame<r->frame)return 0;
    if(!title_route_sample(&r->route,frame,0,sample) || sample->part!=row-2 || sample->tick!=s->player_tick0+local)return 0;
    r->frame=frame;r->have_frame=1;
    while(r->next_spray<r->nspray && r->sprays[r->next_spray].frame<=frame)
        r->spray=r->sprays+r->next_spray++;
    return r->spray!=NULL;
}
