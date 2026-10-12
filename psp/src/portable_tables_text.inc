/* Copy bounded source strings and animation frame counts into the existing
 * portable table ABI. Pointer relocation records describe output storage only. */
static int table_string(JgrDisc *disc,const JgrDiscFile *file,uint32_t base,uint32_t address,
                        unsigned char *text,unsigned capacity,unsigned *length)
{
    unsigned i;
    for(i=0;i<capacity;i++) {
        if((uint64_t)address+i>UINT32_MAX || table_bytes(disc,file,base,address+i,text+i,1))return -1;
        if(!text[i]){*length=i+1;return 0;}
    }
    return -1;
}
static int table_string_fixup(const TableRecipe *r,unsigned char *data,unsigned number,unsigned section,
                              unsigned index,const unsigned char *text,unsigned length,unsigned *used)
{
    unsigned offset=0,pool_index=r->section_count-1;
    const TableSection *pool=r->sections+pool_index;unsigned char *fixup;
    if(*used>pool->size)return -1;
    while(offset<*used) {
        const unsigned char *previous=data+pool->offset+offset;
        const unsigned char *end=(const unsigned char *)memchr(previous,0,*used-offset);
        unsigned previous_length;
        if(!end)return -1;
        previous_length=(unsigned)(end-previous)+1;
        if(previous_length==length && !memcmp(previous,text,length))break;
        offset=(offset+previous_length+3)&~3u;
    }
    if(number>=r->fixups || section>=pool_index || index>=r->sections[section].size/4 ||
       offset>pool->size || length>pool->size-offset)return -1;
    if(offset>=*used){memcpy(data+pool->offset+offset,text,length);*used=offset+length;}
    fixup=data+40+16*r->section_count+16*number;
    put(fixup,section,4);put(fixup+4,index*4,4);put(fixup+8,pool_index,4);put(fixup+12,offset,4);
    return 0;
}
static int populate_hud_text(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    JgrDiscFile game;unsigned i,length,used=0,result=r->special==20,count=result ? 4 : 2;
    unsigned char text[256];uint32_t list,address;
    if(r->version!=2 || r->section_count!=(result ? 5u : 3u) || r->sections[0].size!=count*4 ||
       r->sections[1].size!=33 || jgr_disc_source(disc,"decompile/afs/U_GAME/000_gamedispdat.bin",&game) ||
       table_word(disc,&game,0x8c980000u,0x8c9801e4u,&list))return -1;
    for(i=0;i<count;i++) {
        uint64_t at=(uint64_t)list+(result ? i : i+12)*4;
        if(at>UINT32_MAX || table_word(disc,&game,0x8c980000u,(uint32_t)at,&address) ||
           table_string(disc,&game,0x8c980000u,address,text,sizeof text,&length) ||
           table_string_fixup(r,data,i,0,i,text,length,&used))return -1;
    }
    for(i=0;i<11;i++) {
        if(table_word(disc,exe,0x8c010000u,0x8c173ba0u+i*4,&address) ||
           table_bytes(disc,exe,0x8c010000u,address,data+r->sections[1].offset+i*3,2))return -1;
    }
    if(result) {
        if(r->sections[2].size!=128 || r->sections[3].size!=120 || r->fixups!=34)return -1;
        for(i=0;i<2;i++)
            if(table_bytes(disc,exe,0x8c010000u,i ? 0x8c179594u : 0x8c179558u,
                           data+r->sections[2].offset+i*64,64))return -1;
        for(i=0;i<30;i++) {
            if(table_word(disc,exe,0x8c010000u,0x8c17948cu+i*4,&address) ||
               table_string(disc,exe,0x8c010000u,address,text,sizeof text,&length) ||
               table_string_fixup(r,data,i+4,3,i,text,length,&used))return -1;
        }
    }
    return used==r->sections[r->section_count-1].size ? 0 : -1;
}
static int compare_word(const void *a,const void *b)
{uint32_t x=*(const uint32_t *)a,y=*(const uint32_t *)b;return (x>y)-(x<y);}
static int populate_source_frames(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    uint32_t pointers[24],sorted[24],table=r->special==21 ? 0x8c1b5c74u : 0x8c1b60ecu;
    unsigned count=r->special==21 ? 14 : 24,valid=0,i;JgrDiscFile bank;
    const TableSection *frames=r->sections+(r->special==21 ? 2 : 1);
    if(jgr_disc_source(disc,r->special==21 ? "decompile/afs/ENEMY/005_inudat.bin" :
                       "decompile/afs/ENEMY/033_onisimadat.bin",&bank))return -1;
    for(i=0;i<count;i++) {
        if(table_word(disc,exe,0x8c010000u,table+i*4,pointers+i) || !pointers[i])return -1;
        if(pointers[i]!=UINT32_MAX)sorted[valid++]=pointers[i];
    }
    if(r->special==21) {
        uint32_t end;
        if(table_word(disc,exe,0x8c010000u,table+count*4,&end) || end)return -1;
    }
    if(frames->size!=valid)return -1;
    qsort(sorted,valid,sizeof sorted[0],compare_word);
    for(i=0;i<valid;i++) {
        double frame;
        if(sorted[i]>UINT32_MAX-2 || read_value(disc,&bank,0x8ca00000u,sorted[i]+2,'H',&frame) || frame>255)return -1;
        data[frames->offset+i]=(unsigned char)frame;
    }
    if(r->special==21) {
        if(r->sections[1].size!=count)return -1;
        for(i=0;i<count;i++) {
            unsigned j;for(j=0;j<valid;j++)if(sorted[j]==pointers[i])break;
            data[r->sections[1].offset+i]=j==valid ? 255 : (unsigned char)j;
        }
    }
    return 0;
}
