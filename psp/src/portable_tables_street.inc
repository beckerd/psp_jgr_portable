/* Native rigid-prop dispatch is code; every placement, collision vertex,
 * animation key and model-child pose is read from the supported source. */
static int table_prop_kind(unsigned stage,unsigned bank,unsigned type,unsigned *reaction,int *sound)
{
    *reaction=stage==1||stage==2?0:1;*sound=31;
    if(stage==1){switch(type){case 4:case 5:case 10:case 12:case 13:case 14:case 15:case 16:case 17:case 18:case 19:case 20:case 21:case 22:case 27:break;default:return 0;}
        *sound=(type==5||(type>=16&&type<=19))?-8:type==12?31:type==14?33:type==15?28:type==22?27:32;return 1;}
    if(stage==2){switch(type){
        case 2:case 6:case 10:case 11:case 13:case 14:case 16:case 22:case 23:case 24:case 25:case 26:case 27:case 28:case 29:case 34:case 43:case 61:case 64:case 66:case 67:*sound=type>=23&&type<=26?30:type==61?28:type==67?27:31;return 1;
        case 3:case 12:case 15:case 20:case 21:case 35:case 36:case 62:case 63:case 65:case 70:case 88:case 89:case 90:*reaction=2;*sound=type==3||type==12||type==20||type==21||type==90?27:31;return 1;
        case 59:case 60:*reaction=5;return 1;
        case 37:case 38:case 39:case 40:case 41:*reaction=6;*sound=28;return 1;
        case 30:*reaction=7;*sound=28;return 1;default:return 0;}}
    if(stage==3){if(bank){*sound=0;*reaction=4;return type<=2;}if(type>=51&&type<=53){*sound=28;*reaction=3;return 1;}
        switch(type){case 1:case 2:case 3:case 4:case 5:case 9:case 10:case 12:case 13:case 14:case 15:case 16:case 17:case 18:case 19:case 20:case 21:case 24:case 25:case 26:case 27:case 28:case 29:case 30:case 31:case 35:case 36:case 37:case 38:case 39:case 40:case 41:case 42:case 43:case 45:case 49:case 54:case 55:case 56:case 67:case 68:case 69:break;default:return 0;}
        if(type==3||type==14||(type>=25&&type<=31)||(type>=40&&type<=42)||type==49||(type>=54&&type<=56))*sound=28;
        else if(type==13||type==21)*sound=27;else if(type==16||type==17)*sound=32;else if(type==35)*sound=30;else if(type>=37&&type<=39)*sound=42;
        return 1;}
    if(stage==5){switch(type){case 0:case 2:case 3:case 4:case 6:case 7:case 8:case 9:case 10:case 11:case 14:case 15:case 17:case 18:case 19:break;default:return 0;}
        *sound=type==0||type==15?28:type==2||type==3?31:type==4||type==17?32:type==14?34:27;return 1;}
    return 0;
}
static int table_pointer_fixup(const TableRecipe *r,unsigned char *data,unsigned number,unsigned section,unsigned offset,unsigned target,unsigned target_offset)
{
    if(number>=r->fixups||section>=r->section_count||offset>r->sections[section].size||4>r->sections[section].size-offset||target>=r->section_count||target_offset>=r->sections[target].size)return -1;
    unsigned char *out=data+40+16*r->section_count+number*16;put(out,section,4);put(out+4,offset,4);put(out+8,target,4);put(out+12,target_offset,4);return 0;
}
static int table_prop_animations(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    static const uint32_t source_motions[]={0x8cda1164,0x8cda1498,0x8cda1934,0x8c107d28};
    for(unsigned i=0;i<4;i++){
        TableStage s;uint32_t root,child,motion=source_motions[i],head,keys;unsigned frames=r->sections[i].size/2;
        if(r->sections[i].size%2||ts_open(&s,disc,exe,i==3?2:3)||ts_word(&s,(i==3?0x8c109d2c+30*4:0x8c1bc064+(51+i)*4),&root)||ts_word(&s,root+44,&child)||!child)return -1;
        unsigned char *out=data+r->sections[4].offset+i*48;
        if(ts_bytes(&s,child+8,out+8,36)||table_pointer_fixup(r,data,i,4,i*48+4,i,0))return -1;
        put(out,frames,4);put(out+44,i==3?2:1,4);
        if(i==3){uint32_t declared;if(ts_word(&s,motion,&motion)||ts_word(&s,motion,&head)||ts_word(&s,motion+4,&declared)||declared!=frames||ts_word(&s,head+20,&keys))return -1;
            for(unsigned j=0;j<frames;j++){uint32_t tick,angle;if(ts_word(&s,keys+j*16,&tick)||tick!=j||ts_word(&s,keys+j*16+12,&angle))return -1;put(data+r->sections[i].offset+j*2,angle,2);}}
        else {uint32_t declared;if(ts_word(&s,motion,&declared)||declared>>16!=frames||ts_word(&s,motion+4,&head)||ts_word(&s,head+24,&keys))return -1;for(unsigned j=0;j<frames;j++){unsigned char angle[2];if(ts_bytes(&s,keys+j*6+2,angle,2))return -1;memcpy(data+r->sections[i].offset+j*2,angle,2);}}
    }
    return 0;
}
static int populate_street_props(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    static const struct {unsigned stage,bank,sectors,stride,types;uint32_t table,colliders;} specs[]={
      {1,0,63,40,28,0x8c10618c,0x8c107644},{2,0,114,40,91,0x8c108284,0x8c10a2dc},
      {3,0,46,52,70,0x8c1ba918,0x8c1bc294},{3,1,46,52,7,0x8c1baa88,0x8c1bc6a0},
      {5,0,71,52,20,0x8c203298,0x8c204084}};
    int audio=r->special==61;unsigned boxes[4]={0},placements[4]={0};
    if(audio){if(r->section_count!=2||r->sections[0].size!=56||r->sections[1].size!=28)return -1;}
    else if(r->section_count!=14||r->fixups!=12||r->sections[4].size!=192||r->sections[13].size!=96||table_prop_animations(disc,exe,r,data))return -1;
    for(unsigned bank=0;bank<5;bank++){
        unsigned stage=specs[bank].stage,si=stage==1?0:stage==2?1:stage==3?2:3;TableStage s;TablePlacement *rows=NULL;unsigned count=0;
        if(ts_open(&s,disc,exe,stage)||table_placements(&s,specs[bank].table,specs[bank].sectors,specs[bank].stride,specs[bank].types,&rows,&count))return -1;
        unsigned first[128]={0},nboxes[128]={0};int bad=0;
        if(!audio)for(unsigned type=0;type<specs[bank].types&&!bad;type++){
            unsigned reaction;int sound;if(!table_prop_kind(stage,specs[bank].bank,type,&reaction,&sound))continue;unsigned found=0;for(unsigned j=0;j<count;j++)found|=rows[j].type==type;if(!found)continue;
            uint32_t root;TableCollider nodes[64];unsigned n;if(ts_word(&s,specs[bank].colliders+type*4,&root)||table_colliders(&s,root,nodes,&n)){bad=1;break;}const TableSection *section=r->sections+5+si*2;if(section->size%172||n>section->size/172-boxes[si]){bad=1;break;}first[type]=boxes[si];nboxes[type]=n;
            for(unsigned j=0;j<n;j++){
                TableCollider *node=nodes+j;unsigned char *out=data+section->offset+boxes[si]++*172;
                if((node->kind!=2&&node->kind!=4)||node->flags!=25||ts_bytes(&s,node->address+48,out+96,12)||ts_bytes(&s,node->address+12,out+128,36)||ts_bytes(&s,node->mesh,out+(node->kind==2?112:0),node->kind==2?16:96)){bad=1;break;}
                put(out+108,node->flags,4);put(out+164,(uint32_t)node->parent,4);put(out+168,node->kind,4);
            }
        }
        for(unsigned j=0;j<count&&!bad;j++){
            TablePlacement *p=rows+j;unsigned reaction;int sound;if(!table_prop_kind(stage,specs[bank].bank,p->type,&reaction,&sound))continue;
            if(audio){if(sound>0){if(sound>=64){bad=1;break;}unsigned offset=r->sections[0].offset+stage*8+((unsigned)sound/32)*4;put(data+offset,word(data+offset)|(1u<<((unsigned)sound%32)),4);}else if(sound<0){if(sound<=-32){bad=1;break;}unsigned offset=r->sections[1].offset+stage*4;put(data+offset,word(data+offset)|(1u<<(unsigned)-sound),4);}continue;}
            const TableSection *section=r->sections+6+si*2;if(section->size%68||placements[si]>=section->size/68||!nboxes[p->type]){bad=1;break;}unsigned char *out=data+section->offset+placements[si]++*68;
            unsigned values[7]={p->address,p->type,p->sector,first[p->type],nboxes[p->type],reaction,(unsigned)sound};for(unsigned k=0;k<7;k++)put(out+k*4,values[k],4);memcpy(out+28,p->raw+4,36);unsigned animation=stage==3&&!specs[bank].bank&&p->type>=51&&p->type<=53?p->type-50:stage==2&&p->type==30?4:0;put(out+64,animation,4);
        }
        free(rows);if(bad)return -1;
        if(!audio&&(bank==4||specs[bank+1].stage!=stage)){
            if(boxes[si]*172!=r->sections[5+si*2].size||placements[si]*68!=r->sections[6+si*2].size)return -1;
            unsigned char *out=data+r->sections[13].offset+si*24;put(out,stage,4);put(out+4,specs[bank].sectors,4);put(out+8,placements[si],4);put(out+12,boxes[si],4);
            if(table_pointer_fixup(r,data,4+si*2,13,si*24+16,6+si*2,0)||table_pointer_fixup(r,data,5+si*2,13,si*24+20,5+si*2,0))return -1;
        }
    }
    return 0;
}
