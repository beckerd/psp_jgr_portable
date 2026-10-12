/* Native state selectors retain their existing dispatch meaning; raw clip
 * indices, frame counts, anchors and tuning all come from the user's disc. */
static int controller_clips(const TableRecipe *r,unsigned char *data,unsigned section,
                            const signed char map[64],const int *slots,unsigned count,unsigned width)
{
    unsigned i;const TableSection *s;
    if(section>=r->section_count)return -1;
    s=r->sections+section;if(s->size!=count*width)return -1;
    for(i=0;i<count;i++) {
        if(slots[i]>=64 || slots[i]<-1)return -1;
        put(data+s->offset+i*width,slots[i]<0 ? UINT32_MAX : (uint32_t)(int)map[slots[i]],width);
    }
    return 0;
}
static int controller_frames(const TableRecipe *r,unsigned char *data,unsigned section,
                             const signed char frames[128],unsigned count)
{
    if(section>=r->section_count || count>128 || r->sections[section].size!=count)return -1;
    memcpy(data+r->sections[section].offset,frames,count);return 0;
}
static int controller_anchors(JgrDisc *disc,const JgrDiscFile *exe,uint32_t table,
                              unsigned char *out,unsigned count)
{
    unsigned written=0,record;
    for(record=0;written<count && record<count;record++) {
        uint32_t descriptor,pointer;unsigned entries,j;
        if(table_word(disc,exe,0x8c010000u,table+record*12,&descriptor) ||
           table_word(disc,exe,0x8c010000u,table+record*12+4,&pointer))return -1;
        entries=descriptor>>16;if(!entries || entries>count-written)return -1;
        for(j=0;j<entries;j++) {
            uint64_t at=(uint64_t)pointer+j*12;
            if(at>UINT32_MAX || table_bytes(disc,exe,0x8c010000u,(uint32_t)at,out+written*16+4,12))return -1;
            put(out+written*16,descriptor&65535,4);written++;
        }
    }
    return written==count ? 0 : -1;
}
static int populate_controller(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    static const int states[]={11,23,8,20,12,13,14,15,9,0,18,19,5,16,17,4,21,21,19,16};
    static const int gait[]={7},front[]={22};
    signed char map[64],frames[128];unsigned count,family;
    if(r->special==23)family=TABLE_ENEMY_KILLER;
    else if(r->special==24)family=TABLE_ENEMY_KIDOUTAI;
    else if(r->special==25)family=TABLE_ENEMY_PARA;
    else if(r->special==26)family=TABLE_ENEMY_BOMB;
    else if(r->special==27)family=TABLE_ENEMY_FIRE;
    else if(r->special==28)family=TABLE_ENEMY_TABAN;
    else return -1;
    if(enemy_map_count(disc,family,map,frames,&count))return -1;
    if(r->special==23) {
        if(map[11]<0)map[11]=map[9];
        return controller_clips(r,data,1,map,states,20,1) || controller_clips(r,data,2,map,gait,1,4) ||
               controller_clips(r,data,3,map,front,1,4) || controller_frames(r,data,4,frames,count) ? -1 : 0;
    }
    if(r->special==24) {
        int full[23];static const int gas[]={27,29,31},turn[]={34,35};
        memcpy(full,states,sizeof states);full[20]=26;full[21]=28;full[22]=30;
        return controller_clips(r,data,1,map,full,23,1) || controller_clips(r,data,2,map,gait,1,4) ||
               controller_clips(r,data,3,map,front,1,4) || controller_clips(r,data,4,map,gas,3,1) ||
               controller_clips(r,data,5,map,turn,2,1) || controller_frames(r,data,6,frames,count) ? -1 : 0;
    }
    if(r->special==25) {
        int full[25];unsigned i;
        memcpy(full,states,sizeof states);full[8]=25;full[9]=25;for(i=20;i<25;i++)full[i]=(int)i+5;
        return controller_clips(r,data,1,map,full,25,1) || controller_clips(r,data,2,map,gait,1,4) ||
               controller_frames(r,data,3,frames,count) ? -1 : 0;
    }
    if(r->special==26) {
        int full[22];JgrDiscFile bank;uint32_t pointer;double length;
        memcpy(full,states,sizeof states);full[20]=-1;full[21]=3;
        if(controller_clips(r,data,4,map,full,22,1) || controller_clips(r,data,5,map,gait,1,4) ||
           controller_clips(r,data,6,map,front,1,4) || controller_frames(r,data,7,frames,count) ||
           r->section_count!=9 || r->sections[8].size!=4 ||
           jgr_disc_source(disc,enemy_recipes[family].source,&bank) ||
           table_word(disc,exe,0x8c010000u,0x8c1b5dd4u,&pointer) || pointer>UINT32_MAX-2 ||
           read_value(disc,&bank,ENEMY_BASE,pointer+2,'H',&length))return -1;
        put(data+r->sections[8].offset,(uint32_t)length,4);return 0;
    }
    if(r->special==27) {
        int full[24];static const int aim[]={28,31,29,30};
        memcpy(full,states,sizeof states);full[20]=25;full[21]=26;full[22]=27;full[23]=3;
        if(map[11]<0)map[11]=map[9];
        if(controller_clips(r,data,3,map,full,24,1) || controller_clips(r,data,4,map,gait,1,4) ||
           controller_frames(r,data,5,frames,count) || controller_clips(r,data,6,map,front,1,4) ||
           controller_clips(r,data,7,map,aim,4,4) || enemy_map_count(disc,TABLE_ENEMY_KAIRIKI,map,frames,&count))return -1;
        if(map[11]<0)map[11]=map[9];
        if(controller_clips(r,data,8,map,full,22,1) || controller_clips(r,data,9,map,gait,1,4) ||
           controller_frames(r,data,10,frames,count) || r->section_count!=15 ||
           r->sections[11].size!=32 || r->sections[12].size!=48 ||
           controller_anchors(disc,exe,0x8c1b8964u,data+r->sections[11].offset,2) ||
           controller_anchors(disc,exe,0x8c1b9c04u,data+r->sections[12].offset,3))return -1;
        return 0;
    }
    if(r->special==28) {
        static const int taban[]={15,30,30,29,0,15,15,6,7,8,1,2};
        return controller_clips(r,data,1,map,taban,12,1) || controller_frames(r,data,2,frames,count) ? -1 : 0;
    }
    return -1;
}
