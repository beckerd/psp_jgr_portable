static const char *trail_sources[16]={
 "decompile/afs/PLAYER/001_bistxr.bin",
 "decompile/afs/PLAYER/003_codetxr.bin",
 "decompile/afs/PLAYER/005_conpotxr.bin",
 "decompile/afs/PLAYER/007_corntxr.bin",
 "decompile/afs/PLAYER/009_erekitxr.bin",
 "decompile/afs/PLAYER/011_gojitxr.bin",
 "decompile/afs/PLAYER/013_gumtxr.bin",
 "decompile/afs/PLAYER/015_nattxr.bin",
 "decompile/afs/PLAYER/019_plugtxr.bin",
 "decompile/afs/PLAYER/021_potstxr.bin",
 "decompile/afs/PLAYER/023_recotxr.bin",
 "decompile/afs/PLAYER/025_rival_lshockertxr.bin",
 "decompile/afs/PLAYER/027_rival_ntanktxr.bin",
 "decompile/afs/PLAYER/029_rival_pjamtxr.bin",
 "decompile/afs/PLAYER/031_rival_snpjamtxr.bin",
 "decompile/afs/PLAYER/033_yoyotxr.bin",
};
static int skate_trails(JgrDisc *d,FILE *out)
{
 unsigned char *atlas=malloc(256*128*2);if(!atlas)return -1;int result=-1;
 if(fwrite("TEX1",1,4,out)!=4||mword(out,2))goto done;
 for(unsigned bank=0;bank<2;bank++){
  memset(atlas,0,256*128*2);
  for(unsigned tile=0;tile<8;tile++){
   Misc m;if(open_misc(&m,d,trail_sources[bank*8+tile],0))goto done;
   unsigned record=mw(&m,8),list=mw(&m,record+0x44),count=mw(&m,list+4),at=mw(&m,list+8);
   unsigned w,h;unsigned char *pixels=NULL;if(m.failed||count!=1||texture_pixels(d,&m.data,at,&pixels,&w,&h))goto done;
   if(w!=64||h!=64){free(pixels);goto done;}
   for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
    unsigned char *p=pixels+(y*64+x)*4;unsigned v=p[0]>>4|(p[1]>>4)<<4|(p[2]>>4)<<8|(p[3]>>4)<<12;
    unsigned target=((tile/4*64+y)*256+tile%4*64+x)*2;atlas[target]=v;atlas[target+1]=v>>8;
   }
   free(pixels);
  }
  unsigned char dimensions[4]={0,1,128,0};if(fwrite(dimensions,1,4,out)!=4||fwrite(atlas,1,256*128*2,out)!=256*128*2)goto done;
 }
 result=0;
 done:free(atlas);return result;
}
typedef struct {uint32_t address,model,vertices,indices;unsigned nv,ni,strips;int parent;unsigned char row[60];} ShadowNode;
typedef struct {Misc source;ShadowNode nodes[960];unsigned count,nv,ni,first;} Shadows;
static int shadow_walk(Shadows *s,uint32_t at,int parent,unsigned depth)
{
 if(depth>64)return -1;
 while(at){
  if(s->count>=960||s->count-s->first>=64)return -1;
  for(unsigned i=s->first;i<s->count;i++)if(s->nodes[i].address==at)return -1;
  unsigned index=s->count++;ShadowNode *n=s->nodes+index;memset(n,0,sizeof *n);n->address=at;n->parent=parent;
  unsigned char raw[52];if(mread(&s->source,at,raw,52))return -1;n->model=mu32(raw+4);n->row[0]=parent;n->row[1]=parent>>8;n->row[2]=raw[0];n->row[3]=raw[1];memcpy(n->row+4,raw+8,36);mput(n->row+40,s->nv);mput(n->row+48,s->ni);
  if(n->model){
   unsigned vl=mw(&s->source,n->model),pl=mw(&s->source,n->model+4);unsigned char vertex[8],poly[6];
   if(mread(&s->source,vl,vertex,8)||mread(&s->source,pl,poly,6))return -1;
   n->nv=mu16(vertex+6);n->strips=mu16(poly+4);n->vertices=vl+8;n->indices=pl+6;
   if(mu16(vertex)!=34||mu16(vertex+4)||!n->nv||n->nv>32||mu16(vertex+2)!=1+3*n->nv||mu16(poly)!=58||!n->strips||n->strips>64)return -1;
   unsigned cursor=pl+6;
   for(unsigned strip=0;strip<n->strips;strip++){
    unsigned char v[2];if(mread(&s->source,cursor,v,2))return -1;int count=(int16_t)mu16(v);count=count<0?-count:count;if(count<3||count>4096)return -1;cursor+=2;
    for(int j=0;j<count;j++){if(mread(&s->source,cursor,v,2)||mu16(v)>=n->nv)return -1;cursor+=2;}n->ni+=count+1;
   }
   unsigned end=pl+4+mu16(poly+2)*2;if(cursor>end||end-cursor>4)return -1;
   while(cursor<end){unsigned char c;if(mread(&s->source,cursor++,&c,1)||c)return -1;}
   s->nv+=n->nv;s->ni+=n->ni;
  }
  mput(n->row+44,n->nv);mput(n->row+52,n->ni);mput(n->row+56,n->strips);
  if(shadow_walk(s,mu32(raw+44),(int)(index-s->first),depth+1))return -1;
  at=mu32(raw+48);
 }
 return s->source.failed?-1:0;
}
typedef struct {const ShadowNode *nodes;unsigned count,cursor;} ShadowCheck;
static int shadow_parent(const JgrModelNode *n,const double matrix[16],unsigned ordinal,int parent,void *opaque)
{(void)n;(void)matrix;(void)ordinal;ShadowCheck *c=opaque;if(c->cursor>=c->count||c->nodes[c->cursor++].parent!=parent)return -1;return 0;}
static int garage_shadow(JgrDisc *d,FILE *out)
{
 Shadows *s=calloc(1,sizeof *s);if(!s)return -1;int result=-1;unsigned ranges[15][2];
 if(open_misc(&s->source,d,"decompile/afs/U_GARAGE/001_garagedat.bin",0x8c800000))goto done;
 for(unsigned slot=0;slot<15;slot++){
  unsigned root=mw(&s->source,mw(&s->source,0x8c105c1c+slot*4)),body=mw(&s->source,mw(&s->source,0x8c105ba4+slot*4));s->first=s->count;ranges[slot][0]=s->count;
  if(shadow_walk(s,root,-1,0))goto done;
  ranges[slot][1]=s->count-s->first;
  if(ranges[slot][1]){double identity[16];jgr_model_identity(identity);ShadowCheck check={s->nodes+s->first,ranges[slot][1],0};if(jgr_model_walk(d,&s->source.data,s->source.base,body,identity,0,1,shadow_parent,&check)||check.cursor!=check.count)goto done;}
 }
 unsigned on=32+15*8,ov=on+s->count*60,oi=ov+s->nv*12;unsigned head[7]={s->count,s->nv,s->ni,on,ov,oi,oi+s->ni*2};
 if(fwrite("GSH1",1,4,out)!=4)goto done;
 for(unsigned i=0;i<7;i++)if(mword(out,head[i]))goto done;
 for(unsigned i=0;i<15;i++)if(mword(out,ranges[i][0])||mword(out,ranges[i][1]))goto done;
 for(unsigned i=0;i<s->count;i++)if(fwrite(s->nodes[i].row,1,60,out)!=60)goto done;
 for(unsigned i=0;i<s->count;i++)if(s->nodes[i].nv&&mcopy(&s->source,s->nodes[i].vertices,s->nodes[i].nv*12,out))goto done;
 for(unsigned i=0;i<s->count;i++)if(s->nodes[i].ni&&mcopy(&s->source,s->nodes[i].indices,s->nodes[i].ni*2,out))goto done;
 result=s->source.failed?-1:0;
 done:free(s);return result;
}
static int enemy_audio(JgrDisc *d,FILE *out)
{
 static const uint32_t profiles[29]={0x1d43b4,0x1d4a98,0x1d4504,0x1d42b8,0x1d45ac,0x1d3f38,0x1d4c58,0x1d4cac,0x1d4e6c,0x1d502c,0x1d4cac,0x1d4e6c,0x1d502c,0x1d3cd0,0x1d3dcc,0x1d4cac,0x1d4e6c,0x1d502c,0x1d3b2c,0x1d3e90,0x1d40f8,0x1d468c,0x1d4aec,0x1d4b94,0x1d4718,0x1d4328,0x1d48a0,0x1d49d4,0x1d476c};
 Misc m;JgrDiscFile archive,files[256];unsigned char used[256]={0};unsigned count=0;
 if(open_misc(&m,d,"decompile/extracted/1ST_READ.BIN",EXE_BASE)||jgr_disc_find(d,"JETRADIO/E_SND_SE.AFS",&archive))return -1;
 for(unsigned kind=0;kind<25;kind++){
  unsigned source=mw(&m,0x8c1ba590+kind*4);if(source<27||source>55)continue;
  unsigned record=mw(&m,mw(&m,0x8c05bf94)+source*4);if(mw(&m,record)!=0x1d||mw(&m,record+32)!=0x52)continue;
  unsigned file=mw(&m,record+4),address=0x8c000000+profiles[source-27];
  if(mw(&m,address)>2||mw(&m,address+4)>18||mw(&m,address+24))continue;
  if(file>=256||jgr_disc_afs(d,&archive,file,files+file))return -1;
  unsigned char head[16];if(jgr_disc_read(d,files+file,0,head,16))return -1;unsigned programs=mu32(head+12);if(programs>65536)return -1;
  for(unsigned p=0;p<programs;p++)if(mw(&m,address+p*28)<=2&&mw(&m,address+p*28+4)<=18&&!mw(&m,address+p*28+24)){used[file]=1;break;}
 }
 used[66]=used[72]=1;
 if(m.failed)return -1;
 for(unsigned i=0;i<256;i++)if(used[i]){count++;if(jgr_disc_afs(d,&archive,i,files+i))return -1;}
 if(fwrite("CAS1",1,4,out)!=4||mword(out,count))return -1;
 unsigned offset=8+count*32;
 for(unsigned i=0;i<256;i++)if(used[i]){
  unsigned char row[32]={0};memcpy(row,"E_SND_SE",8);mput(row+16,i);mput(row+20,offset);mput(row+24,files[i].size);
  if(files[i].size>UINT32_MAX-offset||fwrite(row,1,32,out)!=32)return -1;
  offset+=files[i].size;
 }
 unsigned char bytes[4096];for(unsigned i=0;i<256;i++)if(used[i])for(unsigned at=0;at<files[i].size;){unsigned n=files[i].size-at;if(n>sizeof bytes)n=sizeof bytes;if(jgr_disc_read(d,files+i,at,bytes,n)||fwrite(bytes,1,n,out)!=n)return -1;at+=n;}
 return 0;
}
