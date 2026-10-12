#include "source_data_sections.h"
#include "source_data_io.h"
#if JGR_PORTABLE_ASSETS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef __PSP__
#include <pspkernel.h>
#endif
static unsigned word(const unsigned char *p)
{return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static unsigned checksum(const unsigned char *p,size_t bytes)
{
    return source_data_io_checksum(p,bytes);
}
int source_data_sections_apply(const void *image,size_t bytes,const SourceDataSection *sections,
                              unsigned count,unsigned expected_bytes,unsigned layout_crc)
{
    const unsigned char *p=image;const unsigned endian=1;
    if(!p || !sections || !count || count>4096 || bytes!=expected_bytes || bytes<32 ||
       *(const unsigned char *)&endian!=1)return -1;
    unsigned header=32+count*16;
    if(header>bytes || memcmp(p,"JGSD",4) || word(p+4)!=1 || word(p+8)!=count ||
       word(p+12)!=bytes || word(p+16)!=layout_crc || word(p+24)!=header || word(p+28))return -2;
    if(checksum(p+32,count*16)!=layout_crc || checksum(p+header,bytes-header)!=word(p+20))return -3;
    unsigned previous=header;
    for(unsigned i=0;i<count;i++) {
        const SourceDataSection *s=sections+i;const unsigned char *entry=p+32+i*16;
        if(word(entry)!=s->id || word(entry+4)!=s->offset || word(entry+8)!=s->bytes ||
           word(entry+12)!=s->alignment || !s->destination || !s->bytes ||
           !s->alignment || (s->alignment&(s->alignment-1)) || s->alignment>4096 ||
           ((uintptr_t)s->destination&(s->alignment-1)) || (s->offset&(s->alignment-1)) ||
           s->offset<previous || s->offset>bytes || s->bytes>bytes-s->offset)return -4;
        previous=s->offset+s->bytes;
    }
    for(unsigned i=0;i<count;i++)memcpy(sections[i].destination,p+sections[i].offset,sections[i].bytes);
#ifdef __PSP__
    for(unsigned i=0;i<count;i++)sceKernelDcacheWritebackRange(sections[i].destination,sections[i].bytes);
#endif
    return 0;
}
int source_data_sections_load(const char *path,const SourceDataSection *sections,
                             unsigned count,unsigned bytes,unsigned layout_crc)
{
    if(bytes<32 || bytes>4u*1024u*1024u)return -5;
    const void *memory=NULL;size_t memory_bytes=0;
    int view=source_data_io_view(path,&memory,&memory_bytes);
    if(view)return view<0 ? -6:source_data_sections_apply(memory,memory_bytes,sections,count,bytes,layout_crc);
    FILE *f=source_data_io_open(path);if(!f)return -6;
    unsigned char *image=malloc(bytes);if(!image){fclose(f);return -7;}
    size_t got=fread(image,1,bytes,f);
    int failed=got!=bytes || fgetc(f)!=EOF || ferror(f);
    if(fclose(f))failed=1;
    int result=failed ? -8:source_data_sections_apply(image,got,sections,count,bytes,layout_crc);
    free(image);return result;
}
#endif
