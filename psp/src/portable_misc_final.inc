/* Original animated final-stage collision hierarchy, strips and channels. */
typedef struct {uint32_t address,mesh,key;unsigned piece,nv,nt,first_vertex,first_triangle;int parent;unsigned char trs[36];} FinalNode;
typedef struct {Misc source;FinalNode nodes[768];unsigned count,vertices,triangles,keys,frames,first,piece;uint32_t motion_data;} FinalBuild;
static int final_walk(FinalBuild *b,uint32_t address,int parent,unsigned depth)
{
 if(depth>128)return -1;
 while(address){
  if(b->count>=768)return -1;
  for(unsigned i=b->first;i<b->count;i++)if(b->nodes[i].address==address)return -1;
  unsigned char raw[68];if(mread(&b->source,address,raw,sizeof raw))return -1;
  unsigned kind=mu16(raw);if(kind!=0&&kind!=6)return -1;
  unsigned index=b->count++;FinalNode *n=b->nodes+index;memset(n,0,sizeof *n);n->address=address;n->piece=b->piece;n->parent=parent;n->mesh=kind==6?mu32(raw+4):0;memcpy(n->trs,raw+12,36);
  n->first_vertex=b->vertices;n->first_triangle=b->triangles;
  if(n->mesh){
   unsigned char mesh[12];if(mread(&b->source,n->mesh,mesh,12))return -1;unsigned patches=mu16(mesh+10);n->nv=mu16(mesh+8);
   if(n->nv>1048576-b->vertices)return -1;
   b->vertices+=n->nv;
   for(unsigned p=0;p<patches;p++){unsigned flags=mw(&b->source,mu32(mesh+4)+p*36+28),triangles=flags&65535;if(triangles>1048576-b->triangles)return -1;b->triangles+=triangles;n->nt+=triangles;}
  }
  if(b->motion_data){
   unsigned char channel[16];if(mread(&b->source,b->motion_data+(index-b->first)*16,channel,16))return -1;
   unsigned flags=mu32(channel);if((flags!=0&&flags!=16)||mu32(channel+4)||mu32(channel+12))return -1;
   n->key=mu32(channel+8);if(n->key){if(flags!=16||b->keys>16*1024*1024-b->frames*6)return -1;b->keys+=b->frames*6;}
  }
  if(final_walk(b,mu32(raw+60),(int)index,depth+1))return -1;
  address=mu32(raw+64);
 }
 return b->source.failed?-1:0;
}
static int final_collect(FinalBuild *b,JgrDisc *d,int boss)
{
 memset(b,0,sizeof *b);if(open_misc(&b->source,d,"decompile/afs/STAGELAST/001_lastdat.bin",LAST_BASE))return -1;
 b->frames=boss?719:500;
 for(unsigned piece=0;piece<(boss?2u:6u);piece++){
  uint32_t root,motion;
  if(boss){root=mw(&b->source,piece?0x8c1d0ee4:0x8c1d0ed0);motion=piece?0:mw(&b->source,0x8c1d0ecc);}
  else{root=mw(&b->source,0x8c1d0dec+piece*4);motion=mw(&b->source,0x8c1d0e04+piece*4);}
  b->motion_data=0;if(motion){unsigned header=mw(&b->source,motion);if(header>>16!=b->frames||((header&65535)!=0x11&&(header&65535)!=0x15))return -1;b->motion_data=mw(&b->source,motion+4);}
  b->first=b->count;b->piece=piece;if(final_walk(b,root,-1,0))return -1;
 }
 return 0;
}
static int final_primary(FinalBuild *b,FILE *out)
{
 unsigned nodes_offset=40,vertices_offset=40+b->count*68,triangles_offset=vertices_offset+b->vertices*12,keys_offset=triangles_offset+b->triangles*20;
 uint32_t header[9]={b->count,b->vertices,b->triangles,b->frames,nodes_offset,vertices_offset,triangles_offset,keys_offset,keys_offset+b->keys};
 if(fwrite("FCL1",1,4,out)!=4)return -1;
 for(unsigned i=0;i<9;i++)if(mword(out,header[i]))return -1;
 unsigned key_at=keys_offset;
 for(unsigned i=0;i<b->count;i++){
  FinalNode *n=b->nodes+i;unsigned char row[68];mput(row,n->address);mput(row+4,n->piece);mput(row+8,(uint32_t)n->parent);mput(row+12,n->first_vertex);mput(row+16,n->nv);mput(row+20,n->first_triangle);mput(row+24,n->nt);memcpy(row+28,n->trs,36);mput(row+64,n->key?key_at:0);if(n->key)key_at+=b->frames*6;
  if(fwrite(row,1,68,out)!=68)return -1;
 }
 for(unsigned i=0;i<b->count;i++){FinalNode *n=b->nodes+i;if(n->mesh&&mcopy(&b->source,mw(&b->source,n->mesh),n->nv*12,out))return -1;}
 for(unsigned i=0;i<b->count;i++){
  FinalNode *n=b->nodes+i;if(!n->mesh)continue;unsigned char mesh[12];if(mread(&b->source,n->mesh,mesh,12))return -1;
  for(unsigned p=0;p<mu16(mesh+10);p++){
   unsigned char patch[12];if(mread(&b->source,mu32(mesh+4)+p*36+24,patch,12))return -1;
   unsigned ip=mu32(patch),flags=mu32(patch+4),count=flags&65535,wind=!!(flags&0x40000000);
   for(unsigned k=0;k<count;k++){
    unsigned char ids[6],row[20];if(mread(&b->source,ip+k*2,ids,6))return -1;unsigned v[3]={mu16(ids),mu16(ids+2),mu16(ids+4)};
    for(unsigned j=0;j<3;j++)if(v[j]>=n->nv)return -1;
    mput(row,n->first_vertex+v[0]);mput(row+4,n->first_vertex+v[wind?1:2]);mput(row+8,n->first_vertex+v[wind?2:1]);
    memcpy(row+12,patch+10,2);memcpy(row+14,patch+8,2);row[16]=(!!(flags&0x80000000))|(wind?0:2);row[17]=0;row[18]=i;row[19]=i>>8;
    if(fwrite(row,1,20,out)!=20)return -1;
    wind=!wind;
   }
  }
 }
 for(unsigned i=0;i<b->count;i++)if(b->nodes[i].key&&mcopy(&b->source,b->nodes[i].key,b->frames*6,out))return -1;
 return b->source.failed?-1:0;
}
static int final_proxy(Misc *m,uint32_t at,const FinalNode *nodes,unsigned count,unsigned *cursor,int parent,unsigned depth)
{
 if(depth>128)return -1;
 while(at){if(*cursor>=count)return -1;unsigned index=(*cursor)++;if(nodes[index].parent!=parent)return -1;unsigned char node[52];if(mread(m,at,node,52))return -1;
  if(final_proxy(m,mu32(node+44),nodes,count,cursor,(int)index,depth+1))return -1;
  at=mu32(node+48);
 }
 return 0;
}
typedef struct{uint32_t address,pointers[3],offsets[3];} FinalCollapseRow;
static int final_collapse_rows(FinalBuild *b,FILE *out,FinalCollapseRow *rows)
{
 unsigned count=0,bytes=0;
 for(unsigned piece=0;piece<4;piece++){
  unsigned motion=mw(&b->source,0x8c1d0e2c+piece*4),header=mw(&b->source,motion),md=mw(&b->source,motion+4);
  if(header>>16!=640||((header&65535)!=0x11&&(header&65535)!=0x15))return -1;
  unsigned first=count;while(count<b->count&&b->nodes[count].piece==piece)count++;unsigned n=count-first;
  FinalNode proxy_nodes[128];if(n>128)return -1;for(unsigned i=0;i<n;i++){proxy_nodes[i]=b->nodes[first+i];if(proxy_nodes[i].parent>=0)proxy_nodes[i].parent-=first;}
  unsigned cursor=0;if(final_proxy(&b->source,mw(&b->source,0x8c1d0e3c+piece*4),proxy_nodes,n,&cursor,-1,0)||cursor!=n)return -1;
  for(unsigned i=0;i<n;i++){
   unsigned char channel[16];if(mread(&b->source,md+i*16,channel,16))return -1;unsigned flags=mu32(channel);if(flags&~0x15)return -1;
   FinalCollapseRow *r=rows+first+i;r->address=b->nodes[first+i].address;
   for(unsigned k=0;k<3;k++){
    unsigned bit=k==0?1:k==1?16:4,stride=k==1?6:12;r->pointers[k]=mu32(channel+4+k*4);
    if(!!(flags&bit)!=!!r->pointers[k])return -1;
    r->offsets[k]=bytes;if(r->pointers[k]){if(bytes>16*1024*1024-640*stride)return -1;bytes+=640*stride;}
   }
  }
 }
 unsigned key_offset=24+count*16;uint32_t head[5]={count,640,24,key_offset,key_offset+bytes};
 if(fwrite("FCO1",1,4,out)!=4)return -1;
 for(unsigned i=0;i<5;i++)if(mword(out,head[i]))return -1;
 for(unsigned i=0;i<count;i++){if(mword(out,rows[i].address))return -1;for(unsigned k=0;k<3;k++)if(mword(out,rows[i].pointers[k]?key_offset+rows[i].offsets[k]:0))return -1;}
 for(unsigned i=0;i<count;i++)for(unsigned k=0;k<3;k++)if(rows[i].pointers[k]&&mcopy(&b->source,rows[i].pointers[k],640*(k==1?6:12),out))return -1;
 return b->source.failed?-1:0;
}
static int final_collapse(FinalBuild *b,FILE *out)
{
 FinalCollapseRow *rows=malloc(768*sizeof *rows);if(!rows)return -1;
 int result=final_collapse_rows(b,out,rows);free(rows);return result;
}
static int final_collision(JgrDisc *d,const char *name,FILE *out)
{
 FinalBuild *b=malloc(sizeof *b);if(!b)return -1;int r=-1;
 if(final_collect(b,d,!strcmp(name,"finalbosscoll.bin")))goto done;
 r=!strcmp(name,"finalcollapse.bin")?final_collapse(b,out):final_primary(b,out);
 done:free(b);return r;
}
