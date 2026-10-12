/* Original animated water texlist, losslessly packed into a shared palette. */
static unsigned char *water_pixels(Stage *s,unsigned index)
{
 Pack *pack=NULL;
 for(unsigned i=0;i<s->npacks;i++)if(index>=s->packs[i].first&&index-s->packs[i].first<s->packs[i].count){pack=s->packs+i;break;}
 if(!pack)return NULL;
 unsigned at=pack->offset[index-pack->first],n=pack->source.size-at;unsigned char head[80];if(n>sizeof head)n=sizeof head;
 if(jgr_disc_read(s->disc,&pack->source,at,head,n))return NULL;
 unsigned pv=8;while(pv+16<=n&&memcmp(head+pv,"PVRT",4))pv++;
 if(pv+16>n||word(head+pv+4)<8||word(head+pv+4)>4*1024*1024||
    (head[pv+12]|(unsigned)head[pv+13]<<8)!=64||(head[pv+14]|(unsigned)head[pv+15]<<8)!=64)return NULL;
 unsigned bytes=word(head+pv+4)-8;if(bytes>pack->source.size-at-pv-16)bytes=pack->source.size-at-pv-16;
 unsigned char *raw=malloc(bytes?bytes:1),*pixels=NULL;if(!raw)return NULL;
 if(!jgr_disc_read(s->disc,&pack->source,at+pv+16,raw,bytes))pixels=jgr_pvr_decode_payload(raw,bytes,head[pv+8],head[pv+9],64,64);
 free(raw);return pixels;
}
static int water_color_order(const void *a,const void *b){return memcmp(a,b,4);}
int jgr_stage_surface(JgrDisc *disc,FILE *out)
{
 Stage s;memset(&s,0,sizeof s);s.disc=disc;s.stage=2;
 unsigned char *images[16]={0},palette[1024]={0},header[64]={0},frame[4096];unsigned count=0;int result=-1;
 if(!disc||!out||jgr_disc_find(disc,"1ST_READ.BIN",&s.exe)||s.exe.size>4*1024*1024)return -1;
 s.code=malloc(s.exe.size);s.packs=calloc(128,sizeof *s.packs);s.lists=calloc(MAX_LIST,sizeof *s.lists);s.nodes=calloc(MAX_NODE,sizeof *s.nodes);
 if(!s.code||!s.packs||!s.lists||!s.nodes||jgr_disc_read(disc,&s.exe,0,s.code,s.exe.size)||textures(&s))goto done;
 int a=list_id(&s,0x8c1a8108,0),b=list_id(&s,0x8c1a8210,0);
 if(a<0||b<0||s.lists[a].count!=16||s.lists[b].count!=16||memcmp(s.lists[a].map,s.lists[b].map,32))goto done;
 for(unsigned i=0;i<16;i++){
  images[i]=water_pixels(&s,s.lists[a].map[i]);if(!images[i])goto done;
  for(unsigned j=0;j<4096;j++){
   unsigned k;for(k=0;k<count;k++)if(!memcmp(palette+k*4,images[i]+j*4,4))break;
   if(k==count){if(count==256)goto done;memcpy(palette+count++*4,images[i]+j*4,4);}
  }
 }
 qsort(palette,count,4,water_color_order);
 memcpy(header,"KWS1",4);encode(header+4,16);encode(header+8,64);encode(header+12,64);
 encode(header+16,60);encode(header+20,64);encode(header+24,1088);encode(header+28,66624);
 if(fwrite(header,1,64,out)!=64||fwrite(palette,1,1024,out)!=1024)goto done;
 for(unsigned i=0;i<16;i++){
  unsigned at=0;
  for(unsigned y=0;y<64;y+=8)for(unsigned x=0;x<64;x+=16)for(unsigned dy=0;dy<8;dy++)for(unsigned dx=0;dx<16;dx++){
   unsigned char *color=images[i]+((y+dy)*64+x+dx)*4;unsigned k;
   for(k=0;k<count;k++)if(!memcmp(palette+k*4,color,4))break;
   if(k==count)goto done;
   frame[at++]=k;
  }
  if(fwrite(frame,1,sizeof frame,out)!=sizeof frame)goto done;
 }
 result=0;
 done:for(unsigned i=0;i<16;i++)free(images[i]);
 for(unsigned i=0;i<s.npacks;i++)free(s.packs[i].offset);
 free(s.code);free(s.packs);free(s.lists);free(s.nodes);return result;
}
