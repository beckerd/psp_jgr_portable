/* Streaming serializers for original mission-set records. */
#include "portable_mission_convert.h"
#include <stdint.h>
#include <string.h>

#define SET_BASE 0x8cde6000u
#define MAX_BANK (1024u*1024u)
typedef struct { JgrDisc *disc;const JgrDiscFile *file;FILE *out;uint32_t base;int error; } Reader;
static uint32_t le32(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int span(Reader *r,uint32_t address,uint32_t bytes)
{return address>=r->base && address-r->base<=r->file->size && bytes<=r->file->size-(address-r->base);}
static int ok(Reader *r,uint32_t address){return span(r,address,4);}
static void read_at(Reader *r,uint32_t address,void *out,uint32_t bytes)
{
    if(!span(r,address,bytes) || jgr_disc_read(r->disc,r->file,address-r->base,out,bytes)<0) {
        memset(out,0,bytes);r->error=1;
    }
}
static uint32_t word(Reader *r,uint32_t address)
{unsigned char p[4];read_at(r,address,p,4);return le32(p);}
static unsigned byte(Reader *r,uint32_t address)
{unsigned char p;read_at(r,address,&p,1);return p;}
static int half(Reader *r,uint32_t address)
{unsigned char p[2];read_at(r,address,p,2);return (int16_t)((unsigned)p[0]|((unsigned)p[1]<<8));}
/* Some original sentinel records contain NaN bit patterns. Classify source
 * words before loading them into the PSP FPU: an ordered float comparison
 * can raise Invalid Operation on those records. Preserve IEEE comparison
 * semantics, including both zero signs and positive infinity. */
static int source_nonnegative(Reader *r,uint32_t address)
{uint32_t bits=word(r,address),magnitude=bits&0x7fffffffu;
 return !magnitude || (!(bits&0x80000000u) && magnitude<=0x7f800000u);}
static int source_finite(Reader *r,uint32_t address)
{return (word(r,address)&0x7fffffffu)<0x7f800000u;}
static void bytes(Reader *r,const void *data,size_t size)
{if(fwrite(data,1,size,r->out)!=size)r->error=1;}
static void put(Reader *r,uint32_t value)
{unsigned char p[4]={(unsigned char)value,(unsigned char)(value>>8),(unsigned char)(value>>16),(unsigned char)(value>>24)};bytes(r,p,4);}
static void angle(Reader *r,int32_t bam)
{
    float value=(float)((double)bam*2.0*3.141592653589793/65536.0);uint32_t bits;
    memcpy(&bits,&value,4);put(r,bits);
}
static void copy(Reader *r,uint32_t address,uint32_t size)
{
    unsigned char block[1024];
    if(!size)return;
    if(!span(r,address,size)){r->error=1;return;}
    while(size && !r->error) {
        uint32_t n=size<sizeof block ? size:sizeof block;
        read_at(r,address,block,n);bytes(r,block,n);address+=n;size-=n;
    }
}
static long placeholder(Reader *r){long at=ftell(r->out);if(at<0)r->error=1;put(r,0);return at;}
static void patch(Reader *r,long at,uint32_t value)
{long end=ftell(r->out);if(end<0 || fseek(r->out,at,SEEK_SET)){r->error=1;return;}put(r,value);if(fseek(r->out,end,SEEK_SET))r->error=1;}
static void header(Reader *r,const char *magic,uint32_t count){bytes(r,magic,4);put(r,count);}
static unsigned chain(Reader *r,uint32_t sets[8])
{
    r->base=SET_BASE;uint32_t at=SET_BASE;unsigned count=0;
    if(word(r,at)==0x62534d4au)return 0; /* JMSb: empty legacy bank. */
    while(at && !r->error) {
        if(count==8 || (at&3) || !span(r,at,0x54) || word(r,at)!=0x63534d4au){r->error=1;break;}
        for(unsigned i=0;i<count;i++)if(sets[i]==at)r->error=1;
        if(r->error)break;
        sets[count++]=at;at=word(r,at+0x50);
    }
    return count;
}
static void infer_base(Reader *r)
{
    /* Same aligned-pointer score and high-address tie break as the host
       serializers, using one streamed pass instead of 128 image scans. */
    int difference[129]={0},score=0,best=-1;unsigned char block[4096];
    for(uint32_t at=0;at+4<=r->file->size;) {
        uint32_t size=r->file->size-at;if(size>sizeof block)size=sizeof block;size&=~3u;
        if(jgr_disc_read(r->disc,r->file,at,block,size)<0){r->error=1;return;}
        for(uint32_t j=0;j<size;j+=4) {
            uint32_t v=le32(block+j);if(v&3)continue;
            int64_t low=(int64_t)v-r->file->size+1-0x8cd80000ll;
            int64_t high=(int64_t)v-0x8cd80000ll;
            if(high<0 || low>127*4096)continue;
            int first=low<=0 ? 0:(int)((low+4095)/4096);
            int last=high>=127*4096 ? 127:(int)(high/4096);
            if(first<=last){difference[first]++;difference[last+1]--;}
        }
        at+=size;
    }
    for(unsigned i=0;i<128;i++){score+=difference[i];if(score>=best){best=score;r->base=0x8cd80000u+i*4096;}}
}
static void cans(Reader *r)
{
    uint32_t sets[8];unsigned count=chain(r,sets),rows=0;header(r,"CAN3",0);put(r,count);
    for(unsigned s=0;s<count && !r->error;s++)for(unsigned field=0;field<2;field++) {
        uint32_t table=word(r,sets[s]+0x38+field*4);if(!table)continue;
        for(unsigned area=0;area<5 && !r->error;area++) {
            uint32_t p=word(r,table+area*4);if(!p)continue;
            for(unsigned n=0;!r->error;n++,p+=4) {
                if((p&3) || !ok(r,p)){r->error=1;break;}
                uint32_t record=word(r,p);if(!record)break;
                if((record&3) || !span(r,record,field ? 20:16) || n>=4096){r->error=1;break;}
                uint32_t kind=word(r,record+12);if(kind>1){r->error=1;break;}
                put(r,s);put(r,area);put(r,record);copy(r,record,12);put(r,kind+field*2);rows++;
            }
        }
    }
    patch(r,4,rows);
}
static void objects(Reader *r,unsigned sectors)
{
    uint32_t sets[8];unsigned count=chain(r,sets);header(r,"SOB1",count);
    if(!count)return;
    if(!sectors || sectors>128){r->error=1;return;}
    for(unsigned s=0;s<count && !r->error;s++) {
        long mark=placeholder(r);unsigned rows=0;uint32_t table=word(r,sets[s]+0x34);
        if(table)for(unsigned zone=0;zone<sectors && !r->error;zone++) {
            uint32_t p=word(r,table+zone*4);if(!p)continue;
            for(unsigned n=0;!r->error;n++,p+=4) {
                uint32_t record=word(r,p);if(!record)break;
                if(n>=512 || !span(r,record,0x28) || word(r,record)>0x40 ||
                   !source_finite(r,record+4) || !source_finite(r,record+8) || !source_finite(r,record+12)) {r->error=1;break;}
                put(r,zone);put(r,word(r,record));copy(r,record+4,12);angle(r,(int32_t)word(r,record+20));rows++;
            }
        }
        patch(r,mark,rows);
    }
}
static void rivals(Reader *r)
{
    header(r,"RIV1",0);uint32_t p=word(r,r->base+0x18);unsigned rows=0;
    while(ok(r,p) && word(r,p) && rows<64 && !r->error) {
        uint32_t record=word(r,p);p+=4;if(!ok(r,record))break;
        uint32_t type=word(r,record),count=word(r,record+12),entry=word(r,record+16);
        put(r,type);long mark=placeholder(r);unsigned n=0;
        if(count<=64 && ok(r,entry))for(;n<count && ok(r,entry+n*28+24);n++) {
            uint32_t e=entry+n*28;copy(r,e,12);angle(r,(int32_t)word(r,e+16));put(r,word(r,e+24));
        }
        patch(r,mark,n);rows++;
    }
    patch(r,4,rows);
}
static void waves(Reader *r)
{
    header(r,"WAV1",0);uint32_t p=word(r,r->base+0x28);unsigned lists=0;
    while(ok(r,p) && word(r,p) && lists<16 && !r->error) {
        uint32_t q=word(r,p);p+=4;unsigned rows=0;long mark=placeholder(r);
        while(ok(r,q) && word(r,q)!=0xffffffffu && rows<16 && !r->error) {
            uint32_t record=word(r,q);q+=4;if(!ok(r,record+0x54))continue;
            for(unsigned i=0;i<4;i++){copy(r,record+i*12,12);angle(r,(int32_t)word(r,record+0x3c+i*4));}
            put(r,(uint16_t)half(r,record+0x50));rows++;
        }
        patch(r,mark,rows);lists++;
    }
    patch(r,4,lists);
}
static void peds(Reader *r)
{
    JgrDiscFile exe;unsigned char speed[25*8];
    if(jgr_disc_find(r->disc,"1ST_READ.BIN",&exe)<0 || jgr_disc_read(r->disc,&exe,0x1a98b0,speed,sizeof speed)<0){r->error=1;return;}
    header(r,"PED3",0);uint32_t p=word(r,r->base+0x30);unsigned lists=0,rows=0;
    while(ok(r,p) && word(r,p) && lists<64 && !r->error) {
        uint32_t q=word(r,p);p+=4;
        while(ok(r,q) && byte(r,q)!=255 && span(r,q,20) && !r->error) {
            unsigned type=byte(r,q),route=byte(r,q+1),flags=byte(r,q+2),n=byte(r,q+3);uint32_t points=word(r,q+4);
            if(type<0x4c && route<25 && n && n<=32 && ok(r,points) && span(r,points,n*12)) {
                put(r,type);put(r,n);put(r,route);put(r,flags);angle(r,half(r,q+12));bytes(r,speed+route*8,4);put(r,lists);copy(r,points,n*12);rows++;
            }
            q+=20;
        }
        lists++;
    }
    patch(r,4,rows);
}
static void groups(Reader *r)
{
    header(r,"GRP1",0);uint32_t p=word(r,r->base+0x18);unsigned records=0;
    while(ok(r,p) && word(r,p) && records<64 && !r->error) {
        uint32_t s=word(r,p);p+=4;long submark=placeholder(r);unsigned subs=0;
        while(ok(r,s) && ok(r,s+19) && word(r,s)!=0xffffffffu && subs<8 && !r->error) {
            uint32_t type=word(r,s),quad=word(r,s+8),count=word(r,s+12),entry=word(r,s+16);
            put(r,type);put(r,word(r,s+4));if(ok(r,quad))copy(r,quad,32);else for(unsigned i=0;i<8;i++)put(r,0);
            long mark=placeholder(r);unsigned n=0;
            unsigned stride=type==31 ? 12:(type==17 || type==21) ? 16:(type==11 || type==12 || type==14) ? 20:
                            (type==8 || type==10 || type==24 || type==13 || type==15 || type==32) ? 24:28;
            if(ok(r,entry) && count<=(type==31 ? 256u:32u))for(;n<count && ok(r,entry+stride*n+stride-1);n++) {
                uint32_t e=entry+stride*n;copy(r,e,12);
                if(stride==28)for(unsigned i=0;i<3;i++)angle(r,(int32_t)word(r,e+12+i*4));
                else {put(r,stride>=20 ? word(r,e+12):0);put(r,stride==24 ? word(r,e+16):0);put(r,0);}
                put(r,stride==12 ? 0:word(r,e+stride-4));
            }
            patch(r,mark,n);s+=20;subs++;
        }
        patch(r,submark,subs);records++;
    }
    patch(r,4,records);
}
static unsigned graph_pass(Reader *r,int links_only)
{
    uint32_t table=word(r,r->base+0x10);unsigned total=0;
    for(unsigned area=0;area<6 && !r->error;area++) {
        uint32_t p=ok(r,table+area*4) ? word(r,table+area*4):0;unsigned graphs=0;
        long mark=links_only ? 0:placeholder(r);
        while(ok(r,p) && word(r,p) && graphs<16 && !r->error) {
            uint32_t g=word(r,p);p+=4;if(!ok(r,g))break;
            unsigned count=word(r,g);uint32_t nodes=word(r,g+4),xf=word(r,g+8);if(count>256 || !ok(r,nodes))break;
            unsigned n=0;while(n<count && ok(r,nodes+n*44+43))n++;
            if(!links_only) {
                put(r,n);put(r,ok(r,xf) && xf);
                if(ok(r,xf) && xf)copy(r,xf,48);else for(unsigned k=0;k<12;k++)put(r,k==0 || k==5 || k==10 ? 0x3f800000u:0);
            }
            for(unsigned k=0;k<n && !r->error;k++) {
                uint32_t node=nodes+k*44,lp=word(r,node+40);int countlinks=(int32_t)word(r,node+36);
                if(countlinks<0 || countlinks>16 || !ok(r,lp) || !ok(r,lp+(unsigned)countlinks*16))countlinks=0;
                if(links_only)copy(r,lp,(unsigned)countlinks*16);
                else {copy(r,node,36);put(r,(unsigned)countlinks);put(r,total);}
                total+=(unsigned)countlinks;
            }
            graphs++;
        }
        if(!links_only)patch(r,mark,graphs);
    }
    return total;
}
static void graph(Reader *r)
{header(r,"GRA1",6);unsigned links=graph_pass(r,0);put(r,links);if(graph_pass(r,1)!=links)r->error=1;}
static void ctrl(Reader *r)
{
    uint32_t sets[8];unsigned sets_count=chain(r,sets);header(r,"CTL2",sets_count);
    for(unsigned s=0;s<sets_count && !r->error;s++) {
        uint32_t streams[1024];unsigned stream_count=0,tables=0;long tablemark=placeholder(r);uint32_t p=word(r,sets[s]+0x44);
        while(ok(r,p) && ok(r,word(r,p)) && tables<16 && !r->error) {
            uint32_t q=word(r,p);p+=4;unsigned segments=0;long segmark=placeholder(r);
            while(ok(r,q) && ok(r,word(r,q)) && word(r,q+8)<8 && segments<64 && !r->error) {
                uint32_t stream=word(r,q);unsigned index=0;while(index<stream_count && streams[index]!=stream)index++;
                if(index==stream_count){if(stream_count==1024){r->error=1;break;}streams[stream_count++]=stream;}
                put(r,index);put(r,word(r,q+4));put(r,word(r,q+8));q+=12;segments++;
            }
            patch(r,segmark,segments);tables++;
        }
        patch(r,tablemark,tables);put(r,stream_count);
        for(unsigned i=0;i<stream_count && !r->error;i++) {
            uint32_t start=streams[i],q=start+32;unsigned records=0;
            while(ok(r,q)) {unsigned end=byte(r,q)&128;records++;q+=4;if(end || records*4>65536)break;}
            copy(r,start,32);put(r,records);copy(r,start+32,records*4);
        }
        p=word(r,sets[s]+0x48);unsigned trials=0;long trialmark=placeholder(r);
        while(ok(r,p) && ok(r,word(r,p)) && trials<64 && !r->error) {
            uint32_t e=word(r,p);p+=4;uint32_t type=word(r,e),qa=word(r,e+12),na=word(r,e+16),qb=word(r,e+20),nb=word(r,e+24);
            if(type>9)break;
            if(na>r->file->size/144 || nb>r->file->size/144 || (na && !span(r,qa,na*144)) || (nb && !span(r,qb,nb*144))){r->error=1;break;}
            put(r,type);put(r,word(r,e+4));put(r,word(r,e+8));put(r,na);put(r,nb);
            if(na)copy(r,qa,na*144);
            if(nb)copy(r,qb,nb*144);
            trials++;if(type==9)break;
        }
        patch(r,trialmark,trials);
    }
}
static void police_record(Reader *r,uint32_t record)
{
    uint32_t points[64],areas[128],segments[64];unsigned np=0,na=0,ns=0,nmem=0,nadj=0;
    uint32_t p=word(r,record),a=word(r,record+4),s=word(r,record+8);
    while(ok(r,p+60) && source_nonnegative(r,p+60) && np<64 && !r->error) {
        int count=half(r,p+66);uint32_t adj=word(r,p+68);
        if(count<0 || count>16 || !ok(r,adj) || !ok(r,adj+(unsigned)count*8))break;
        points[np++]=p;nadj+=(unsigned)count;p+=72;
    }
    while(np && ok(r,a+66) && half(r,a+64)>=0 && na<128 && !r->error) {
        if((unsigned)half(r,a+64)>=np)break;
        areas[na++]=a;a+=68;
    }
    while(np && ok(r,s+79) && byte(r,s+24)!=255 && ns<64 && !r->error) {
        unsigned kind=byte(r,s+24),count=byte(r,s+25),start=byte(r,s+26);uint32_t members=word(r,s+76);
        if(kind>1 || count>16 || start>=np || !ok(r,members) || !ok(r,members+36*count))break;
        segments[ns++]=s;nmem+=count;s+=80;
    }
    put(r,np);put(r,na);put(r,ns);put(r,nmem);put(r,nadj);
    unsigned first=0;
    for(unsigned i=0;i<np;i++){p=points[i];unsigned count=(unsigned)half(r,p+66);copy(r,p,64);put(r,count);put(r,first);first+=count;}
    for(unsigned i=0;i<na;i++){a=areas[i];copy(r,a,64);put(r,(uint32_t)half(r,a+64));put(r,(uint32_t)half(r,a+66));}
    first=0;
    for(unsigned i=0;i<ns;i++) {
        s=segments[i];unsigned count=byte(r,s+25);copy(r,s,12);put(r,byte(r,s+24));put(r,byte(r,s+26));put(r,count);put(r,first);copy(r,s+28,48);first+=count;
    }
    for(unsigned i=0;i<ns;i++) {
        s=segments[i];unsigned count=byte(r,s+25);uint32_t member=word(r,s+76);
        for(unsigned j=0;j<count;j++,member+=36){copy(r,member,12);for(unsigned k=0;k<3;k++)angle(r,(int32_t)word(r,member+12+k*4));copy(r,member+24,12);}
    }
    for(unsigned i=0;i<np;i++){p=points[i];unsigned count=(unsigned)half(r,p+66);if(count)copy(r,word(r,p+68),count*8);}
}
static void police(Reader *r)
{
    uint32_t records[32],table=word(r,r->base+0x24);unsigned count=0;
    if(ok(r,table))while(count<32 && !r->error) {
        uint32_t p=word(r,table+count*4);
        if(p && (!ok(r,p) || !ok(r,word(r,p))))break;
        records[count++]=p;
    }
    while(count && !records[count-1])count--;
    header(r,"POL1",count);
    for(unsigned i=0;i<count && !r->error;i++)if(records[i])police_record(r,records[i]);else for(unsigned j=0;j<5;j++)put(r,0);
}
int jgr_mission_convert(JgrDisc *disc,const JgrDiscFile *source,unsigned kind,unsigned sectors,FILE *output)
{
    if(!disc || !source || !output || source->size<0x54 || source->size>MAX_BANK)return -1;
    Reader r={disc,source,output,SET_BASE,0};
    if(kind!=JGR_MISSION_CTRL && kind!=JGR_MISSION_OBJECTS && kind!=JGR_MISSION_CANS)infer_base(&r);
    switch(kind) {
    case JGR_MISSION_CTRL:ctrl(&r);break;
    case JGR_MISSION_GRAPH:graph(&r);break;
    case JGR_MISSION_GROUPS:groups(&r);break;
    case JGR_MISSION_OBJECTS:objects(&r,sectors);break;
    case JGR_MISSION_PEDS:peds(&r);break;
    case JGR_MISSION_POLICE:police(&r);break;
    case JGR_MISSION_RIVALS:rivals(&r);break;
    case JGR_MISSION_WAVES:waves(&r);break;
    case JGR_MISSION_CANS:cans(&r);break;
    default:return -1;
    }
    return r.error || ferror(output) ? -1:0;
}
