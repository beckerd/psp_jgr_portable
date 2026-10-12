#include "portable_scene_convert.h"
#include "portable_crc32.h"
#include <stdlib.h>
#include <string.h>
#define EXE_BASE 0x8c010000u
#define MAX_DEPENDENCIES 4096u
#define MAX_EVENT_SIZE (4u*1024*1024)
#define SCENE_COPY_BYTES (128u*1024u)
static unsigned word(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static int get(JgrDisc *d,const JgrDiscFile *f,unsigned at,unsigned *v)
{unsigned char b[4];if(jgr_disc_read(d,f,at,b,4))return -1;*v=word(b);return 0;}
static int exget(JgrDisc *d,const JgrDiscFile *f,unsigned at,unsigned *v)
{return at<EXE_BASE?-1:get(d,f,at-EXE_BASE,v);}
static int put(FILE *f,unsigned v){unsigned char b[4];for(unsigned i=0;i<4;i++)b[i]=(unsigned char)(v>>(8*i));return fwrite(b,1,4,f)==4?0:-1;}
static int copy(JgrDisc *d,const JgrDiscFile *f,FILE *out)
{unsigned char b[4096];for(unsigned at=0;at<f->size;){unsigned n=f->size-at;if(n>sizeof b)n=sizeof b;if(jgr_disc_read(d,f,at,b,n)||fwrite(b,1,n,out)!=n)return -1;at+=n;}return 0;}
static int archive(JgrDisc *d,const char *folder,JgrDiscFile *f,unsigned *count)
{char path[64];unsigned char head[8];if(snprintf(path,sizeof path,"JETRADIO/%s.AFS",folder)<0 || jgr_disc_find(d,path,f)||jgr_disc_read(d,f,0,head,8)||memcmp(head,"AFS",3)||(head[3]&&head[3]!=' '))return -1;*count=word(head+4);return *count>MAX_DEPENDENCIES?-1:0;}
static int scenes(JgrDisc *d,const JgrDiscFile *exe,FILE *out)
{
 JgrDiscFile event,files[112];unsigned n,request_table,record_table,ids[112],flags[112],offsets[112],cursor=8+112*20;
 if(archive(d,"EVENT",&event,&n)||exget(d,exe,0x8c0eacd0,&request_table)||exget(d,exe,0x8c05be64,&record_table))return -1;
 for(unsigned i=0;i<112;i++){
  unsigned request,type,end,slot,record,base,mode;
  if(exget(d,exe,request_table+i*4,&request)||exget(d,exe,request,&type)||type!=0x10||exget(d,exe,request+8,&end)||end!=0x22||
     exget(d,exe,request+4,&slot)||slot>4095||exget(d,exe,record_table+slot*4,&record)||exget(d,exe,record,&type)||type!=0x43||
     exget(d,exe,record+8,&base)||base!=0x8c900000||exget(d,exe,record+20,&mode)||mode!=2||exget(d,exe,record+32,&end)||end!=0x52||
     exget(d,exe,record+4,&ids[i])||ids[i]>=n||jgr_disc_afs(d,&event,ids[i],&files[i])||files[i].size>MAX_EVENT_SIZE||
     exget(d,exe,0x8c1d0fd4+i*0x44,&flags[i]))return -1;
  unsigned j;for(j=0;j<i;j++)if(ids[j]==ids[i])break;
  if(j<i)offsets[i]=offsets[j];else{offsets[i]=cursor;if(files[i].size>0x7fffffffu-cursor)return -1;cursor+=files[i].size;}
 }
 if(fwrite("SCN1",1,4,out)!=4||put(out,112))return -1;
 for(unsigned i=0;i<112;i++)if(put(out,i)||put(out,flags[i])||put(out,0x8c900000)||put(out,offsets[i])||put(out,files[i].size))return -1;
 for(unsigned i=0;i<112;i++){unsigned j;for(j=0;j<i;j++)if(ids[j]==ids[i])break;if(j==i&&copy(d,&files[i],out))return -1;}
 return 0;
}
typedef struct {JgrDiscFile file;unsigned folder,index,offset,owner,hashed;uint32_t fingerprint;} Dependency;
static int fingerprint(JgrDisc *d,Dependency *r,unsigned char *buffer)
{
 if(r->hashed)return 0;
 uint32_t crc=0xffffffffu;
 for(unsigned at=0;at<r->file.size;){unsigned n=r->file.size-at;if(n>SCENE_COPY_BYTES)n=SCENE_COPY_BYTES;
  if(jgr_disc_read(d,&r->file,at,buffer,n))return -1;
  crc=jgr_crc32_add(crc,buffer,n);at+=n;
 }
 r->fingerprint=crc^0xffffffffu;r->hashed=1;return 0;
}
/* Fingerprints only select candidates. Compare their complete contents before
 * sharing an extent, including when two different payloads have the same CRC. */
static int identical(JgrDisc *d,const JgrDiscFile *a,const JgrDiscFile *b,unsigned char *buffer)
{
 if(a->size!=b->size)return 0;
 if(a->offset==b->offset)return 1;
 for(unsigned at=0;at<a->size;){unsigned n=a->size-at;if(n>SCENE_COPY_BYTES)n=SCENE_COPY_BYTES;
  if(jgr_disc_read(d,a,at,buffer,n)||jgr_disc_read(d,b,at,buffer+SCENE_COPY_BYTES,n))return -1;
  if(memcmp(buffer,buffer+SCENE_COPY_BYTES,n))return 0;
  at+=n;
 }
 return 1;
}
static int checked_write(FILE *out,const void *bytes,size_t n,uint32_t *crc)
{
 if(fwrite(bytes,1,n,out)!=n)return -1;
 if(crc)*crc=jgr_crc32_add(*crc,bytes,n);
 return 0;
}
static void encode_word(unsigned char *p,unsigned v)
{for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static int dependencies(JgrDisc *d,FILE *out,uint32_t *output_crc)
{
 static const char *const folders[]={"EVENT","ENEMY","E_SND_SE","SND_SE","E_SND_MUSEN","SND_MUSEN"};
 JgrDiscFile afs[6];unsigned counts[6],total=0;int result=-1;
 for(unsigned i=0;i<6;i++){if(archive(d,folders[i],afs+i,counts+i)||counts[i]>MAX_DEPENDENCIES-total)return -1;total+=counts[i];}
 Dependency *rows=calloc(total?total:1,sizeof *rows);
 unsigned char *buffer=malloc(SCENE_COPY_BYTES*2u);
 if(!rows||!buffer){free(rows);free(buffer);return -1;}
 unsigned cursor=8+total*32,slot=0;
 for(unsigned f=0;f<6;f++)for(unsigned i=0;i<counts[f];i++,slot++){
  Dependency *r=rows+slot;r->folder=f;r->index=i;
  if(jgr_disc_afs(d,afs+f,i,&r->file))goto done;
  /* A unique size cannot be a duplicate. Hash lazily only when another
   * same-sized owner exists, rather than reading every input twice. */
  unsigned j;for(j=0;j<slot;j++)if(rows[j].owner==j && rows[j].file.size==r->file.size){
   if(rows[j].file.offset==r->file.offset)break;
   if(fingerprint(d,r,buffer)||fingerprint(d,rows+j,buffer))goto done;
   if(r->fingerprint==rows[j].fingerprint){int same=identical(d,&r->file,&rows[j].file,buffer);
    if(same<0)goto done;
    if(same)break;
   }
  }
  r->owner=j;if(j<slot)r->offset=rows[j].offset;else{r->offset=cursor;if(r->file.size>0x7fffffffu-cursor)goto done;cursor+=r->file.size;}
 }
 uint32_t crc=0xffffffffu,*sum=output_crc?&crc:NULL;
 unsigned char header[8];memcpy(header,"CAS1",4);encode_word(header+4,total);
 if(checked_write(out,header,sizeof header,sum))goto done;
 for(unsigned i=0;i<total;i++){
  Dependency *r=rows+i;unsigned char record[32]={0};strcpy((char *)record,folders[r->folder]);
  encode_word(record+16,r->index);encode_word(record+20,r->offset);encode_word(record+24,r->file.size);
  if(checked_write(out,record,sizeof record,sum))goto done;
 }
 for(unsigned i=0;i<total;i++)if(rows[i].owner==i)for(unsigned at=0;at<rows[i].file.size;){
  unsigned n=rows[i].file.size-at;if(n>SCENE_COPY_BYTES)n=SCENE_COPY_BYTES;
  if(jgr_disc_read(d,&rows[i].file,at,buffer,n)||checked_write(out,buffer,n,sum))goto done;
  at+=n;
 }
 if(output_crc)*output_crc=crc^0xffffffffu;
 result=0;
done:free(buffer);free(rows);return result;
}
typedef struct {char name[11];char *text;} Subtitle;
static int alpha(unsigned c){return(c>='A'&&c<='Z')||(c>='a'&&c<='z');}
static int compare(const void *a,const void *b){return strcmp(((const Subtitle *)a)->name,((const Subtitle *)b)->name);}
static char *subtitle(const unsigned char *bytes,unsigned size)
{
 for(unsigned at=0;at<size;){
  unsigned first=at,letters=0,maximum=0,dollar=0;
  while(at<size&&bytes[at]>=32&&bytes[at]<=126){unsigned c=bytes[at++];if(c=='$')dollar=1;if(alpha(c)){if(++letters>maximum)maximum=letters;}else letters=0;}
  unsigned n=at-first;at++;if(n<8||!dollar||maximum<3)continue;
  char *text=malloc((size_t)n*2+1);if(!text)return NULL;unsigned out=0;
  for(unsigned i=first;i<first+n;i++){
   unsigned c=bytes[i];
   if(c=='$'&&i+1<first+n){unsigned next=bytes[i+1];
    if(next=='c'&&i+3<first+n&&bytes[i+2]=='['){unsigned p=i+3;while(p<first+n&&bytes[p]>='0'&&bytes[p]<='9')p++;if(p>i+3&&p<first+n&&bytes[p]==']'){i=p;continue;}}
    if(next=='w'){i++;continue;}
    if(next=='f'||next=='n'||next=='m'){i++;text[out++]='\n';if(next=='f')text[out++]='\n';continue;}
   }
   if(c==' '&&out&&text[out-1]==' ')continue;
   text[out++]=(char)c;
  }
  while(out&&(text[out-1]==' '||text[out-1]=='\n'))out--;
  unsigned begin=0;while(begin<out&&(text[begin]==' '||text[begin]=='\n'))begin++;
  memmove(text,text+begin,out-begin);text[out-begin]=0;return text;
 }
 return NULL;
}
static int events(JgrDisc *d,FILE *out)
{
 JgrDiscFile afs;unsigned count;int result=-1;
 if(archive(d,"EVENT",&afs,&count)||count>1024)return -1;
 Subtitle *rows=calloc(count,sizeof *rows);if(!rows)return -1;unsigned used=0;
 for(unsigned i=0;i<count;i++){
  JgrDiscFile file;char name[33];if(jgr_disc_afs_named(d,&afs,i,&file,name)||file.size>MAX_EVENT_SIZE)goto done;
  char *number=strstr(name,"event_");if(!number||strstr(name,"txr"))continue;number+=6;
  for(unsigned k=0;k<4;k++)if(number[k]<'0'||number[k]>'9')goto done;
  unsigned char *data=malloc(file.size?file.size:1);if(!data)goto done;if(jgr_disc_read(d,&file,0,data,file.size)){free(data);goto done;}
  char *text=subtitle(data,file.size);free(data);if(!text)continue;if(!*text){free(text);continue;}
  char key[11]="EVENT_";memcpy(key+6,number,4);unsigned j;for(j=0;j<used;j++)if(!strcmp(key,rows[j].name))break;
  if(j==used)used++;else free(rows[j].text);strcpy(rows[j].name,key);rows[j].text=text;
 }
 qsort(rows,used,sizeof *rows,compare);
 for(unsigned i=0;i<used;i++){
  if(fwrite(rows[i].name,1,10,out)!=10||fputc('\t',out)==EOF)goto done;
  for(const char *p=rows[i].text;*p;p++){if(*p=='\n'){if(fwrite("\\n",1,2,out)!=2)goto done;}else if(fputc((unsigned char)*p,out)==EOF)goto done;}
  if(fputc('\n',out)==EOF)goto done;
 }
 result=0;
done:for(unsigned i=0;i<used;i++)free(rows[i].text);free(rows);return result;
}
int jgr_scene_convert(JgrDisc *d,const JgrDiscFile *f,unsigned op,FILE *out)
{
 if(!d||!f||!out)return -1;
 if(op==80)return scenes(d,f,out);
 if(op==81)return dependencies(d,out,NULL);
 if(op==82)return events(d,out);
 return -1;
}
int jgr_scene_dependencies_crc(JgrDisc *d,FILE *out,uint32_t *crc)
{return !d||!out||!crc?-1:dependencies(d,out,crc);}
