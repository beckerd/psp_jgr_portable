/* Original UI strings and font glyphs are decoded from user data. The blank
 * atlas area and semantic menu selection below are native layout operations. */
static int garage_ui_font(JgrDisc *disc,const TableRecipe *r,unsigned char *data,const unsigned char used[256])
{
    JgrDiscFile source;unsigned char head[64],*payload=NULL,*pixels=NULL,chars[42];
    uint32_t colors[16]={0};unsigned n=0,color_count=1,pvr=0,length,w,h,i,x,y;int result=-1;
    const TableSection *s=r->sections;
    for(i=0;i<256;i++)if(used[i]){if(n==42)return -1;chars[n++]=(unsigned char)i;}
    if(n+1!=s[12].size || s[13].size!=64 || s[14].size!=8192 ||
       jgr_disc_source(disc,"decompile/afs/E_FONTBASIC/000_e_fontbasictxr.bin",&source) ||
       jgr_disc_read(disc,&source,0,head,sizeof head))return -1;
    while(pvr+16<=sizeof head && memcmp(head+pvr,"PVRT",4))pvr++;
    if(pvr+16>sizeof head)return -1;
    length=word(head+pvr+4);w=head[pvr+12]|(unsigned)head[pvr+13]<<8;h=head[pvr+14]|(unsigned)head[pvr+15]<<8;
    if(length<8 || length>1024*1024 || w!=512 || h!=512 || pvr+8u>source.size || length>source.size-pvr-8)return -1;
    length-=8;payload=(unsigned char *)malloc(length ? length : 1);if(!payload)return -1;
    if(jgr_disc_read(disc,&source,pvr+16,payload,length))goto done;
    pixels=jgr_pvr_decode_payload(payload,length,head[pvr+8],head[pvr+9],w,h);free(payload);payload=NULL;
    if(!pixels)goto done;
    memcpy(data+s[12].offset,chars,n);
    for(y=0;y<64;y++)for(x=0;x<256;x++) {
        unsigned tile=(y/24)*21+x/12,index=0;uint32_t color=0;
        if(x<252 && tile<n) {
            unsigned glyph=chars[tile],sx=(glyph%42)*12+x%12,sy=(glyph/42)*24+y%24;
            if(sx>=w || sy>=h)goto done;
            color=word(pixels+(sy*w+sx)*4);
        }
        for(index=0;index<color_count;index++)if(colors[index]==color)break;
        if(index==color_count){if(color_count==16)goto done;colors[color_count++]=color;}
        data[s[14].offset+(y*256+x)/2]|=(unsigned char)(index<<((x&1)*4));
    }
    for(i=0;i<color_count;i++)put(data+s[13].offset+i*4,colors[i],4);
    result=0;
done:free(pixels);free(payload);return result;
}
static int populate_garage_ui(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    static const unsigned name_ids[]={8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,24,26,22,24,26,22,24,26,65};
    JgrDiscFile garage;const TableSection *s=r->sections;unsigned slot,i,fixup=0,pool=0,quads=0,order=0;
    unsigned char glyphs[256]={0};uint32_t list,address;
    if(r->section_count!=16 || r->fixups!=61 || s[0].size!=15 || s[1].size!=180 || s[3].size!=24 ||
       s[8].size!=192 || s[9].size!=1640 || s[10].size!=82 ||
       jgr_disc_source(disc,"decompile/afs/U_GARAGE/001_garagedat.bin",&garage) ||
       table_word(disc,&garage,0x8c800000u,0x8c80034cu,&list))return -1;
    for(slot=0;slot<15;slot++) {
        uint32_t id,health,tech,capacity;double fast,maximum,turna,turnb;float power,paint,ratio,value;
        if(table_word(disc,exe,0x8c010000u,0x8c10e06cu+slot*4,&id) || id>=15 || (order&(1u<<id)) ||
           table_word(disc,exe,0x8c010000u,0x8c182884u+slot*4,&address) || address>UINT32_MAX-0xa4 ||
           table_word(disc,exe,0x8c010000u,address,&health) || table_word(disc,exe,0x8c010000u,address+8,&tech) ||
           table_word(disc,exe,0x8c010000u,address+12,&capacity) ||
           read_value(disc,exe,0x8c010000u,address+0x4c,'f',&fast) || read_value(disc,exe,0x8c010000u,address+0x68,'f',&maximum) ||
           read_value(disc,exe,0x8c010000u,address+0xa0,'f',&turna) || read_value(disc,exe,0x8c010000u,address+0xa4,'f',&turnb) || maximum==0)return -1;
        order|=1u<<id;data[s[0].offset+id]=(unsigned char)slot;
        power=table_round_float((float)(health/150.0));power=table_round_float(power*86.f);
        ratio=table_round_float((float)(fast/maximum));value=table_round_float((float)(1.0-ratio));
        value=table_round_float((float)((double)value*turnb));value=table_round_float((float)((double)value+turna));
        value=table_round_float(value*2.3f);value=table_round_float(value*86.f);
        paint=table_round_float((float)(((3.0-tech)*30.0+capacity-10.0)/100.0));paint=table_round_float(paint*86.f);
        table_put_float(data+s[1].offset+slot*12,power);table_put_float(data+s[1].offset+slot*12+4,value);table_put_float(data+s[1].offset+slot*12+8,paint);
    }
    if(order!=32767 || table_word(disc,exe,0x8c010000u,0x8c10e0dcu,&address) ||
       table_bytes(disc,exe,0x8c010000u,address,data+s[3].offset,24))return -1;
    for(i=0;i<5;i++) {
        unsigned section=i==0 ? 2 : i+3,count=i<2 ? 3 : i==2 ? 7 : 24,j;
        for(j=0;j<count;j++) {
            unsigned char text[256];unsigned length,k;uint32_t id;
            if(i==0)id=31+j;
            else if(i==1)id=69+j;
            else if(i==2){if(table_word(disc,exe,0x8c010000u,0x8c10dc14u+j*4,&id))return -1;}
            else if(i==3)id=name_ids[j];
            else id=j<14 ? 7 : j==23 ? 67 : 23+((j-14)%3)*2;
            if(id>1024 || (uint64_t)list+id*4>UINT32_MAX || table_word(disc,&garage,0x8c800000u,list+id*4,&address) ||
               table_string(disc,&garage,0x8c800000u,address,text,sizeof text,&length) ||
               table_string_fixup(r,data,fixup++,section,j,text,length,&pool))return -1;
            if(i==3)for(k=0;k+1<length;k++)glyphs[text[k]]=1;
        }
    }
    if(fixup!=61 || pool!=s[15].size)return -1;
    for(slot=0;slot<24;slot++) {
        unsigned char point[8];float y;
        if(garage_table(disc,exe,&garage,0x8c10dcb0u,slot,&address) || garage_position(disc,&garage,address,point,8))return -1;
        memcpy(data+s[8].offset+slot*8,point,4);y=-enemy_float(point+4);table_put_float(data+s[8].offset+slot*8+4,y);
    }
    for(slot=0;slot<41;slot++) {
        uint32_t cell;unsigned count;
        if(slot<15){if(table_word(disc,exe,0x8c010000u,0x8c10dfb8u+slot*4,&cell))return -1;}
        else if(slot<19)cell=0x8c800384u+(slot-15)*4;
        else if(slot==40)cell=0x8c8004bcu;
        else {
            uint32_t table=slot<26 ? 0x8c10dbf8u : slot<33 ? 0x8c10dc30u : 0x8c10dc4cu;
            if(table_word(disc,exe,0x8c010000u,table+((slot-19)%7)*4,&cell))return -1;
        }
        if(garage_quads(disc,&garage,cell,data+s[9].offset+quads*40,41-quads,&count,1))return -1;
        data[s[10].offset+slot*2]=(unsigned char)quads;data[s[10].offset+slot*2+1]=(unsigned char)count;quads+=count;
    }
    if(quads!=41 || table_word(disc,&garage,0x8c800000u,0x8c800480u,&address) ||
       table_word(disc,&garage,0x8c800000u,address,&address) || address>UINT32_MAX-4 ||
       table_bytes(disc,&garage,0x8c800000u,address+4,data+s[11].offset,4))return -1;
    return garage_ui_font(disc,r,data,glyphs);
}
