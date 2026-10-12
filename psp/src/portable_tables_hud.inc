/* Source GUI records reduced to the port's existing render-table ABI. Source
 * texture/mesh data remains separate; this provider emits no guessed layout. */
static int table_bytes(JgrDisc *disc,const JgrDiscFile *file,uint32_t base,uint32_t address,void *out,unsigned bytes)
{return address<base ? -1 : jgr_disc_read(disc,file,address-base,out,bytes);}
static int table_word(JgrDisc *disc,const JgrDiscFile *file,uint32_t base,uint32_t address,uint32_t *out)
{unsigned char bytes[4];if(table_bytes(disc,file,base,address,bytes,4))return -1;*out=word(bytes);return 0;}
static int hud_append(JgrDisc *disc,const JgrDiscFile *game,uint32_t cell,unsigned mode,
                      unsigned char *out,unsigned capacity,unsigned *written)
{
    unsigned char header[32];uint32_t address,first,count;unsigned i,stride=mode==1 ? 40 : mode==2 ? 24 : 36;
    if(table_word(disc,game,0x8c980000u,cell,&address) ||
       table_bytes(disc,game,0x8c980000u,address,header,32))return -1;
    first=word(header);count=word(header+4);
    if(count>capacity/stride || word(header+8) || word(header+12) || word(header+16) || word(header+20) ||
       word(header+24)!=0x3f800000u || word(header+28)!=0x3f800000u || (mode==2 && count!=1))return -1;
    for(i=0;i<count;i++) {
        unsigned char entry[64],*dest=out+i*stride;unsigned slot,flags,j;
        uint64_t at=(uint64_t)first+i*64;
        if(at>UINT32_MAX || table_bytes(disc,game,0x8c980000u,(uint32_t)at,entry,64))return -1;
        slot=entry[0]|(unsigned)entry[1]<<8;flags=entry[2]|(unsigned)entry[3]<<8;
        if(entry[20] || entry[21] || word(entry+24)!=0x3f000000u || word(entry+28)!=0x3f000000u)return -1;
        for(j=0;j<4;j++)if(!isfinite(enemy_float(entry+4+j*4)) || !isfinite(enemy_float(entry+48+j*4)))return -1;
        if(mode==1) {
            if(slot!=1 || (flags&~24u)!=9600 || word(entry+32)!=word(entry+36) ||
               word(entry+32)!=word(entry+40) || word(entry+32)!=word(entry+44))return -1;
            put(dest,flags&24,4);memcpy(dest+4,entry+32,4);memcpy(dest+8,entry+4,16);memcpy(dest+24,entry+48,16);
        }else if(mode==2) {
            if(slot!=2)return -1;
            memcpy(dest,entry+12,8);memcpy(dest+8,entry+48,16);
        }else {
            if((slot!=1 && slot!=2) || flags!=9600)return -1;
            for(j=0;j<4;j++)if(word(entry+32+j*4)!=UINT32_MAX)return -1;
            put(dest,slot-1,4);memcpy(dest+4,entry+4,16);memcpy(dest+20,entry+48,16);
        }
    }
    *written=count*stride;return 0;
}
static int populate_pause(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    uint32_t models[42][3];unsigned model_count=0,stage;
    if(r->section_count!=1 || r->sections[0].size!=6*7*32)return -1;
    for(stage=0;stage<6;stage++) {
        uint32_t address;unsigned index;
        if(table_word(disc,exe,0x8c010000u,0x8c17874cu+stage*4,&address))return -1;
        for(index=0;index<7;index++) {
            unsigned char row[32],*out=data+r->sections[0].offset+(stage*7+index)*32;
            uint32_t cell,selected,intro;unsigned model;
            if(table_bytes(disc,exe,0x8c010000u,address,row,32))return -1;
            cell=word(row+20);selected=word(row+24);intro=word(row+28);
            if(!cell){put(out,UINT32_MAX,4);break;}
            if(index==6 || address>UINT32_MAX-32)return -1;
            for(model=0;model<model_count;model++)
                if(models[model][0]==cell && models[model][1]==selected && models[model][2]==intro)break;
            if(model==model_count) {
                if(model_count==42)return -1;
                models[model][0]=cell;models[model][1]=selected;models[model][2]=intro;model_count++;
            }
            put(out,model,4);memcpy(out+4,row+16,4);put(out+8,selected ? 0 : UINT32_MAX,4);
            put(out+12,intro ? (selected ? 1 : 0) : UINT32_MAX,4);memcpy(out+16,row,16);address+=32;
        }
    }
    return 0;
}
static int populate_hud(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    JgrDiscFile game;unsigned i,used=0;const TableSection *section=r->sections;
    if(r->special==15)return populate_pause(disc,exe,r,data);
    if(r->special==16) {
        if(r->section_count!=1 || section->size!=3*28)return -1;
        for(i=0;i<3;i++) {
            unsigned char row[32],*out=data+section->offset+i*28;uint32_t selected;
            if(table_bytes(disc,exe,0x8c010000u,0x8c179344u+i*32,row,32))return -1;
            selected=word(row+24);put(out,i,4);put(out+4,selected ? 0 : UINT32_MAX,4);
            put(out+8,selected ? 1 : 0,4);memcpy(out+12,row,16);
        }
        return 0;
    }
    if(jgr_disc_source(disc,r->special==14 ? "decompile/afs/E_GAME/000_gamedispdat.bin" :
                       "decompile/afs/U_GAME/000_gamedispdat.bin",&game))return -1;
    if(r->special==17) {
        uint32_t address;
        if(r->section_count!=3 || section[0].size!=31*12 || section[1].size!=16 || section[2].size!=16 ||
           table_word(disc,&game,0x8c980000u,0x8c9801e8u,&address) ||
           table_bytes(disc,&game,0x8c980000u,address,data+section[0].offset,31*12) ||
           word(data+section[0].offset+30*12)!=UINT32_MAX ||
           table_bytes(disc,exe,0x8c010000u,0x8c21c070u,data+section[1].offset,16) ||
           table_bytes(disc,exe,0x8c010000u,0x8c21c080u,data+section[2].offset,16))return -1;
        return 0;
    }
    if(r->special==11 || r->special==12) {
        static const unsigned offsets[2][3]={{0x214,0x228,0x22c},{0x234,0x238,0x23c}};
        unsigned mode=r->special==12;
        if(mode && (r->section_count!=2 || section[1].size!=16))return -1;
        for(i=0;i<3;i++) {
            unsigned written;
            if(hud_append(disc,&game,0x8c980000u+offsets[mode][i],mode,data+section->offset+used,section->size-used,&written))return -1;
            used+=written;if(mode)put(data+section[1].offset+(i+1)*4,used/40,4);
        }
        if(mode)return used==section->size ? 0 : -1;
    }
    if(r->special==11 || r->special==14) {
        unsigned font,first=r->special==14 ? 1 : 0,end=r->special==14 ? 2 : 3;
        for(font=first;font<end;font++) {
            uint32_t table;unsigned count=font==2 ? 11 : 10;
            if(table_word(disc,exe,0x8c010000u,0x8c182a38u+font*4,&table))return -1;
            for(i=0;i<count;i++) {
                uint32_t cell;unsigned written;
                if((uint64_t)table+i*4>UINT32_MAX || table_word(disc,exe,0x8c010000u,table+i*4,&cell) ||
                   hud_append(disc,&game,cell,r->special==14 ? 2 : 0,data+section->offset+used,section->size-used,&written))return -1;
                if(written!=(r->special==14 ? 24 : 36))return -1;
                used+=written;
            }
        }
        return used==section->size ? 0 : -1;
    }
    if(r->special==13) {
        uint32_t table,cell;unsigned written;
        if(table_word(disc,exe,0x8c010000u,0x8c182a70u,&table) ||
           table_word(disc,exe,0x8c010000u,table,&cell) ||
           hud_append(disc,&game,cell,0,data+section->offset,section->size,&written))return -1;
        return written==section->size ? 0 : -1;
    }
    return -1;
}
