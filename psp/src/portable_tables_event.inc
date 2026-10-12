/* EVENT commands supply model identities and sound selectors at setup. */
static int table_named_section(const TableRecipe *r,const char *group,const char *symbol,unsigned index)
{
    char name[128];int n=snprintf(name,sizeof name,"%s/%s_%u",group,symbol,index);if(n<0||(unsigned)n>=sizeof name)return -1;
    uint32_t identity=crc32((const unsigned char *)name,(unsigned)n);
    for(unsigned i=0;i<r->section_count;i++)if(r->sections[i].identity==identity)return (int)i;
    return -1;
}
static int table_audio_resolve(JgrDisc *disc,const JgrDiscFile *exe,unsigned stage,unsigned kind,unsigned bank,unsigned reference,unsigned char out[56])
{
    /* Reviewed extraction coordinates for each original loader profile. */
    static const uint32_t profiles[]={0x8c1d43b4,0x8c1d4a98,0x8c1d4504,0x8c1d42b8,0x8c1d45ac,0x8c1d3f38,0x8c1d4c58,0x8c1d4cac,0x8c1d4e6c,0x8c1d502c,0x8c1d4cac,0x8c1d4e6c,0x8c1d502c,0x8c1d3cd0,0x8c1d3dcc,0x8c1d4cac,0x8c1d4e6c,0x8c1d502c,0x8c1d3b2c,0x8c1d3e90,0x8c1d40f8,0x8c1d468c,0x8c1d4aec,0x8c1d4b94,0x8c1d4718,0x8c1d4328,0x8c1d48a0,0x8c1d49d4,0x8c1d476c};
    static const unsigned world[7][3]={{0},{64,70,76},{63,68,74},{62,67,73},{43,69,75},{65,67,73},{65,67,73}};
    unsigned op=kind,logical=bank,program=reference,patch=0,patchbank=2;uint32_t address=0,file=0;
    if(!stage||stage>6)return -1;
    if(op==0x5b||op==0x5c||op==0x5d)op=0x29;
    if(op==0x36){unsigned char r[8];if(program>=8||table_bytes(disc,exe,0x8c010000u,0x8c1ce468+program*8,r,8))return -1;logical=word(r);program=word(r+4);op=(int32_t)logical<0?0x29:0x2a;}
    if(op==0x29){
        if(program&0x80000000u){logical=15;program&=65535;}
        else {unsigned type=program>>16;uint32_t profile,record,tag,end;program&=65535;
            if(type>=25||table_word(disc,exe,0x8c010000u,0x8c1ba590+type*4,&profile))return -1;
            if(profile<27||profile>=27+sizeof profiles/sizeof profiles[0])return 1;
            if(table_indirect(disc,exe,0x8c05bf94,profile*4,&record)||table_word(disc,exe,0x8c010000u,record,&tag)||table_word(disc,exe,0x8c010000u,record+32,&end)||tag!=0x1d||end!=0x52||table_word(disc,exe,0x8c010000u,record+4,&file))return -1;
            address=profiles[profile-27];logical=11;
        }
    }
    switch(logical){
        case 4:address=0x8c1d3384;file=5;break;
        case 15:address=0x8c1d51ec;file=66;patch=72;patchbank=1;break;
        case 16:address=0x8c1d5400;file=world[stage][0];break;
        case 17:address=0x8c1d595c;file=world[stage][1];patch=world[stage][2];break;
        case 18:address=0x8c1d5b38;file=0;break;
        default:break;
    }
    unsigned char profile[28];if(!address||program>1024||table_bytes(disc,exe,0x8c010000u,address+program*28,profile,28))return -1;
    unsigned backend=word(profile),channel=word(profile+4);if(backend>2||channel>18||word(profile+24))return 1;
    for(unsigned k=0;k<3;k++)if(!isfinite(enemy_float(profile+8+k*4)))return -1;
    if(backend!=2){JgrDiscFile archive,member;unsigned char header[16];if(jgr_disc_find(disc,"JETRADIO/E_SND_SE.AFS",&archive)||jgr_disc_afs(disc,&archive,file,&member)||jgr_disc_read(disc,&member,0,header,16))return -1;if(memcmp(header,backend?"SMSB":"SOSB",4)||program>=word(header+12))return 1;}
    unsigned values[11]={kind,bank,reference,file,program,backend,channel,logical,patch,patchbank,backend};
    for(unsigned i=0;i<11;i++)put(out+i*4,values[i],4);
    memcpy(out+44,profile+8,12);return 0;
}
static int populate_enemy_audio(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    if(r->section_count!=1||r->sections[0].size%56)return -1;
    unsigned used=0,capacity=r->sections[0].size/56;JgrDiscFile archive;
    if(jgr_disc_find(disc,"JETRADIO/E_SND_SE.AFS",&archive))return -1;
    for(unsigned kind=0;kind<25;kind++){
        unsigned char first[56];int status=table_audio_resolve(disc,exe,1,0x29,0,kind<<16,first);if(status<0)return -1;if(status)continue;
        JgrDiscFile bank;uint32_t count;if(jgr_disc_afs(disc,&archive,word(first+12),&bank)||table_word(disc,&bank,0,12,&count)||count>1024)return -1;
        for(unsigned program=0;program<count;program++){unsigned char cue[56];status=table_audio_resolve(disc,exe,1,0x29,0,(kind<<16)|program,cue);if(status<0)return -1;if(status)continue;if(used>=capacity)return -1;memcpy(data+r->sections[0].offset+used++*56,cue,56);}
    }
    for(unsigned program=7;program<=17;program++)if(program==7||program==8||program==9||program==11||program==15||program==16||program==17){if(used>=capacity||table_audio_resolve(disc,exe,1,0x29,0,0xffff0000|program,data+r->sections[0].offset+used++*56))return -1;}
    return used==capacity?0:-1;
}
typedef struct {unsigned kind,bank,reference;} TableAudioKey;
static int table_audio_order(const void *a,const void *b){const TableAudioKey *x=a,*y=b;if(x->kind!=y->kind)return x->kind<y->kind?-1:1;if(x->bank!=y->bank)return x->bank<y->bank?-1:1;return x->reference<y->reference?-1:x->reference>y->reference;}
static int populate_event_audio(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    unsigned sections=0;
    for(unsigned event=0;event<112;event++){
        int slot=table_named_section(r,"event_audio_tables","EVENT_AUDIO",event);if(slot<0)continue;sections++;JgrDiscFile file;uint32_t stage;TableAudioKey keys[512];unsigned count=0;
        if(table_event(disc,exe,event,&file)||table_word(disc,exe,0x8c010000u,0x8c1d0fd0+event*0x44+0x3c,&stage))return -1;
        for(unsigned phase=0;phase<3;phase++){
            uint32_t script;if(table_word(disc,&file,0x8c900000u,0x8c900000+phase*4,&script))return -1;unsigned at;
            for(at=0;at<4096;at++){
                unsigned char command[20];if(table_bytes(disc,&file,0x8c900000u,script+at*20,command,20))return -1;unsigned op=word(command);if(op==100)break;TableAudioKey key={op,word(command+8),word(command+12)};
                if(op==0x29||op==0x36)key.reference=key.bank;
                else if(op==0x5b||op==0x5c||op==0x5d)key.bank=key.reference=op==0x5b?0x1000d:op==0x5c?0xffff0010:0x90005;
                else if(op!=0x2a&&op!=0x35)continue;
                unsigned i;for(i=0;i<count;i++)if(!table_audio_order(keys+i,&key))break;if(i==count){if(count>=512)return -1;keys[count++]=key;}
            }
            if(at==4096)return -1;
        }
        if(count*56!=r->sections[slot].size)return -1;
        qsort(keys,count,sizeof *keys,table_audio_order);
        for(unsigned i=0;i<count;i++)if(table_audio_resolve(disc,exe,stage+1,keys[i].kind,keys[i].bank,keys[i].reference,data+r->sections[slot].offset+i*56))return -1;
    }
    return sections==r->section_count?0:-1;
}
static int populate_scene_models(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    unsigned sections=0,fixups=0,used=0;
    for(unsigned event=0;event<112;event++){
        int slot=table_named_section(r,"scene_models","EVENT_MODELS",event);if(slot<0)continue;sections++;JgrDiscFile file;unsigned count=0;if(table_event(disc,exe,event,&file))return -1;
        for(unsigned phase=0;phase<3;phase++){
            uint32_t script;if(table_word(disc,&file,0x8c900000u,0x8c900000+phase*4,&script))return -1;unsigned at;
            for(at=0;at<4096;at++){
                unsigned char command[20],row[20]={0};if(table_bytes(disc,&file,0x8c900000u,script+at*20,command,20))return -1;unsigned op=word(command);if(op==100)break;
                if(op==0x21){unsigned type=word(command+4);uint32_t descriptor,root,list;if(type>=26||table_indirect(disc,exe,0x8c06fb04,type*4,&descriptor)||table_indirect(disc,exe,descriptor,0,&root)||table_indirect(disc,exe,descriptor+4,0,&list))return -1;put(row,type,4);put(row+4,root,4);put(row+8,list,4);put(row+12,type==10||type==13?2:1,4);}
                else if(op==0x2d){put(row,UINT32_MAX,4);memcpy(row+4,command+8,12);}else continue;
                unsigned i;for(i=0;i<count;i++){const unsigned char *prior=data+r->sections[slot].offset+i*20;if(word(row)==UINT32_MAX?word(prior)==UINT32_MAX&&!memcmp(row+4,prior+4,12):word(row)==word(prior))break;}
                if(i<count)continue;
                if(count>=r->sections[slot].size/20)return -1;
                memcpy(data+r->sections[slot].offset+count*20,row,20);char asset[64];int n=snprintf(asset,sizeof asset,"event%03u_m%02u.jgc",event,count);if(n<0||(unsigned)n>=sizeof asset||table_string_fixup(r,data,fixups++,(unsigned)slot,count*5+4,(unsigned char *)asset,(unsigned)n+1,&used))return -1;count++;
            }
            if(at==4096)return -1;
        }
        if(count*20!=r->sections[slot].size)return -1;
    }
    return sections+1==r->section_count&&fixups==r->fixups&&used==r->sections[r->section_count-1].size?0:-1;
}
