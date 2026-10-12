#ifndef JGR_SMPB_STORAGE_H
#define JGR_SMPB_STORAGE_H
#include "smsb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Load-time only. Preserve original offsets, headers, curves, layer bytes
   and compressed samples; no relocated audio data or decoder approximation. */
static unsigned smpb_u32(const unsigned char *p)
{return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static unsigned smpb_u16(const unsigned char *p){return p[0]|((unsigned)p[1]<<8);}
static int smpb_read(FILE *f,unsigned base,unsigned size,unsigned off,void *out,unsigned n)
{return off<=size && n<=size-off && !fseek(f,(long)base+off,SEEK_SET) && fread(out,1,n,f)==n;}
static void smpb_storage_free(SmsbPatchBank *p)
{
    SmsbWave *w=p->waves;
    while(w){
        SmsbWave *next=w->next;
        if(w->pages) {
            unsigned count=(w->bytes+OSB_SAMPLE_PAGE_SIZE-1)>>OSB_SAMPLE_PAGE_SHIFT;
            for(unsigned i=0;i<count;i++)free(w->pages[i]);
            free(w->pages);
        } else free(w->samples);
        free(w);w=next;
    }
    free(p->metadata);memset(p,0,sizeof *p);
}
static int smpb_storage_load(FILE *f,const unsigned rec[2],SmsbPatchBank *out,unsigned *largest)
{
    SmsbPatchBank p={0};unsigned char header[48],row[48],tail[4];int error=-3;
    unsigned size,metadata,table,count,curves,ncurves;
    memset(out,0,sizeof *out);*largest=0;
    if(!f || rec[1]<48 || rec[1]>0x200000 || fseek(f,0,SEEK_END))return error;
    long end=ftell(f);if(end<0 || rec[0]>(unsigned long)end || rec[1]>(unsigned long)end-rec[0])return error;
    if(!smpb_read(f,rec[0],rec[1],0,header,48) || memcmp(header,"SMPB",4) || smpb_u32(header+4)!=2)return error;
    size=smpb_u32(header+8);if(size<48 || size>rec[1] || !smpb_read(f,rec[0],size,size-4,tail,4) || memcmp(tail,"ENDB",4))return error;
    table=smpb_u32(header+16);count=smpb_u32(header+20);curves=smpb_u32(header+24);ncurves=smpb_u32(header+28);
    if(!count || count>128 || !ncurves || ncurves>256 || table>size || count*4>size-table || curves>size || ncurves*128>size-curves)return error;
    metadata=48;if(table+count*4>metadata)metadata=table+count*4;if(curves+ncurves*128>metadata)metadata=curves+ncurves*128;
    for(unsigned program=0;program<count;program++) {
        unsigned char groups[16];
        if(!smpb_read(f,rec[0],size,table+program*4,row,4))goto done;
        unsigned po=smpb_u32(row);
        if(!smpb_read(f,rec[0],size,po,groups,16))goto done;
        if(po+16>metadata)metadata=po+16;
        for(unsigned g=0;g<4;g++) {
            unsigned group=smpb_u32(groups+g*4);if(!group)continue;
            if(!smpb_read(f,rec[0],size,group,row,16))goto done;
            unsigned layers=smpb_u32(row)&127,at=smpb_u32(row+4);
            if((smpb_u32(row)&0x80) || layers>4 || at>size || layers*48>size-at)goto done;
            if(group+16>metadata)metadata=group+16;
            if(at+layers*48>metadata)metadata=at+layers*48;
            for(unsigned l=0;l<layers;l++) {
                if(!smpb_read(f,rec[0],size,at+l*48,row,48))goto done;
                unsigned ctl=smpb_u16(row),start=((ctl&127)<<16)|smpb_u16(row+2),length=smpb_u16(row+6),format=(ctl>>7)&3;
                unsigned bytes=format==2 ? (length+1)/2:length*2;
                if((format!=0 && format!=2) || !length || smpb_u16(row+4)>=length || start>size || bytes>size-start || row[42]>=ncurves)goto done;
                SmsbWave *w=p.waves;while(w && w->offset!=start)w=w->next;
                if(!w) {
                    if(sizeof *w>*largest)*largest=sizeof *w;
                    w=calloc(1,sizeof *w);if(!w){error=-4;goto done;}
                    w->offset=start;w->next=p.waves;p.waves=w;
                }
                if(bytes>w->bytes)w->bytes=bytes;
            }
        }
    }
    /* Metadata cannot overlap any wave. This also rejects malicious table
       offsets that try to turn a sample allocation into a large image copy. */
    for(SmsbWave *w=p.waves;w;w=w->next)if(metadata>w->offset)goto done;
    p.metadata_size=metadata;p.image_size=size;
    if(metadata>*largest)*largest=metadata;
    p.metadata=malloc(metadata);if(!p.metadata){error=-4;goto done;}
    if(!smpb_read(f,rec[0],size,0,p.metadata,metadata))goto done;
    for(SmsbWave *w=p.waves;w;w=w->next) {
        if(w->bytes<=32768) {
            if(w->bytes>*largest)*largest=w->bytes;
            w->samples=malloc(w->bytes);if(!w->samples){error=-4;goto done;}
            if(!smpb_read(f,rec[0],size,w->offset,w->samples,w->bytes))goto done;
        } else {
            unsigned count=(w->bytes+OSB_SAMPLE_PAGE_SIZE-1)>>OSB_SAMPLE_PAGE_SHIFT;
            if(count*sizeof *w->pages>*largest)*largest=count*sizeof *w->pages;
            w->pages=calloc(count,sizeof *w->pages);if(!w->pages){error=-4;goto done;}
            for(unsigned i=0;i<count;i++) {
                unsigned offset=i*OSB_SAMPLE_PAGE_SIZE,bytes=w->bytes-offset;
                if(bytes>OSB_SAMPLE_PAGE_SIZE)bytes=OSB_SAMPLE_PAGE_SIZE;
                if(bytes>*largest)*largest=bytes;
                w->pages[i]=malloc(bytes);if(!w->pages[i]){error=-4;goto done;}
                if(!smpb_read(f,rec[0],size,w->offset+offset,w->pages[i],bytes))goto done;
            }
            w->samples=w->pages[0];
        }
    }
    *out=p;memset(&p,0,sizeof p);error=0;
done:smpb_storage_free(&p);return error;
}
#endif
