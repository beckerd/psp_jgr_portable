#ifndef JGR_FONT_SOURCE_H
#define JGR_FONT_SOURCE_H
/* 0c13e4/0c154c -> 0c17b4/0c18dc, E_FONTMAIN 512x512.
   Full-width source glyphs occupy 24x24 cells, not stretched ASCII. */
static inline unsigned font_source_code(const unsigned char *p,unsigned available,unsigned *bytes)
{
    *bytes=0;if(!available || !p[0])return 0;
    if(p[0]>=0x80 && p[0]<0xa0){if(available<2 || !p[1])return 0;*bytes=2;return ((unsigned)p[0]<<8)|p[1];}
    *bytes=1;return p[0];
}
static inline int font_source_cell(unsigned code,int *u,int *v,int *width)
{
    int index;
    if(code<0x100){if(code<0x20 || code>=0x80)return 0;index=code;*width=12;*u=index%42*12;*v=index/42*24;}
    else {
        index=(int)code-(code<0x8240 ? 0x8140:code<0x8340 ? 0x8180:0x8220);
        if(index<0 || index>=21*21)return 0;
        *width=24;*u=index%21*24;*v=index/21*24;
    }
    return 1;
}
#endif
