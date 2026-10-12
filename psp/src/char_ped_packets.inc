/* Ninja retains owner-transformed vertices and consumes strips (2956e0 /
   293c54). Keep our existing GE palettes/material packets, but share exact
   weighted records inside each packet and reconstruct ordered strips.
   This cold path runs only while building an immutable packet. */
#include "char_strip.h"
#define PED_PACKET_MAX_VERTICES 256u
static int compact_ped_packet(unsigned char *vertices,unsigned stride,
    unsigned short *indices,unsigned *nvert,unsigned *nidx)
{
    if(!*nvert || *nvert>PED_PACKET_MAX_VERTICES || !*nidx ||
       *nidx>CHAR_STRIP_MAX_INDICES || *nidx%3)return 0;
    for(unsigned i=0;i<*nidx;i++)if(indices[i]>=*nvert)return 0;
    unsigned short remap[PED_PACKET_MAX_VERTICES],strip[CHAR_STRIP_MAX_INDICES];
    unsigned unique=0;
    for(unsigned i=0;i<*nvert;i++) {
        const unsigned char *src=vertices+i*stride;unsigned j=0;
        /* Compare bytes, including palette weights, UV, colour and XYZ.
           Equal positions with distinct seams or owners remain distinct. */
        while(j<unique && memcmp(vertices+j*stride,src,stride))j++;
        if(j==unique){if(j!=i)memcpy(vertices+j*stride,src,stride);unique++;}
        remap[i]=(unsigned short)j;
    }
    for(unsigned i=0;i<*nidx;i++)indices[i]=remap[indices[i]];
    *nvert=unique;
    unsigned count=char_strip_indices(strip,*nidx,indices,*nidx);
    if(!count)return 0; /* Unique records still work with triangle indices. */
    memcpy(indices,strip,count*sizeof *strip);*nidx=count;return 1;
}

/* No command refers to a packet until its construction finishes. Return
   unused trailing pages before publication; all live packets stay pinned. */
static void trim_ped_packet(unsigned page,unsigned *pages,unsigned bytes)
{
    if(!*pages)return;
    unsigned needed=(bytes+255)/256;
    for(unsigned i=needed;i<*pages;i++) {
        ge_packet_pinned[page+i]=0;ge_packet_owner[page+i]=0;
    }
    *pages=needed;
}
