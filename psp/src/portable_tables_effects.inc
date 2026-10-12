static int populate_rival_graffiti(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    unsigned i;
    if(r->section_count!=1 || r->sections[0].size!=6)return -1;
    for(i=0;i<6;i++) {
        uint32_t logical,record,index;
        if(table_word(disc,exe,0x8c010000u,0x8c1ce420u+i*4,&logical) || logical>=5 ||
           table_indirect(disc,exe,0x8c05bf84u,(102+logical)*4,&record) || record>UINT32_MAX-4 ||
           table_word(disc,exe,0x8c010000u,record+4,&index) || index>=5)return -1;
        data[r->sections[0].offset+i]=(unsigned char)index;
    }
    return 0;
}
static int populate_reverb(JgrDisc *disc,const JgrDiscFile *file,const TableRecipe *r,unsigned char *data)
{
    unsigned i,coefficients=0,delays=0;unsigned char header[4];
    if(r->section_count!=2 || r->sections[0].size!=212 || r->sections[1].size!=132 ||
       jgr_disc_read(disc,file,0,header,4) || memcmp(header,"SFPB",4))return -1;
    for(i=0;i<128;i++) {
        unsigned char words[16];uint32_t a,b,c,d,coefficient,delay;unsigned ysel,shift;
        int multiplier;
        if(jgr_disc_read(disc,file,0x454+i*16,words,16))return -1;
        a=word(words);b=word(words+4);c=word(words+8);d=word(words+12);
        if(i>=84){if(a || b || c || d)return -1;continue;}
        ysel=(b>>13)&3;shift=(c>>4)&3;
        if((a&~0xfffeu) || (b&~0xfffeu) || (c&~0x7f13u) || (d&~0x7e80u) ||
           ysel>1 || shift>1 || (c&0x84u) || ((b>>7)&63)>32 ||
           table_word(disc,file,0,0x54+i*4,&coefficient))return -1;
        multiplier=ysel ? (int)(int16_t)(coefficient&65535)>>3 : 0;
        if(multiplier) {
            if(coefficients==53)return -1;
            put(data+r->sections[0].offset+coefficients++*4,(uint32_t)multiplier,4);
        }
        if(c&0x6000) {
            if(!(i&1) || delays==33 || table_word(disc,file,0,0x254+((d>>9)&63)*4,&delay) || delay>18294)return -1;
            delay+=(d&0x80)!=0;if(delay>18294)return -1;
            put(data+r->sections[1].offset+delays++*4,delay,4);
        }
    }
    return coefficients==53 && delays==33 ? 0 : -1;
}
static int hardpoint_descriptor(JgrDisc *disc,const JgrDiscFile *exe,uint32_t descriptor,
                                unsigned char *out,unsigned capacity,unsigned *written)
{
    uint32_t shape,pointer;unsigned count,i;
    if(descriptor>UINT32_MAX-4 || table_word(disc,exe,0x8c010000u,descriptor,&shape) ||
       table_word(disc,exe,0x8c010000u,descriptor+4,&pointer))return -1;
    count=shape>>16;if(count>capacity)return -1;
    for(i=0;i<count;i++) {
        uint64_t at=(uint64_t)pointer+i*12;
        if(at>UINT32_MAX || table_bytes(disc,exe,0x8c010000u,(uint32_t)at,out+i*16+4,12))return -1;
        put(out+i*16,shape&65535,4);
    }
    *written=count;return 0;
}
static int populate_hardpoints(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    static const uint32_t callers[]={0x8c0a7d20u,0x8c09c2acu,0x8c0a3730u};
    unsigned i,count=0;
    if(r->section_count!=1 || r->sections[0].size!=17*16)return -1;
    for(i=0;i<10;i++) {
        uint32_t descriptor;unsigned written;
        if(i<4){if(table_word(disc,exe,0x8c010000u,0x8c1c92ccu+i*4,&descriptor))return -1;}
        else if(i<7){if(table_indirect(disc,exe,callers[i-4],0,&descriptor))return -1;}
        else {
            if(table_word(disc,exe,0x8c010000u,0x8c21bc20u,&descriptor) || descriptor>UINT32_MAX-24)return -1;
            descriptor+=((i-6)%3)*12;
        }
        if(hardpoint_descriptor(disc,exe,descriptor,data+r->sections[0].offset+count*16,17-count,&written))return -1;
        if(i>=7 && written!=1)return -1;
        count+=written;
    }
    return count==17 ? 0 : -1;
}
static int populate_skate_anchors(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    unsigned model;
    if(r->section_count!=3 || r->sections[0].size!=16*29*16 || r->sections[1].size!=192)return -1;
    for(model=0;model<16;model++) {
        JgrDiscFile file;uint32_t stack[20],visited[20],anchor_bits=0,section;unsigned pending=1,count=0,i;
        if(jgr_disc_source(disc,table_player_sources[model],&file) || play_section_select(disc,&file,5,0,stack))return -1;
        while(pending) {
            uint32_t address=stack[--pending],attachment,child,sibling;unsigned char node[68];
            if(!address)continue;
            if(count==20 || jgr_disc_read(disc,&file,address,node,68))return -1;
            for(i=0;i<count;i++)if(visited[i]==address)return -1;
            visited[count]=address;attachment=word(node+8);child=word(node+60);sibling=word(node+64);
            if(attachment) {
                uint32_t array,n;
                if(attachment>UINT32_MAX-4 || table_word(disc,&file,0,attachment,&array) ||
                   table_word(disc,&file,0,attachment+4,&n) || n>29)return -1;
                for(i=0;i<n;i++) {
                    unsigned char row[16],*out;uint32_t index;uint64_t at=(uint64_t)array+i*16;
                    if(at>UINT32_MAX || jgr_disc_read(disc,&file,(uint32_t)at,row,16))return -1;
                    index=word(row+12);if(index>=29 || (anchor_bits&(1u<<index)))return -1;
                    anchor_bits|=1u<<index;out=data+r->sections[0].offset+(model*29+index)*16;
                    put(out,count,4);memcpy(out+4,row,12);
                }
            }
            if(sibling){if(pending==20)return -1;stack[pending++]=sibling;}
            if(child){if(pending==20)return -1;stack[pending++]=child;}
            count++;
        }
        if(count!=20 || anchor_bits!=0x1fffffffu || play_section(disc,&file,4,&section))return -1;
        for(i=0;i<12;i++) {
            uint32_t index,mask;uint64_t at=(uint64_t)section+12+i*4;
            if(at>UINT32_MAX || table_word(disc,&file,0,(uint32_t)at,&index) || index>=15 ||
               table_word(disc,exe,0x8c010000u,0x8c17f130u+index*4,&mask) || mask>255)return -1;
            data[r->sections[1].offset+model*12+i]=(unsigned char)mask;
        }
    }
    return 0;
}
static int populate_gas_body(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    JgrDiscFile bank;unsigned variant;
    if(r->section_count!=1 || r->sections[0].size!=192 ||
       jgr_disc_source(disc,"decompile/afs/ENEMY/003_kidoutaidat.bin",&bank))return -1;
    for(variant=0;variant<2;variant++) {
        uint32_t stack[6],visited[6];unsigned pending=1,count=0;
        if(table_word(disc,exe,0x8c010000u,0x8c1b5c60u+variant*4,stack))return -1;
        while(pending) {
            uint32_t address=stack[--pending],shape,kind,child,sibling;unsigned char node[68];unsigned i;
            if(!address)continue;
            if(count==6 || table_bytes(disc,&bank,ENEMY_BASE,address,node,68))return -1;
            for(i=0;i<count;i++)if(visited[i]==address)return -1;
            visited[count]=address;shape=word(node+4);kind=word(node);child=word(node+60);sibling=word(node+64);
            if(((kind&65535)!=0 && (kind&65535)!=2) || word(node+48) || word(node+52) || word(node+56))return -1;
            if(shape && ((kind>>16)!=26 || table_bytes(disc,&bank,ENEMY_BASE,shape,data+r->sections[0].offset+(variant*6+count)*16,16)))return -1;
            if(sibling){if(pending==6)return -1;stack[pending++]=sibling;}
            if(child){if(pending==6)return -1;stack[pending++]=child;}
            count++;
        }
        if(count!=6)return -1;
    }
    return 0;
}
