/* Original scene/movie loader records select source AFS members. */
static int table_indirect(JgrDisc *disc,const JgrDiscFile *exe,uint32_t address,uint32_t offset,uint32_t *out)
{
    uint32_t pointer;
    if(table_word(disc,exe,0x8c010000u,address,&pointer) || pointer>UINT32_MAX-offset)return -1;
    return table_word(disc,exe,0x8c010000u,pointer+offset,out);
}
static int table_event(JgrDisc *disc,const JgrDiscFile *exe,unsigned descriptor,JgrDiscFile *out)
{
    uint32_t request,index,record,kind,base;JgrDiscFile archive;
    if(descriptor>=112 || table_indirect(disc,exe,0x8c0eacd0u,descriptor*4,&request) || request>UINT32_MAX-8 ||
       table_word(disc,exe,0x8c010000u,request,&kind) || kind!=16 ||
       table_word(disc,exe,0x8c010000u,request+4,&index) || index>4095 ||
       table_word(disc,exe,0x8c010000u,request+8,&kind) || kind!=34 ||
       table_indirect(disc,exe,0x8c05be64u,index*4,&record) || record>UINT32_MAX-32 ||
       table_word(disc,exe,0x8c010000u,record,&kind) || kind!=67 ||
       table_word(disc,exe,0x8c010000u,record+8,&base) || base!=0x8c900000u ||
       table_word(disc,exe,0x8c010000u,record+4,&index) ||
       jgr_disc_find(disc,"JETRADIO/EVENT.AFS",&archive))return -1;
    return jgr_disc_afs(disc,&archive,index,out);
}
static int populate_intro(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    JgrDiscFile event;uint32_t script;unsigned index,models=0,motions=0;
    if(r->section_count!=2 || r->sections[0].size!=72 || r->sections[1].size!=8 ||
       table_event(disc,exe,7,&event) || table_word(disc,&event,0x8c900000u,0x8c900004u,&script))return -1;
    for(index=0;index<1024;index++) {
        unsigned char words[20];uint64_t at=(uint64_t)script+index*20;uint32_t op;unsigned i;
        if(at>UINT32_MAX || table_bytes(disc,&event,0x8c900000u,(uint32_t)at,words,20))return -1;
        op=word(words);if(op==100)break;
        if(op==0x2d) {
            unsigned char *dest=data+r->sections[0].offset;
            if(word(words+16)!=2)return -1;
            for(i=0;i<models;i++)if(!memcmp(dest+i*8,words+8,8))break;
            if(i==models){if(models==9)return -1;memcpy(dest+models++*8,words+8,8);}
        }else if(op==0x34) {
            unsigned char *dest=data+r->sections[1].offset;uint32_t motion=word(words+8),header;
            if(table_word(disc,&event,0x8c900000u,motion,&header) || header!=0x04b00011u)return -1;
            for(i=0;i<motions;i++)if(word(dest+i*4)==motion)break;
            if(i==motions){if(motions==2)return -1;put(dest+motions++*4,motion,4);}
        }
    }
    return index<1024 && models==9 && motions==2 ? 0 : -1;
}
static int populate_movie(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    unsigned movie,cues=0;
    if(r->section_count!=5 || r->sections[0].size!=35 || r->sections[1].size!=490 || r->sections[4].size!=32)return -1;
    memset(data+r->sections[1].offset,255,490);
    for(movie=0;movie<35;movie++) {
        char name[48];JgrDiscFile archive,file;uint32_t list,voices,script;unsigned segment,index;
        snprintf(name,sizeof name,"JETRADIO/DEMO%02u.AFS",movie+1);
        if(jgr_disc_find(disc,name,&archive) || jgr_disc_afs(disc,&archive,0,&file) ||
           table_word(disc,&file,0x8c800000u,0x8c800004u,&list) ||
           table_word(disc,exe,0x8c010000u,0x8c1e5284u+movie*4,&voices))return -1;
        for(segment=0;segment<=7;segment++) {
            uint32_t address,voice;
            if((uint64_t)list+segment*4>UINT32_MAX || table_word(disc,&file,0x8c800000u,list+segment*4,&address))return -1;
            if(!address)break;
            if(segment==7 || (uint64_t)voices+segment*4>UINT32_MAX ||
               table_word(disc,exe,0x8c010000u,voices+segment*4,&voice) || (voice!=UINT32_MAX && voice>=184))return -1;
            put(data+r->sections[1].offset+(movie*7+segment)*2,voice,2);
        }
        data[r->sections[0].offset+movie]=(unsigned char)segment;
        if(table_word(disc,&file,0x8c800000u,0x8c800000u,&script) ||
           table_word(disc,&file,0x8c800000u,script,&script))return -1;
        for(index=0;index<512;index++) {
            unsigned char command[16];uint64_t at=(uint64_t)script+index*28;
            if(at>UINT32_MAX || table_bytes(disc,&file,0x8c800000u,(uint32_t)at,command,16))return -1;
            if(word(command)==16)break;
            if(word(command)==13) {
                unsigned char *out=data+r->sections[4].offset+cues*16;uint32_t voice=word(command+12);
                if(cues==2 || word(command+4)>=segment || word(command+8)>INT32_MAX || (voice!=UINT32_MAX && voice>=184))return -1;
                put(out,movie,4);memcpy(out+4,command+4,12);cues++;
            }
        }
        if(index==512)return -1;
    }
    return cues==2 ? 0 : -1;
}
static int populate_dialogue(JgrDisc *disc,const TableRecipe *r,unsigned char *data)
{
    JgrDiscFile archive;uint32_t members;unsigned index,i;unsigned char enabled[112]={0};
    if(r->section_count!=1 || r->sections[0].size!=112 || jgr_disc_find(disc,"JETRADIO/E_MISSIONDATA.AFS",&archive) ||
       table_word(disc,&archive,0,4,&members) || members>4096)return -1;
    for(i=0;i<sizeof table_dialogue_routes;i++)enabled[table_dialogue_routes[i]]=1;
    for(index=0;index<members;index++) {
        JgrDiscFile file;unsigned char magic[4];uint32_t visited[128],offset=0;unsigned count=0;
        if(jgr_disc_afs(disc,&archive,index,&file) || file.size<4 || jgr_disc_read(disc,&file,0,magic,4))return -1;
        if(memcmp(magic,"JMDc",4))continue;
        do {
            unsigned char header[240];uint32_t id,mode,next;
            if(count==128 || (offset&3) || jgr_disc_read(disc,&file,offset,header,sizeof header) || memcmp(header,"JMDc",4))return -1;
            for(i=0;i<count;i++)if(visited[i]==offset)return -1;
            visited[count++]=offset;id=word(header+0xd8);mode=word(header+0x20);next=word(header+0xec);
            if(id<112 && enabled[id]) {
                unsigned char *value=data+r->sections[0].offset+id;
                if(mode<1 || mode>13 || (*value && *value!=mode))return -1;
                *value=(unsigned char)mode;
            }
            if(!next)break;
            if(next<0x8cde0000u)return -1;
            offset=next-0x8cde0000u;
        }while(1);
    }
    /* All enabled routes have an intro reference in the supported source;
     * there is no fallback value for a missing original mission selector. */
    for(i=0;i<112;i++)if(enabled[i] && !data[r->sections[0].offset+i])return -1;
    return 0;
}
