/* Model ordinals are generated in the host exporter's first-use order. The
 * referenced roots, placements, captions and digits remain original inputs. */
static int pause_model(uint32_t models[128][2],unsigned *count,uint32_t cell,uint32_t motion)
{
    unsigned i;for(i=0;i<*count;i++)if(models[i][0]==cell && models[i][1]==motion)return (int)i;
    if(*count==128)return -1;
    models[*count][0]=cell;models[*count][1]=motion;return (int)(*count)++;
}
static int populate_pause_map(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    static const unsigned counts[]={5,5,6,1,1,1},texts[]={4,9,10,11};
    static const unsigned icons[9][2]={{0,0},{4,0},{0x190,0},{0x180,0x184},{0x188,0x18c},
        {0x194,0x198},{0x19c,0x1a0},{0x1a4,0x1a8},{0x1ac,0x1b0}};
    JgrDiscFile game;uint32_t models[128][2],address,list;unsigned model_count=0,stage,pool=0;
    const TableSection *s=r->sections;
    if(r->section_count!=7 || r->fixups!=4 || s[0].size!=1440 || s[1].size!=24 || s[2].size!=36 ||
       s[4].size!=33 || s[5].size!=16 || jgr_disc_source(disc,"decompile/afs/U_GAME/000_gamedispdat.bin",&game))return -1;
    for(stage=0;stage<6;stage++) {
        unsigned row;
        put(data+s[1].offset+stage*4,counts[stage],4);
        if(table_word(disc,exe,0x8c010000u,0x8c178c84u+stage*4,&list))return -1;
        for(row=0;row<counts[stage];row++) {
            unsigned char record[20],*out=data+s[0].offset+(stage*6+row)*40;uint32_t pointers;unsigned i;
            if((uint64_t)list+row*4>UINT32_MAX || table_word(disc,exe,0x8c010000u,list+row*4,&address) ||
               table_bytes(disc,exe,0x8c010000u,address,record,20))return -1;
            memcpy(out,record,16);pointers=word(record+16);
            for(i=0;i<6;i++) {
                uint32_t cell;int model;
                if((uint64_t)pointers+i*4>UINT32_MAX || table_word(disc,exe,0x8c010000u,pointers+i*4,&cell))return -1;
                if(!cell){put(out+16+i*4,UINT32_MAX,4);break;}
                if(i==5 || (model=pause_model(models,&model_count,cell,0))<0)return -1;
                put(out+16+i*4,(unsigned)model,4);
            }
        }
    }
    for(stage=0;stage<9;stage++) {
        int model=pause_model(models,&model_count,0x8c980000u+icons[stage][0],icons[stage][1] ? 0x8c980000u+icons[stage][1] : 0);
        if(model<0)return -1;
        put(data+s[2].offset+stage*4,(unsigned)model,4);
    }
    if(table_word(disc,&game,0x8c980000u,0x8c9801e4u,&list))return -1;
    for(stage=0;stage<4;stage++) {
        unsigned char text[256];unsigned length;
        if((uint64_t)list+texts[stage]*4>UINT32_MAX || table_word(disc,&game,0x8c980000u,list+texts[stage]*4,&address) ||
           table_string(disc,&game,0x8c980000u,address,text,sizeof text,&length) ||
           table_string_fixup(r,data,stage,3,stage,text,length,&pool))return -1;
    }
    for(stage=0;stage<11;stage++)
        if(table_word(disc,exe,0x8c010000u,0x8c173ba0u+stage*4,&address) ||
           table_bytes(disc,exe,0x8c010000u,address,data+s[4].offset+stage*3,2))return -1;
    if(pool!=s[6].size || table_indirect(disc,exe,0x8c0340bcu,4,&address) ||
       table_bytes(disc,exe,0x8c010000u,address,data+s[5].offset,16))return -1;
    return 0;
}
