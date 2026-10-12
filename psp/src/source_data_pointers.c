#include "source_data_pointers.h"
#include "source_data_io.h"
#if JGR_PORTABLE_ASSETS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef __PSP__
#include <pspkernel.h>
#endif
/* Host tests substitute a 32-bit target address space while keeping real copy buffers. */
#ifndef SOURCE_DATA_POINTER_ADDRESS
#define SOURCE_DATA_POINTER_ADDRESS(pointer) ((uintptr_t)(pointer))
#endif
static unsigned word(const unsigned char *p)
{return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static unsigned checksum(const unsigned char *p,size_t bytes)
{
    return source_data_io_checksum(p,bytes);
}
int source_data_pointers_apply(const void *image,size_t bytes,
    const SourceDataPointerSection *sections,unsigned count,unsigned expected_bytes,
    unsigned layout_crc,unsigned fixup_count)
{
    const unsigned char *p=image;const unsigned endian=1;
    if(!p || !sections || !count || count>4096 || fixup_count>65536 ||
       bytes!=expected_bytes || bytes<40 || *(const unsigned char *)&endian!=1)return -1;
    unsigned header=40+16*(count+fixup_count);
    if(header>bytes || memcmp(p,"JGSD",4) || word(p+4)!=2 || word(p+8)!=count ||
       word(p+12)!=bytes || word(p+16)!=layout_crc || word(p+24)!=header ||
       word(p+28)!=fixup_count || word(p+32)!=4 || word(p+36))return -2;
    if(checksum(p+40,header-40)!=layout_crc || checksum(p+header,bytes-header)!=word(p+20))return -3;
    unsigned previous=header;
    for(unsigned i=0;i<count;i++) {
        const SourceDataPointerSection *s=sections+i;const unsigned char *entry=p+40+i*16;
        uintptr_t address=SOURCE_DATA_POINTER_ADDRESS(s->destination);
        if(word(entry)!=s->id || word(entry+4)!=s->offset || word(entry+8)!=s->bytes ||
           word(entry+12)!=s->alignment || !address || address>UINT32_MAX || !s->bytes ||
           s->bytes-1>UINT32_MAX-address || !s->alignment || (s->alignment&(s->alignment-1)) || s->alignment>4096 ||
           (address&(s->alignment-1)) || (s->offset&(s->alignment-1)) ||
           s->offset<previous || s->offset>bytes || s->bytes>bytes-s->offset)return -4;
        /* A malformed caller registry must not alias one destination into another. */
        for(unsigned j=0;j<i;j++) {
            uintptr_t other=SOURCE_DATA_POINTER_ADDRESS(sections[j].destination);
            if(address<other+sections[j].bytes && other<address+s->bytes)return -4;
        }
        previous=s->offset+s->bytes;
    }
    unsigned last_section=0,last_offset=0;
    for(unsigned i=0;i<fixup_count;i++) {
        const unsigned char *f=p+40+16*count+i*16;
        unsigned source=word(f),offset=word(f+4),target=word(f+8),addend=word(f+12);
        if(source>=count || target>=count || (offset&3) || offset>sections[source].bytes ||
           sections[source].bytes-offset<4 || addend>=sections[target].bytes ||
           (i && (source<last_section || (source==last_section && offset<=last_offset))) ||
           word(p+sections[source].offset+offset))return -5;
        last_section=source;last_offset=offset;
    }
    for(unsigned i=0;i<count;i++)memcpy(sections[i].destination,p+sections[i].offset,sections[i].bytes);
    for(unsigned i=0;i<fixup_count;i++) {
        const unsigned char *f=p+40+16*count+i*16;
        unsigned source=word(f),offset=word(f+4),target=word(f+8),addend=word(f+12);
        unsigned address=(unsigned)SOURCE_DATA_POINTER_ADDRESS(sections[target].destination)+addend;
        memcpy((unsigned char *)sections[source].destination+offset,&address,4);
    }
#ifdef __PSP__
    for(unsigned i=0;i<count;i++)sceKernelDcacheWritebackRange(sections[i].destination,sections[i].bytes);
#endif
    return 0;
}
int source_data_pointers_load_file(const char *path,const SourceDataPointerSection *sections,
    unsigned count,unsigned bytes,unsigned layout_crc,unsigned fixup_count)
{
    if(bytes<40 || bytes>4u*1024u*1024u)return -6;
    const void *memory=NULL;size_t memory_bytes=0;
    int view=source_data_io_view(path,&memory,&memory_bytes);
    if(view)return view<0 ? -7:source_data_pointers_apply(memory,memory_bytes,sections,count,bytes,layout_crc,fixup_count);
    FILE *f=source_data_io_open(path);if(!f)return -7;
    unsigned char *image=malloc(bytes);if(!image){fclose(f);return -8;}
    size_t got=fread(image,1,bytes,f);int failed=got!=bytes || fgetc(f)!=EOF || ferror(f);
    if(fclose(f))failed=1;
    int result=failed ? -9:source_data_pointers_apply(image,got,sections,count,bytes,layout_crc,fixup_count);
    free(image);return result;
}
#endif
