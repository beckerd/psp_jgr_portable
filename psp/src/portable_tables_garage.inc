/* Original garage camera keys are sampled at setup using the same binary32
 * interpolation order as the existing converter. Setup never retains banks. */
static int garage_word(JgrDisc *disc,const JgrDiscFile *exe,const JgrDiscFile *garage,uint32_t address,uint32_t *out)
{
    if(address>=0x8c800000u && (uint64_t)address<0x8c800000ull+garage->size)
        return table_word(disc,garage,0x8c800000u,address,out);
    return table_word(disc,exe,0x8c010000u,address,out);
}
static int garage_camera_channel(JgrDisc *disc,const JgrDiscFile *garage,uint32_t address,unsigned count,
                                 unsigned stride,unsigned frames,unsigned component,unsigned char *out)
{
    unsigned char *keys=NULL;unsigned i,frame,lo=0;int result=-1;
    if(!count || count>frames || frames>2048 || (stride!=8 && stride!=16))return -1;
    keys=(unsigned char *)malloc(count*stride);if(!keys)return -1;
    if(table_bytes(disc,garage,0x8c800000u,address,keys,count*stride) || word(keys))goto done;
    for(i=1;i<count;i++)if(word(keys+(i-1)*stride)>=word(keys+i*stride))goto done;
    for(frame=0;frame<frames;frame++) {
        unsigned hi,j;uint32_t first,last;float t,g;
        while(lo+1<count && word(keys+(lo+1)*stride)<=frame)lo++;
        hi=(lo+1)%count;first=word(keys+lo*stride);last=word(keys+hi*stride);
        t=table_round_float((float)((double)(frame-first)/(double)(last>first ? last-first : 1)));
        g=table_round_float((float)(1.0-(double)t));
        for(j=0;j<(stride-4)/4;j++) {
            const unsigned char *a=keys+lo*stride+4+j*4,*b=keys+hi*stride+4+j*4;
            float av=stride==8 ? (float)(int32_t)word(a) : enemy_float(a);
            float bv=stride==8 ? (float)(int32_t)word(b) : enemy_float(b),value;
            if(!isfinite(av) || !isfinite(bv))goto done;
            value=table_round_float((float)((double)g*av+(double)table_round_float(t*bv)));
            if(!isfinite(value))goto done;
            if(stride==8) {
                if((double)value<INT32_MIN || (double)value>INT32_MAX)goto done;
                put(out+frame*32+(component+j)*4,(uint32_t)(int32_t)value,4);
            }else table_put_float(out+frame*32+(component+j)*4,value);
        }
    }
    result=0;
done:free(keys);return result;
}
static int populate_departure(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    JgrDiscFile garage;unsigned slot,section=0;
    if(r->section_count!=14 || r->fixups!=11 || r->sections[11].size!=60 || r->sections[12].size!=60 ||
       r->sections[13].size!=60 || jgr_disc_source(disc,"decompile/afs/U_GARAGE/001_garagedat.bin",&garage))return -1;
    for(slot=0;slot<15;slot++) {
        uint32_t motion,camera,address,length,channels,kind;unsigned i;unsigned char *fixup;
        if(table_word(disc,exe,0x8c010000u,0x8c105cd0u+slot*4,&address) || garage_word(disc,exe,&garage,address,&motion) ||
           table_word(disc,exe,0x8c010000u,0x8c105dfcu+slot*4,&address) || garage_word(disc,exe,&garage,address,&camera))return -1;
        if(motion){if(garage_word(disc,exe,&garage,motion,&length))return -1;put(data+r->sections[11].offset+slot*4,length>>16,4);}
        if(!camera)continue;
        if(section>=11 || camera>UINT32_MAX-8 || garage_word(disc,exe,&garage,camera,&channels) ||
           garage_word(disc,exe,&garage,camera+4,&length) || garage_word(disc,exe,&garage,camera+8,&kind) ||
           kind!=0x401c1u || length!=r->sections[section].size/32 || r->sections[section].size%32 || channels>UINT32_MAX-32)return -1;
        put(data+r->sections[12].offset+slot*4,length,4);
        for(i=0;i<4;i++) {
            uint32_t count;
            if(garage_word(disc,exe,&garage,channels+i*4,&address) || garage_word(disc,exe,&garage,channels+16+i*4,&count) ||
               garage_camera_channel(disc,&garage,address,count,i<2 ? 16 : 8,length,i<2 ? i*3 : i+4,data+r->sections[section].offset))return -1;
        }
        fixup=data+40+16*r->section_count+16*section;
        put(fixup,13,4);put(fixup+4,slot*4,4);put(fixup+8,section,4);section++;
    }
    return section==11 ? 0 : -1;
}
static int garage_table(JgrDisc *disc,const JgrDiscFile *exe,const JgrDiscFile *garage,uint32_t table,unsigned index,uint32_t *out)
{
    uint32_t pointer;uint64_t address=(uint64_t)table+index*4;
    if(address>UINT32_MAX || table_word(disc,exe,0x8c010000u,(uint32_t)address,&pointer))return -1;
    return garage_word(disc,exe,garage,pointer,out);
}
static int garage_position(JgrDisc *disc,const JgrDiscFile *garage,uint32_t address,unsigned char *out,unsigned bytes)
{return address>UINT32_MAX-8 ? -1 : table_bytes(disc,garage,0x8c800000u,address+8,out,bytes);}
static int garage_dense_camera(JgrDisc *disc,const JgrDiscFile *garage,uint32_t cell,unsigned frames,
                               unsigned char *positions,unsigned char *roll,unsigned char *fov,unsigned constant)
{
    uint32_t camera,channels,count,kind,pointers[4];unsigned j,k;
    if(table_word(disc,garage,0x8c800000u,cell,&camera) || camera>UINT32_MAX-8 ||
       table_word(disc,garage,0x8c800000u,camera,&channels) || channels>UINT32_MAX-32 ||
       table_word(disc,garage,0x8c800000u,camera+4,&count) || count!=frames ||
       table_word(disc,garage,0x8c800000u,camera+8,&kind) || kind!=0x401c1u)return -1;
    for(j=0;j<4;j++) {
        if(table_word(disc,garage,0x8c800000u,channels+j*4,pointers+j) ||
           table_word(disc,garage,0x8c800000u,channels+16+j*4,&count) || count!=(constant && j>=2 ? 2 : frames))return -1;
        for(k=0;k<count;k++) {
            unsigned char entry[16];unsigned stride=j<2 ? 16 : 8;uint64_t at=(uint64_t)pointers[j]+k*stride;
            if(at>UINT32_MAX || table_bytes(disc,garage,0x8c800000u,(uint32_t)at,entry,stride) ||
               (!(constant && j>=2) && word(entry)!=k))return -1;
            if(j<2)memcpy(positions+k*24+j*12,entry+4,12);
            else if(constant) {
                if(j==2){if(word(entry+4))return -1;}
                else if(k==0)memcpy(fov,entry+4,4);
                else if(memcmp(fov,entry+4,4))return -1;
            }else memcpy((j==2 ? roll : fov)+k*4,entry+4,4);
        }
    }
    return 0;
}
static int garage_quads(JgrDisc *disc,const JgrDiscFile *garage,uint32_t cell,unsigned char *out,
                        unsigned capacity,unsigned *written,unsigned allow_vertical)
{
    uint32_t header,first,count;unsigned i;
    *written=0;if(!cell)return 0;
    if(table_word(disc,garage,0x8c800000u,cell,&header) || header>UINT32_MAX-4 ||
       table_word(disc,garage,0x8c800000u,header,&first) ||
       table_word(disc,garage,0x8c800000u,header+4,&count) || count>capacity)return -1;
    for(i=0;i<count;i++) {
        unsigned char row[64],*destination=out+i*40;uint64_t at=(uint64_t)first+i*64;unsigned flags;
        if(at>UINT32_MAX || table_bytes(disc,garage,0x8c800000u,(uint32_t)at,row,64))return -1;
        flags=row[2]|(unsigned)row[3]<<8;
        if(row[20] || row[21] || (flags&~(allow_vertical ? 24u : 16u))!=0x2580u ||
           word(row+32)!=word(row+36) || word(row+32)!=word(row+40) || word(row+32)!=word(row+44))return -1;
        put(destination,row[0]|(unsigned)row[1]<<8,4);memcpy(destination+4,row+32,4);memcpy(destination+8,row+4,16);
        memcpy(destination+24,row+48+((flags&16) ? 8 : 0),4);
        memcpy(destination+28,row+52+((flags&8) ? 8 : 0),4);
        memcpy(destination+32,row+48+((flags&16) ? 0 : 8),4);
        memcpy(destination+36,row+52+((flags&8) ? 0 : 8),4);
    }
    *written=count;return 0;
}
static int populate_garage_cast(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    JgrDiscFile garage;unsigned slot,path,used=0,order=0;uint32_t address;const TableSection *s=r->sections;
    if(r->section_count!=21 || s[0].size!=480 || s[19].size!=800 || s[20].size!=36 ||
       jgr_disc_source(disc,"decompile/afs/U_GARAGE/001_garagedat.bin",&garage))return -1;
    for(slot=0;slot<15;slot++) {
        unsigned char *out=data+s[0].offset+slot*32;unsigned char position[24]={0};uint32_t motion,length,id;
        if(garage_table(disc,exe,&garage,0x8c105b68u,slot,&address) ||
           (address && garage_position(disc,&garage,address,position,24)) || word(position+12) || word(position+20))return -1;
        put(out,slot+1,4);put(out+4,UINT32_MAX,4);memcpy(out+8,position,12);memcpy(out+20,position+16,4);
        for(path=0;path<2;path++) {
            if(garage_table(disc,exe,&garage,0x8c105c58u+path*60,slot,&motion))return -1;
            if(motion){if(garage_word(disc,exe,&garage,motion,&length))return -1;put(out+24+path*4,length>>16,4);}
        }
        if(table_word(disc,exe,0x8c010000u,0x8c10e06cu+slot*4,&id) || id>=15 || (order&(1u<<id)))return -1;
        order|=1u<<id;data[s[14].offset+id]=(unsigned char)slot;
        for(path=0;path<2;path++)
            if(garage_table(disc,exe,&garage,path ? 0x8c10e030u : 0x8c10dff4u,slot,&address) ||
               garage_position(disc,&garage,address,data+s[15].offset+slot*24+path*12,12))return -1;
    }
    put(data+s[1].offset,16,4);
    for(slot=0;slot<3;slot++) {
        uint32_t length;
        put(data+s[2].offset+slot*4,17+slot,4);
        if(table_word(disc,&garage,0x8c800000u,0x8c8000acu+slot*4,&address) ||
           table_word(disc,&garage,0x8c800000u,address,&length))return -1;
        put(data+s[3].offset+slot*4,length>>16,4);
    }
    {
        unsigned char position[24];
        if(table_word(disc,&garage,0x8c800000u,0x8c800098u,&address) || garage_position(disc,&garage,address,position,24) ||
           word(position+12) || word(position+20))return -1;
        memcpy(data+s[4].offset,position,12);put(data+s[5].offset,word(position+16)+0x444u,4);
    }
    for(path=0;path<2;path++) {
        if(s[8+path*2].size%24 || garage_dense_camera(disc,&garage,0x8c800008u+path*4,s[8+path*2].size/24,
           data+s[8+path*2].offset,NULL,data+s[9+path*2].offset,1))return -1;
        for(slot=0;slot<2;slot++)
            if(garage_table(disc,exe,&garage,path ? 0x8c21a788u : 0x8c21a780u,slot,&address) ||
               garage_position(disc,&garage,address,data+s[18].offset+slot*24+path*12,12))return -1;
    }
    for(slot=0;slot<18;slot++) {
        uint32_t cell;unsigned count;
        if(slot<3)cell=0x8c800000u+(slot==0 ? 0x3a0 : slot==1 ? 0x360 : 0x374);
        else if(table_word(disc,exe,0x8c010000u,0x8c10df7cu+(slot-3)*4,&cell))return -1;
        if(garage_quads(disc,&garage,cell,data+s[19].offset+used*40,20-used,&count,0))return -1;
        data[s[20].offset+slot*2]=(unsigned char)used;data[s[20].offset+slot*2+1]=(unsigned char)count;used+=count;
    }
    return used==20 && order==32767 ? 0 : -1;
}
static int populate_pending(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    static const unsigned residents[]={3,4,5,6,7,8,9,12};
    static const unsigned text_offsets[]={0x8c,0xa4,0x90,0x9c,0x98,0xa0,0x94,0x94,0x94,0x94};
    static const uint32_t cells[]={0x8c04c364u,0x8c04c374u,0x8c04c3c8u,0x8c04c3dcu};
    JgrDiscFile garage;const TableSection *s=r->sections;unsigned i,used=0,strings=0;uint32_t address,list;
    if(r->section_count!=10 || r->fixups!=13 || s[0].size!=15 || s[3].size!=244*24 ||
       s[4].size!=244*4 || s[5].size!=244*4 || s[7].size!=9*40 || s[8].size!=40 ||
       jgr_disc_source(disc,"decompile/afs/U_GARAGE/001_garagedat.bin",&garage))return -1;
    memset(data+s[0].offset,255,15);
    for(i=0;i<8;i++) {
        uint32_t motion,length;
        if(garage_table(disc,exe,&garage,0x8c105d0cu,residents[i],&motion) ||
           table_word(disc,&garage,0x8c800000u,motion,&length) || (length>>16)<=1)return -1;
        data[s[0].offset+residents[i]]=(unsigned char)i;put(data+s[1].offset+residents[i]*4,length>>16,4);
    }
    if(table_word(disc,&garage,0x8c800000u,0x8c80009cu,&address) || garage_position(disc,&garage,address,data+s[2].offset,12) ||
       garage_dense_camera(disc,&garage,0x8c80019cu,244,data+s[3].offset,data+s[4].offset,data+s[5].offset,0) ||
       table_word(disc,&garage,0x8c800000u,0x8c80034cu,&list))return -1;
    for(i=0;i<13;i++) {
        unsigned char text[256]={0};unsigned length=1;
        if(i>=3 && ((uint64_t)list+text_offsets[i-3]>UINT32_MAX ||
           table_word(disc,&garage,0x8c800000u,list+text_offsets[i-3],&address) ||
           table_string(disc,&garage,0x8c800000u,address,text,sizeof text,&length)))return -1;
        if(table_string_fixup(r,data,i,6,i,text,length,&strings))return -1;
    }
    for(i=0;i<5;i++) {
        unsigned count;
        if(i<4){if(table_word(disc,exe,0x8c010000u,cells[i],&address))return -1;}
        else address=0x8c800418u;
        if(garage_quads(disc,&garage,address,data+s[7].offset+used*40,9-used,&count,0))return -1;
        put(data+s[8].offset+i*8,used,4);put(data+s[8].offset+i*8+4,count,4);used+=count;
    }
    return used==9 && strings==s[9].size ? 0 : -1;
}
