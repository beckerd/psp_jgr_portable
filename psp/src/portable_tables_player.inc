/* PLAY section directories carry both the collision tree and original clip
 * lengths. Character identity comes from the source header, not a copied map. */
static int play_section_select(JgrDisc *disc,const JgrDiscFile *file,unsigned kind,unsigned last,uint32_t *section)
{
    unsigned char header[12];uint32_t table;unsigned i,found=0;
    if(jgr_disc_read(disc,file,0,header,12) || memcmp(header,"PLAY",4))return -1;
    table=word(header+4);
    for(i=0;i<256;i++) {
        unsigned char row[8];uint64_t at=(uint64_t)table+i*8;
        if(at>UINT32_MAX || jgr_disc_read(disc,file,(uint32_t)at,row,8))return -1;
        if(!word(row))return found ? 0 : -1;
        if(word(row)==kind){
            *section=word(row+4);found=1;
            if(!last)return 0;
        }
    }
    return -1;
}
static int play_section(JgrDisc *disc,const JgrDiscFile *file,unsigned kind,uint32_t *section)
{return play_section_select(disc,file,kind,0,section);}
static int populate_player_frames(JgrDisc *disc,const TableSection *section,unsigned char *data)
{
    unsigned model,seen=0;
    if(section->size!=16*176*2)return -1;
    for(model=0;model<16;model++) {
        JgrDiscFile file;uint32_t table,identity,address;unsigned clip;
        if(jgr_disc_source(disc,table_player_sources[model],&file) || play_section(disc,&file,3,&table) ||
           table_word(disc,&file,0,8,&address) || table_word(disc,&file,0,address,&identity) ||
           identity>=16 || (seen&(1u<<identity)))return -1;
        seen|=1u<<identity;
        for(clip=0;clip<176;clip++) {
            uint32_t header;uint64_t at=(uint64_t)table+clip*4;
            if(at>UINT32_MAX || table_word(disc,&file,0,(uint32_t)at,&address) || !address ||
               table_word(disc,&file,0,address,&header))return -1;
            header>>=16;if(!header)header=1;put(data+section->offset+(identity*176+clip)*2,header,2);
        }
    }
    return seen==65535 ? 0 : -1;
}
static int populate_player_body(JgrDisc *disc,const TableRecipe *r,unsigned char *data)
{
    unsigned model;
    if(r->section_count!=1 || r->sections[0].size!=16*20*60)return -1;
    for(model=0;model<16;model++) {
        struct {uint32_t address;int parent;} stack[20];uint32_t visited[20];
        JgrDiscFile file;unsigned char header[12];uint32_t record;unsigned pending=1,count=0;
        /* 018fb0 passes PLAY's normal character tree (+6c) to 0b6d5c.
         * Some files contain another section-5 tree for spray rendering;
         * directory order does not select the actor's collision hierarchy. */
        if(jgr_disc_source(disc,table_player_sources[model],&file) ||
           jgr_disc_read(disc,&file,0,header,sizeof header) || memcmp(header,"PLAY",4))return -1;
        record=word(header+8);
        if(record>UINT32_MAX-0x6c || table_word(disc,&file,0,record+0x6c,&stack[0].address) ||
           !stack[0].address)return -1;
        stack[0].parent=-1;
        while(pending) {
            unsigned char node[68],*out;uint32_t address,kind,child,sibling;int parent;unsigned i;
            pending--;address=stack[pending].address;parent=stack[pending].parent;
            if(!address)continue;
            if(count==20 || jgr_disc_read(disc,&file,address,node,sizeof node))return -1;
            for(i=0;i<count;i++)if(visited[i]==address)return -1;
            visited[count]=address;kind=word(node);child=word(node+60);sibling=word(node+64);
            if(((kind&65535)!=0 && (kind&65535)!=2) || word(node+48) || word(node+52) || word(node+56))return -1;
            out=data+r->sections[0].offset+(model*20+count)*60;
            put(out,(uint32_t)parent,4);put(out+4,kind>>16,4);
            if((kind&65535) && jgr_disc_read(disc,&file,word(node+4),out+8,16))return -1;
            memcpy(out+24,node+12,36);
            if(sibling){if(pending==20)return -1;stack[pending].address=sibling;stack[pending++].parent=parent;}
            if(child){if(pending==20)return -1;stack[pending].address=child;stack[pending++].parent=(int)count;}
            count++;
        }
        if(count!=20)return -1;
    }
    return 0;
}
static int populate_player_motion(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    unsigned index;
    if(r->section_count!=8 || r->sections[0].size!=134*2*56)return -1;
    for(index=0;index<134*2;index++) {
        unsigned char descriptor[32],*out=data+r->sections[0].offset+index*56;unsigned entry;
        if(table_bytes(disc,exe,0x8c010000u,0x8c10b2a8u+index*32,descriptor,32))return -1;
        memcpy(out,descriptor,4);memcpy(out+36,descriptor+20,4);memcpy(out+40,descriptor+28,4);
        memcpy(out+44,descriptor+4,4);memcpy(out+48,descriptor+24,4);memcpy(out+52,descriptor+16,4);
        for(entry=0;entry<2;entry++) {
            uint32_t ref=word(descriptor+8+entry*4);unsigned char *target=out+4+entry*16,record[16];
            if(ref==UINT32_MAX){put(target,UINT32_MAX,4);put(target+8,UINT32_MAX,4);continue;}
            if(ref>=170 || table_bytes(disc,exe,0x8c010000u,0x8c10a808u+ref*16,record,16))return -1;
            memcpy(target,record,4);memcpy(target+4,record+8,4);memcpy(target+8,record+4,4);memcpy(target+12,record+12,4);
        }
    }
    return populate_player_frames(disc,r->sections+3,data);
}
static int populate_tricks(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    static const uint32_t sources[]={0x8c1833fcu,0x8c183414u,0x8c18342cu,0x8c18342cu,0x8c18345cu,0x8c183474u,0x8c183444u};
    static const unsigned counts[]={15,15,30,30,1,1,1};
    unsigned group,fixup=0,used=0,length,model,seen=0;uint32_t address;unsigned char text[256];
    if(r->section_count!=7 || r->fixups!=93 || r->sections[3].size!=16 || r->sections[4].size!=192 || r->sections[5].size!=128)return -1;
    for(group=0;group<7;group++) {
        uint32_t table;unsigned i;
        if(table_word(disc,exe,0x8c010000u,sources[group]+4,&table))return -1;
        for(i=0;i<counts[group];i++) {
            if((uint64_t)table+i*4>UINT32_MAX || table_word(disc,exe,0x8c010000u,table+i*4,&address) ||
               table_string(disc,exe,0x8c010000u,address,text,sizeof text,&length) ||
               table_string_fixup(r,data,fixup++,2,group*30+i,text,length,&used))return -1;
        }
    }
    if(used!=r->sections[6].size || table_word(disc,exe,0x8c010000u,0x8c183200u,&address) ||
       table_string(disc,exe,0x8c010000u,address,data+r->sections[3].offset,16,&length) || length!=16)return -1;
    for(model=0;model<16;model++) {
        JgrDiscFile file;uint32_t table,identity;unsigned kind;
        if(jgr_disc_source(disc,table_player_sources[model],&file) || play_section(disc,&file,4,&table) ||
           table_word(disc,&file,0,8,&address) || table_word(disc,&file,0,address,&identity) ||
           identity>=16 || (seen&(1u<<identity)))return -1;
        seen|=1u<<identity;
        for(kind=0;kind<2;kind++) {
            unsigned count=kind ? 8 : 12,i,offset=kind ? 60 : 12;const TableSection *s=r->sections+4+kind;
            for(i=0;i<count;i++) {
                uint32_t id;uint64_t at=(uint64_t)table+offset+i*4;
                if(at>UINT32_MAX || table_word(disc,&file,0,(uint32_t)at,&id) || id>=(kind ? 30u : 15u))return -1;
                data[s->offset+identity*count+i]=(unsigned char)id;
            }
        }
    }
    return seen==65535 ? 0 : -1;
}
