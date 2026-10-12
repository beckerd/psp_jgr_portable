#include "portable_people_convert.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define BASE 0x8cad0000u
#define MAX_POSITION 4096u
#define MAX_CORNER 16384u
#define MAX_STRIP 2048u
static unsigned word(const unsigned char*p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static unsigned half(const unsigned char*p){return p[0]|(unsigned)p[1]<<8;}
static int signed_half(const unsigned char*p){unsigned n=half(p);return n>=32768?(int)n-65536:(int)n;}
static void w32(unsigned char*p,unsigned n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8));}
static void w16(unsigned char*p,unsigned n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);}
static void real(unsigned char*p,double n){float f=(float)n;unsigned bits;memcpy(&bits,&f,4);w32(p,bits);}
static double number(const unsigned char*p){unsigned bits=word(p);float f;memcpy(&f,&bits,4);return f;}
static int write(FILE*f,const void*p,unsigned n){return fwrite(p,1,n,f)==n?0:-1;}
static int pad(FILE*f){long at=ftell(f);if(at<0)return -1;
 while((unsigned long)at++&3)if(fputc(0,f)==EOF)return -1;
 return 0;}
typedef struct {unsigned char*data,*nodes;unsigned size,roots[128],special[128],nr,ns;} Image;
typedef struct {unsigned owner,source;unsigned char xyz[12];} Position;
typedef struct {unsigned position,source;unsigned char uv[8];} Corner;
typedef struct {unsigned owner,texture,flags,first,count,runtime_texture,runtime_flags,index;int signed_count;} Strip;
typedef struct {
 Position positions[MAX_POSITION];Corner corners[MAX_CORNER];Strip strips[MAX_STRIP];uint16_t slots[65536];
 unsigned seen[128],np,nc,ns,nb;unsigned char bones[128][40];
 unsigned order[MAX_STRIP],retained[MAX_POSITION];uint16_t positions_out[256][2],emissions[512][2],groups[128][8],indices[2048];unsigned npos,nv,ng,ni,source_vertices;
} Native;
typedef struct {unsigned char*data;unsigned size,h[16],stride;} Bank;
static int span(unsigned at,unsigned n,unsigned size){return at<=size&&n<=size-at;}
static const unsigned char *source(const Image*i,unsigned a,unsigned n){return a<BASE||!span(a-BASE,n,i->size)?NULL:i->data+(a-BASE);}
static int is_node(const Image*i,unsigned a){return a>=BASE&&!(a&3)&&a-BASE<i->size&&(i->nodes[(a-BASE)/4]&1);}
static int tree_count(const Image*i,unsigned root)
{
 unsigned stack[256],seen[256],n=0,depth=0;stack[depth++]=root;
 while(depth){unsigned a=stack[--depth];if(!a||!is_node(i,a))continue;unsigned j;for(j=0;j<n;j++)if(seen[j]==a)break;if(j<n)continue;if(n>=256||depth>253)return -1;
 seen[n++]=a;const unsigned char*p=source(i,a,52);if(!p)return -1;
 stack[depth++]=word(p+44);stack[depth++]=word(p+48);}
 return (int)n;
}
static void free_image(Image*i){free(i->data);free(i->nodes);memset(i,0,sizeof*i);}
static int load_image(JgrDisc*d,unsigned city,Image*i)
{
 JgrDiscFile afs,file;unsigned member=city==1?47:city==2?49:city==3?51:city==5?53:0;if(!member||jgr_disc_find(d,"JETRADIO/ENEMY.AFS",&afs)||jgr_disc_afs(d,&afs,member,&file)||file.size>2*1024*1024||file.size<52)return -1;
 i->data=malloc(file.size);i->nodes=calloc((file.size+3)/4,1);i->size=file.size;if(!i->data||!i->nodes||jgr_disc_read(d,&file,0,i->data,file.size))return -1;
 for(unsigned at=0;at+52<file.size;at+=4){const unsigned char*p=i->data+at;unsigned model=word(p+4),child=word(p+44),sib=word(p+48);if(word(p)>65535||(!model&&!child&&!sib))continue;unsigned ptrs[3]={model,child,sib};int valid=1;for(unsigned k=0;k<3;k++)if(ptrs[k]&&(ptrs[k]<BASE||ptrs[k]-BASE>=file.size||(ptrs[k]&3)))valid=0;for(unsigned k=0;k<3;k++){double v=number(p+32+k*4);if(!(v>1e-4&&v<1e3))valid=0;}if(valid)i->nodes[at/4]=1;}
 for(unsigned at=0;at+52<file.size;at+=4)if(i->nodes[at/4]&1){const unsigned char*p=i->data+at;unsigned child=word(p+44),sib=word(p+48);if(child)i->nodes[(child-BASE)/4]|=2;if(sib)i->nodes[(sib-BASE)/4]|=2;}
 /* Candidate membership ignores the child marker bit when traversing. */
 for(unsigned at=0;at+52<file.size;at+=4)if(i->nodes[at/4]==1){int n=tree_count(i,BASE+at);if(n==11){if(i->nr>=128)return -1;
 i->roots[i->nr++]=BASE+at;}else if(n==3){if(i->ns>=128)return -1;
 i->special[i->ns++]=BASE+at;}}
 return i->nr?0:-1;
}
static const unsigned char *bank_span(const Bank*b,unsigned at,unsigned n){return span(at,n,b->size)?b->data+at:NULL;}
static const unsigned char *vertex(const Bank*b,unsigned i){return i>=b->h[3]?NULL:bank_span(b,b->h[10]+i*b->stride,b->stride);}
static const unsigned char *batch(const Bank*b,unsigned i){return i>=b->h[2]?NULL:bank_span(b,b->h[9]+i*16,16);}
static int parse_mesh(const Image*i,unsigned model,unsigned owner,int override,Native*n)
{
 const unsigned char*m=source(i,model,8);if(!m)return -1;
 unsigned vl=word(m),pl=word(m+4);if(!vl&&!pl)return 0;
 if(!vl||!pl)return -1;
 unsigned at=vl;
 for(unsigned guard=0;;guard++){
  const unsigned char*h=source(i,at,4);if(!h||guard>=65536)return -1;
 unsigned type=h[0];if(type==255)break;if(type==0){at+=4;continue;}h=source(i,at,8);if(!h||(type!=34&&type!=41))return -1;
 unsigned bytes=4+half(h+2)*4,first=half(h+4),count=half(h+6),stride=type==34?12:24;
  if(bytes<8||count>(bytes-8)/stride||first+count>65536||count>MAX_POSITION-n->np||!source(i,at,bytes))return -1;
  for(unsigned k=0;k<count;k++){const unsigned char*p=source(i,at+8+k*stride,12);Position*v=n->positions+n->np;v->owner=owner;v->source=UINT32_MAX;for(unsigned j=0;j<3;j++){double x=number(p+j*4);if(!isfinite(x))return -1;
 real(v->xyz+j*4,x==0?0:x);}n->slots[first+k]=(uint16_t)n->np++;}at+=bytes;
 }
 at=pl;unsigned texture=0;
 static const unsigned char extra[12]={0,2,2,3,5,5,2,4,4,1,3,3};
 for(unsigned guard=0;;guard++){
  const unsigned char*h=source(i,at,2);if(!h||guard>=65536)return -1;
 unsigned type=h[0],flags=h[1];if(type==255)break;if(type<=5){at+=2;continue;}
  if(type==8||type==9){h=source(i,at,4);if(!h)return -1;
 texture=half(h+2)&8191;at+=4;continue;}
  if((type>=16&&type<=31)||(type>=56&&type<=58)){h=source(i,at,4);if(!h)return -1;
 unsigned bytes=4+2*half(h+2);if(!source(i,at,bytes))return -1;
 at+=bytes;continue;}
  if(type<64||type>75||(h=source(i,at,6))==NULL)return -1;
 unsigned bytes=4+2*half(h+2),count=half(h+4),end=at+bytes,p=at+6;if(bytes<6||!source(i,at,bytes))return -1;
  unsigned textured=type==65||type==66||type==68||type==69||type==71||type==72,divisor=type==65||type==68||type==71||type==73?255:type==66||type==69||type==72||type==74?1023:1;
  for(unsigned j=0;j<(count&16383);j++){
   h=source(i,p,2);if(!h||p>end||end-p<2||n->ns>=MAX_STRIP)return -1;
 int signed_count=signed_half(h);unsigned nc=signed_count<0?(unsigned)-signed_count:(unsigned)signed_count,step=2+extra[type-64]*2;p+=2;
   if(nc<3||nc>32767||nc>MAX_CORNER-n->nc||nc>(end-p)/step)return -1;
   Strip*s=n->strips+n->ns;s->owner=owner;s->texture=textured?(override>=0?(unsigned)override:texture):65535;s->flags=((flags&8)?1:0)|((flags&16)?2:0)|(textured?0:4)|((flags&1)?8:0);s->first=n->nc;s->count=nc;s->signed_count=signed_count;s->index=n->ns++;
   for(unsigned k=0;k<nc;k++){h=source(i,p,textured?6:2);if(!h)return -1;
 unsigned slot=half(h);if(n->slots[slot]==65535)return -1;
 Corner*c=n->corners+n->nc++;c->position=n->slots[slot];c->source=UINT32_MAX;real(c->uv,textured?(double)signed_half(h+2)/divisor:0);real(c->uv+4,textured?(double)signed_half(h+4)/divisor:0);p+=step;}
   unsigned skip=(nc-2)*(count>>14)*2;if(skip>end-p)return -1;
 p+=skip;
  }
  if(p!=end&&(end-p!=2||half(source(i,p,2))!=0))return -1;
 at=end;
 }
 return 0;
}
static int parse_model(const Image*i,unsigned root,int override,Native*n)
{
 typedef struct{unsigned a;int parent;} Pending;Pending stack[128];unsigned depth=0;stack[depth++]=(Pending){root,-1};memset(n,0,sizeof*n);memset(n->slots,255,sizeof n->slots);
 while(depth){Pending p=stack[--depth];const unsigned char*b=source(i,p.a,52);if(!b||n->nb>=128)return -1;
 for(unsigned k=0;k<n->nb;k++)if(n->seen[k]==p.a)return -1;
 unsigned owner=n->nb++;n->seen[owner]=p.a;w16(n->bones[owner],(unsigned)p.parent);w16(n->bones[owner]+2,word(b));memcpy(n->bones[owner]+4,b+8,36);
  unsigned child=word(b+44),sibling=word(b+48),model=word(b+4);if(depth+!!child+!!sibling>128)return -1;
 if(sibling)stack[depth++]=(Pending){sibling,p.parent};
 if(child)stack[depth++]=(Pending){child,(int)owner};
 if(model&&parse_mesh(i,model,owner,override,n))return -1;
 }return 0;
}
static int order_compare(const void*a,const void*b)
{const Strip*x=*(const Strip*const*)a,*y=*(const Strip*const*)b;if(x->owner!=y->owner)return x->owner<y->owner?-1:1;if(x->texture!=y->texture)return x->texture<y->texture?-1:1;if(x->flags!=y->flags)return x->flags<y->flags?-1:1;return x->index<y->index?-1:x->index>y->index;}
static int match_corner(const Bank*b,Native*n,Corner*c,unsigned source_id)
{
 const unsigned char*v=vertex(b,source_id);Position*p=n->positions+c->position;if(!v||memcmp(c->uv,v,8)||memcmp(p->xyz,v+12,12)||half(v+24)!=p->owner)return -1;
 if(c->source!=UINT32_MAX&&memcmp(vertex(b,c->source),v,28))return -1;
 c->source=source_id;if(p->source==UINT32_MAX)p->source=source_id;return 0;
}
static int match(const Bank*b,unsigned ci,Native*n)
{
 const unsigned char*ch=bank_span(b,b->h[7]+ci*48,48);if(!ch)return -1;
 unsigned first=word(ch),bones=word(ch+4);if(bones!=n->nb||first>b->h[1]||bones>b->h[1]-first)return -1;
 Strip**order=malloc(n->ns*sizeof*order);if(!order)return -1;
 int result=-1;for(unsigned i=0;i<n->ns;i++)order[i]=n->strips+i;qsort(order,n->ns,sizeof*order,order_compare);unsigned cursor=0;
 for(unsigned owner=0;owner<bones;owner++){
  const unsigned char*bone=bank_span(b,b->h[8]+(first+owner)*48,48);if(!bone)goto done;
 int parent=signed_half(bone);if(parent>=0)parent-=(int)first;if(parent!=signed_half(n->bones[owner])||memcmp(bone+2,n->bones[owner]+2,38))goto done;
 unsigned bf=word(bone+40),nb=word(bone+44);if(bf>b->h[2]||nb>b->h[2]-bf)goto done;
  for(unsigned bi=bf;bi<bf+nb;bi++){
   const unsigned char*ba=batch(b,bi);if(!ba||cursor>=n->ns||order[cursor]->owner!=owner)goto done;
 unsigned vo=word(ba),io=word(ba+4),nv=half(ba+8),ni=half(ba+10),tex=half(ba+12),flags=half(ba+14),expected=order[cursor]->flags,source_tex=order[cursor]->texture,source_flags=order[cursor]->flags;
   if(vo>b->h[3]||nv>b->h[3]-vo||io>b->h[4]||ni>b->h[4]-io||ni%3||tex>=b->h[5])goto done;
 if(source_tex!=65535){const unsigned char*t=bank_span(b,b->h[12]+tex*16,16);if(!t)goto done;
 if(half(t+4)&1)expected|=1;}else if(tex)goto done;
 if(flags!=expected)goto done;
   unsigned triangle=0;n->source_vertices+=nv;
   do{
    Strip*s=order[cursor];s->runtime_texture=tex;s->runtime_flags=flags;
    for(unsigned k=0;k<s->count-2;k++){
     unsigned c[3]={s->first+k,s->first+k+1,s->first+k+2};if((k&1)^(s->signed_count<0)){unsigned t=c[0];c[0]=c[1];c[1]=t;}
     if(triangle>=ni/3)goto done;
 const unsigned char*ix=bank_span(b,b->h[11]+(io+triangle*3)*2,6);if(!ix)goto done;
     for(unsigned j=0;j<3;j++){unsigned local=half(ix+2*j);if(local>=nv||match_corner(b,n,n->corners+c[j],vo+local))goto done;}triangle++;
    }cursor++;
   }while(cursor<n->ns&&order[cursor]->owner==owner&&order[cursor]->texture==source_tex&&order[cursor]->flags==source_flags);
   if(triangle*3!=ni)goto done;
  }
 }
 if(cursor!=n->ns)goto done;
 cursor=0;for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<n->ns;i++)if(!!(order[i]->runtime_flags&8)==!pass)n->order[cursor++]=order[i]->index;
 result=0;
done:free(order);return result;
}
static int index_append(Native*n,unsigned index){if(n->ni>=2048||index>65535)return -1;
 n->indices[n->ni++]=(uint16_t)index;return 0;}
static int assemble(const Bank*b,Native*n)
{
 for(unsigned i=0;i<n->np;i++)if(n->positions[i].source!=UINT32_MAX){if(n->npos>=256||n->positions[i].source>65535)return -1;
 n->retained[i]=n->npos;n->positions_out[n->npos][0]=(uint16_t)n->positions[i].source;n->positions_out[n->npos++][1]=(uint16_t)n->positions[i].owner;}
 for(unsigned begin=0;begin<n->ns;){
  unsigned end=begin+1;Strip*first=n->strips+n->order[begin];while(end<n->ns){Strip*s=n->strips+n->order[end];if(s->runtime_texture!=first->runtime_texture||s->runtime_flags!=first->runtime_flags)break;end++;}
  if(n->ng>=128)return -1;
 unsigned fv=n->nv,fi=n->ni,triangles=0;
  for(unsigned i=begin;i<end;i++){
   Strip*s=n->strips+n->order[i];unsigned local[2048];if(s->count>2048)return -1;
   for(unsigned j=0;j<s->count;j++){
    Corner*c=n->corners+s->first+j;if(c->source==UINT32_MAX||n->positions[c->position].source==UINT32_MAX)return -1;
 unsigned pi=n->retained[c->position],source_id=c->source,found;
    for(found=fv;found<n->nv;found++)if(n->emissions[found][0]==pi&&!memcmp(vertex(b,n->emissions[found][1]),vertex(b,source_id),12))break;
    if(found==n->nv){if(n->nv>=512||source_id>65535)return -1;
 n->emissions[n->nv][0]=(uint16_t)pi;n->emissions[n->nv++][1]=(uint16_t)source_id;}local[j]=found-fv;
   }
   if(n->ni==fi){if(s->signed_count<0&&index_append(n,local[0]))return -1;}
   else{unsigned parity=(n->ni-fi)&1,last=n->indices[n->ni-1];if(index_append(n,last)||index_append(n,local[0]))return -1;
 if(!!parity!=(s->signed_count<0)&&index_append(n,local[0]))return -1;}
   for(unsigned j=0;j<s->count;j++)if(index_append(n,local[j]))return -1;
 triangles+=s->count-2;
  }
  unsigned actual=0;for(unsigned j=fi;j+2<n->ni;j++)if(n->indices[j]!=n->indices[j+1]&&n->indices[j]!=n->indices[j+2]&&n->indices[j+1]!=n->indices[j+2])actual++;if(actual!=triangles)return -1;
  unsigned values[8]={fv,n->nv-fv,fi,n->ni-fi,first->runtime_texture,first->runtime_flags,triangles,1};for(unsigned k=0;k<8;k++){if(values[k]>65535)return -1;
 n->groups[n->ng][k]=(uint16_t)values[k];}n->ng++;begin=end;
 }
 return n->npos&&n->ng?0:-1;
}
static int emit_pairs(FILE*out,const uint16_t*values,unsigned count,unsigned*offset)
{if(pad(out))return -1;
 long at=ftell(out);if(at<0||(unsigned long)at>UINT32_MAX)return -1;
 *offset=(unsigned)at;unsigned char b[2];for(unsigned i=0;i<count;i++){w16(b,values[i]);if(write(out,b,2))return -1;}return 0;}
int jgr_people_native(JgrDisc*d,unsigned city,FILE*input,FILE*out)
{
 Bank bank={0};Image images[2]={{0}};Native*n=NULL;int result=-1;unsigned char header[68];if(!d||!input||!out||(city!=1&&city!=2&&city!=3&&city!=5)||fseek(input,0,SEEK_END))return -1;
 long size=ftell(input);if(size<68||(unsigned long)size>8*1024*1024||fseek(input,0,SEEK_SET)||fread(header,1,68,input)!=68||(memcmp(header,"JGC2",4)&&memcmp(header,"JGC3",4)))return -1;
 bank.size=(unsigned)size;bank.stride=header[3]=='3'?40:28;for(unsigned i=0;i<16;i++)bank.h[i]=word(header+4+4*i);if(bank.h[0]>128||bank.h[3]>65535||bank.h[1]>8192||bank.h[2]>8192||bank.h[4]>4000000||bank.h[5]>8192||bank.h[6]>65536||bank.h[7]<68)return -1;
 unsigned strides[7]={48,48,16,bank.stride,2,16,8};for(unsigned i=0;i<9;i++)if(!span(bank.h[7+i],i<7?bank.h[i]*strides[i]:0,bank.size)||(i&&bank.h[7+i]<bank.h[6+i]))return -1;
 bank.data=malloc(bank.size);n=malloc(sizeof*n);if(!bank.data||!n||fseek(input,0,SEEK_SET)||fread(bank.data,1,bank.size,input)!=bank.size||load_image(d,city,images))goto done;
 if(city!=1&&load_image(d,1,images+1))goto done;
 unsigned geometry=2166136261u;for(unsigned i=0;i<bank.h[2];i++){const unsigned char*b=batch(&bank,i);if(!b)goto done;
 unsigned vo=word(b),io=word(b+4),nv=half(b+8),ni=half(b+10);if(vo>bank.h[3]||nv>bank.h[3]-vo||io>bank.h[4]||ni>bank.h[4]-io)goto done;
 const unsigned char*parts[3]={b,bank.data+bank.h[10]+vo*bank.stride,bank.data+bank.h[11]+io*2};unsigned lengths[3]={16,nv*bank.stride,ni*2};for(unsigned k=0;k<3;k++)for(unsigned j=0;j<lengths[k];j++){geometry^=parts[k][j];geometry*=16777619u;}}
 memset(header,0,32);if(write(out,header,32))goto done;
 for(unsigned i=0;i<bank.h[0];i++)if(write(out,header,32))goto done;
 for(unsigned ci=0;ci<bank.h[0];ci++){
  const unsigned char*ch=bank_span(&bank,bank.h[7]+ci*48,48);if(!ch)goto done;
 char name[17];memcpy(name,ch+32,16);name[16]=0;unsigned source_city,index;int special=strstr(name,"special")!=NULL;if(sscanf(name,special?"P%u_special_%u":"P%u_%u",&source_city,&index)!=2||(source_city!=city&&source_city!=1))goto done;
 Image*image=images+(source_city!=city);unsigned root;
  if(special){if(index<43||index-43>=image->ns)goto done;
 root=image->special[index-43];}else{if(index>=image->nr)goto done;
 root=image->roots[index];}
  if(parse_model(image,root,special?-1:(int)index,n)||match(&bank,ci,n)||assemble(&bank,n))goto done;
  unsigned offsets[4];if(emit_pairs(out,&n->positions_out[0][0],n->npos*2,offsets)||emit_pairs(out,&n->emissions[0][0],n->nv*2,offsets+1)||emit_pairs(out,&n->groups[0][0],n->ng*8,offsets+2)||emit_pairs(out,n->indices,n->ni,offsets+3))goto done;
 long end=ftell(out);if(end<0||fseek(out,32+ci*32,SEEK_SET))goto done;
 unsigned char row[32]={0};unsigned counts[6]={n->npos,n->nv,n->ng,n->ni,n->source_vertices,n->nb};for(unsigned i=0;i<6;i++)w16(row+i*2,counts[i]);for(unsigned i=0;i<4;i++)w32(row+12+i*4,offsets[i]);if(write(out,row,32)||fseek(out,end,SEEK_SET))goto done;
 }
 if(pad(out))goto done;
 long end=ftell(out);if(end<0||end>56*1024||fseek(out,0,SEEK_SET))goto done;
 memset(header,0,32);memcpy(header,"JPN1",4);w32(header+4,1);w32(header+8,(unsigned)end);w32(header+12,geometry);w32(header+16,bank.h[0]);w32(header+20,32);if(write(out,header,32)||fseek(out,end,SEEK_SET))goto done;
 result=0;
 done:free(bank.data);free(n);free_image(images);free_image(images+1);return result;
}
