/* Source-only world control tables. Geometric bounds use the original binary32
 * operation order, preserving the current portable collision representation. */
static float table_round_float(float value) {volatile float rounded=value;return rounded;}
static void table_put_float(unsigned char *out,float value)
{uint32_t encoded;memcpy(&encoded,&value,4);put(out,encoded,4);}
static int populate_exit(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    unsigned index;uint32_t table;
    if(r->section_count!=1 || r->sections[0].size!=19*92 ||
       table_word(disc,exe,0x8c010000u,0x8c031e00u,&table) || table!=0x8c177c08u)return -1;
    for(index=14;index<=32;index++) {
        unsigned char row[80],*out=data+r->sections[0].offset+(index-14)*92;
        float vertex[4][3],center[3],radius=0;uint32_t address;unsigned i,j;
        if(table_word(disc,exe,0x8c010000u,table+index*4,&address) ||
           table_bytes(disc,exe,0x8c010000u,address,row,80) || word(row)!=6)return -1;
        for(i=0;i<4;i++)if(word(row+64+i*4))return -1;
        for(i=0;i<4;i++)for(j=0;j<3;j++) {
            vertex[i][j]=enemy_float(row+4+(i*3+j)*4);
            if(!isfinite(vertex[i][j]))return -1;
        }
        for(j=0;j<3;j++) {
            float sum=table_round_float(vertex[0][j]+vertex[1][j]);
            sum=table_round_float(sum+vertex[2][j]);sum=table_round_float(sum+vertex[3][j]);
            center[j]=table_round_float(sum/4.0f);
        }
        for(i=0;i<4;i++) {
            float squared[3],distance;
            for(j=0;j<3;j++){float delta=table_round_float(vertex[i][j]-center[j]);squared[j]=table_round_float(delta*delta);}
            distance=table_round_float(table_round_float(squared[0]+squared[1])+squared[2]);
            if(distance>radius)radius=distance;
        }
        radius=table_round_float(radius*1.2f);if(!isfinite(radius))return -1;
        memcpy(out,row,64);for(j=0;j<3;j++)table_put_float(out+64+j*4,center[j]);table_put_float(out+76,radius);
        put(out+80,index<=21 ? UINT32_MAX : index<=30 ? 10 : 11,4);
        put(out+84,(index==22 || index==27 || index==28 || index==31) ? 0x10000 : 0x20000,4);put(out+88,index,4);
    }
    return 0;
}
static int populate_water(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    static const uint32_t tables[]={0x8c17c504u,0x8c17c54cu,0x8c17c578u};unsigned group,written=0;
    if(r->section_count!=3 || r->sections[1].size!=10*144)return -1;
    for(group=0;group<3;group++) {
        unsigned i;
        for(i=0;i<20;i++) {
            uint32_t address;
            if(table_word(disc,exe,0x8c010000u,tables[group]+i*4,&address))return -1;
            if(!address)break;
            if(written==10 || table_bytes(disc,exe,0x8c010000u,address,data+r->sections[1].offset+written*144,144))return -1;
            written++;
        }
        if(i==20)return -1;
    }
    return written==10 ? 0 : -1;
}
static int populate_emitter_map(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    uint32_t texlist,table=0,count,pointers[8],sorted[8];JgrDiscFile textures;unsigned i;
    if(r->section_count!=1 || r->sections[0].size!=32 ||
       table_word(disc,exe,0x8c010000u,0x8c1d0ee8u,&texlist) || texlist>UINT32_MAX-4 ||
       table_word(disc,exe,0x8c010000u,texlist+4,&count) || count!=8 ||
       jgr_disc_source(disc,"decompile/afs/STAGELAST/000_lasttxr.bin",&textures))return -1;
    for(i=0;i<256;i++) {
        uint32_t current;
        if(table_word(disc,exe,0x8c010000u,0x8c1d0cb8u+i*8,&current) || !current)return -1;
        if(current==texlist){if(table_word(disc,exe,0x8c010000u,0x8c1d0cbcu+i*8,&table))return -1;break;}
    }
    if(!table)return -1;
    for(i=0;i<8;i++) {
        unsigned char header[8];uint64_t at;
        if((uint64_t)table+i*4>UINT32_MAX || table_word(disc,exe,0x8c010000u,table+i*4,pointers+i) ||
           table_bytes(disc,&textures,0x8c800000u,pointers[i],header,8) || memcmp(header,"GBIX",4))return -1;
        at=(uint64_t)pointers[i]+8+word(header+4);
        if(at>UINT32_MAX || table_bytes(disc,&textures,0x8c800000u,(uint32_t)at,header,4) || memcmp(header,"PVRT",4))return -1;
        sorted[i]=pointers[i];
    }
    /* GBIX indices increase with source byte offsets. Compact rank therefore
     * follows sorted source pointers without retaining a texture index table. */
    qsort(sorted,8,sizeof sorted[0],compare_word);
    for(i=0;i<8;i++){unsigned j;for(j=0;j<8;j++)if(sorted[j]==pointers[i])break;put(data+r->sections[0].offset+i*4,j,4);}
    return 0;
}
