/* Original sector ownership and far hierarchies. Near/far vertex data is
 * decoded from source files; this table describes only loader addresses. */
static const unsigned sector_counts[7]={0,63,114,46,0,71,81};
static const uint32_t sector_tables[7]={0,0x8c174424,0x8c17b58c,0x8c176e18,0,0x8c1fbadc,0x8c201604};
static const uint32_t far_texlists[7][4]={
 {0},{0x8c106b70,0x8c107064,0x8c1073b4,0},
 {0,0x8c109ac4,0x8c108b5c,0},
 {0x8c1bb0a4,0,0,0},{0},
 {0x8c203660,0x8c203eb4,0x8c203b28,0},
 {0x8c20b1f8,0,0x8c20b358,0}};
static double ef(Stage *s,uint32_t at){uint32_t u=ew(s,at);float f;memcpy(&f,&u,4);return f;}
static int ownership_rule(Stage *s,Owner *r)
{
 unsigned tag=r->place.category,pid=r->place.pid,sector=r->place.sector;double lod=0;unsigned kind=0;
 switch(s->stage){
 case 1:
  kind=tag==0?4:tag==2?5:0;lod=tag<=1?360000:tag==2?90000:0;
  /* The original knockable handlers own these DYN types. */
  if(tag==3&&(pid==4||pid==5||pid==10||pid==12||pid==13||pid==14||pid==15||pid==16||pid==17||pid==18||pid==19||pid==20||pid==21||pid==22||pid==27))kind=9;
  break;
 case 2:kind=tag==0?2:tag==1||tag==2?1:0;lod=tag==1||tag==2?90000:0;break;
 case 3:
  if(tag<=1){uint32_t fn=ew(s,0x8c17f958+sector*4);if(fn==0)kind=0;else if(fn==0x8c0c3498)kind=1;else if(fn==0x8c0c34c0)kind=2;else if(fn==0x8c0c3470)kind=3;else return -1;}
  if(tag==0)lod=ef(s,0x8c17fac8+sector*4);
  break;
 case 5:if(tag<3){lod=ef(s,tag==0?0x8c2045f8:tag==1?0x8c204600:0x8c2045fc);lod*=lod;}break;
 case 6:kind=tag==0?(sector==0?0:pid<4?2:1):tag==2?1:0;if(tag==0||tag==2){lod=ef(s,tag==0?0x8c20bb48:0x8c20bb50);lod*=lod;}break;
 default:return -1;
 }
 if(!isfinite(lod)||lod<0)return -1;
 r->kind=kind;r->lod=lod;return 0;
}
static int source_walk(Stage *s,uint32_t root,const double parent[16])
{
 typedef struct{uint32_t address;double matrix[16];} Pending;
 Pending *stack=malloc(128*sizeof *stack);uint32_t *seen=malloc(4096*sizeof *seen);unsigned sp=0,n=0;int result=-1;
 if(!stack||!seen)goto done;
 stack[sp].address=root;memcpy(stack[sp++].matrix,parent,128);
 while(sp){Pending p=stack[--sp];JgrModelNode node;double local[16],world[16];if(!p.address)continue;
  if(n>=4096||jgr_model_node(s->disc,p.address>=SB?&s->source:&s->exe,p.address>=SB?SB:EB,p.address,&node))goto done;
  for(unsigned i=0;i<n;i++)if(seen[i]==p.address)goto done;
  seen[n]=p.address;jgr_model_node_matrix(local,&node,0);jgr_model_multiply(world,p.matrix,local);
  if(visit(&node,world,n,-1,s))goto done;
  if(node.sibling){if(sp>=128)goto done;stack[sp].address=node.sibling;memcpy(stack[sp++].matrix,p.matrix,128);}
  if(node.child&&!(node.eval&16)){if(sp>=128)goto done;stack[sp].address=node.child;memcpy(stack[sp++].matrix,world,128);}n++;
 }
 result=0;
 done:free(stack);free(seen);return result;
}
static int retire_root(Stage *s,uint32_t root)
{
 long object_pos=ftell(s->sections[0]),batch_pos=ftell(s->sections[1]);unsigned char obj[32],batch[16];
 if(object_pos<0||batch_pos<0)return -1;
 for(unsigned i=0;i<s->near_objects;i++)if(s->origins[i].root==root&&s->origins[i].kind==0){
  if(fseek(s->sections[0],i*32,SEEK_SET)||fread(obj,1,32,s->sections[0])!=32)return -1;
  unsigned first=word(obj+16),count=word(obj+20);encode(obj+20,0);
  if(fseek(s->sections[0],i*32,SEEK_SET)||fwrite(obj,1,32,s->sections[0])!=32)return -1;
  for(unsigned j=first;j<first+count;j++){
   if(fseek(s->sections[1],j*16,SEEK_SET)||fread(batch,1,16,s->sections[1])!=16)return -1;
   memset(batch+8,0,4);if(fseek(s->sections[1],j*16,SEEK_SET)||fwrite(batch,1,16,s->sections[1])!=16)return -1;
  }
 }
 return fseek(s->sections[0],object_pos,SEEK_SET)||fseek(s->sections[1],batch_pos,SEEK_SET)?-1:0;
}
static int extra_geometry(Stage *s,unsigned table,unsigned near,unsigned tex,unsigned kind,unsigned stride,unsigned count)
{
 Category category={table,near,0,stride,count};Placements p={0};int result=-1;
 if(placements(s,&p,&category,1,2,0))goto done;
 for(unsigned i=0;i<p.count;i++){
  Placement *row=p.rows+i;unsigned root=row->root;int list=list_id(s,ew(s,tex+row->pid*4),0);if(list<0)goto done;
  if(add_owner(s,row))goto done;
  Owner *r=s->owners+s->nowners-1;r->near_first=0;r->far_first=s->objects;r->kind=kind;
  s->root=root;s->root_list=list;s->ground=0;s->post=NULL;s->lighting=NULL;double matrix[16];placed(matrix,row,1,s->stage==2);
  if(source_walk(s,root,matrix))goto done;
  r->far_count=s->objects-r->far_first;
  if(s->stage==1&&retire_root(s,root))goto done;
 }
 result=0;
 done:free(p.rows);return result;
}
static int city_lod(Stage *s)
{
 s->near_objects=s->objects;
 /* Preserve near texture order before appending far-only source materials. */
 for(unsigned i=0;i<=s->ntex;i++)if(s->used[i]&&write_texture(s,i))return -1;
 double low[3],high[3];memcpy(low,s->low,sizeof low);memcpy(high,s->high,sizeof high);
 unsigned near_records=s->nowners;s->object_flags=2;s->force_list=1;s->origin_kind=3;
 for(unsigned i=0;i<near_records;i++){
  Owner *r=s->owners+i;const Placement *p=&r->place;const Category *cat=&categories[s->stage][p->category];
  s->record=i+1;r->far_first=s->objects;if(ownership_rule(s,r))return -1;
  if(!cat->far){r->lod=0;continue;}
  unsigned root=ew(s,cat->far+p->pid*4),near=ew(s,cat->near+p->pid*4);
  if(root==near){r->lod=0;continue;}
  int list=list_id(s,ew(s,far_texlists[s->stage][p->category]+p->pid*4),0);
  s->root_list=list<0?MAX_LIST:(unsigned)list;s->ground=p->category==1;s->root=root;
  double matrix[16],post[16],light[16];placement_matrices(s,p,matrix,post,light);
  if(source_walk(s,root,matrix))return -1;
  r->far_count=s->objects-r->far_first;
 }
 if(s->stage==1){
  if(extra_geometry(s,0x8c105e54,0x8c1063b4,ew(s,0x8c030ffc),6,40,165)||extra_geometry(s,0x8c105e58,0x8c106e04,ew(s,0x8c031180),7,40,2))return -1;
 }
 if(s->stage==2&&extra_geometry(s,0x8c107ce8,0x8c1086a0,0x8c108834,8,52,101))return -1;
 memcpy(s->low,low,sizeof low);memcpy(s->high,high,sizeof high);return 0;
}
static int write_ownership(Stage *s,FILE *out)
{
 if(!out)return -1;
 unsigned ns=sector_counts[s->stage];uint32_t head[7]={s->stage,ns,s->nowners,s->near_objects,s->objects,s->batches,0};
 if(fwrite("JRS1",1,4,out)!=4||words(out,head,7))return -1;
 for(unsigned i=0;i<ns;i++){
  unsigned char mask[16]={0};mask[i/8]|=1u<<(i&7);uint32_t at=ew(s,sector_tables[s->stage]+i*8+4);unsigned limit=0;
  for(;;at+=4){if(++limit>128||!valid_code(s,at,4))return -1;unsigned v=ew(s,at);if(v>=0x80000000)break;if(v>=ns)return -1;mask[v/8]|=1u<<(v&7);}
  if(fwrite(mask,1,16,out)!=16)return -1;
 }
 for(unsigned i=0;i<s->nowners;i++){
  Owner *r=s->owners+i;unsigned char row[32]={0};unsigned values[4]={r->near_first,r->near_count,r->far_first,r->far_count};
  for(unsigned j=0;j<4;j++){if(values[j]>65535)return -1;row[j*2]=values[j];row[j*2+1]=values[j]>>8;}
  for(unsigned j=0;j<3;j++)real(row+8+4*j,r->place.position[j]);
  row[20]=r->place.sector;row[21]=r->place.sector>>8;row[22]=r->kind;real(row+24,r->lod);encode(row+28,r->place.address);
  if(fwrite(row,1,32,out)!=32)return -1;
 }
 return 0;
}
