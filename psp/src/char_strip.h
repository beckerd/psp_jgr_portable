#ifndef JGR_CHAR_STRIP_H
#define JGR_CHAR_STRIP_H

#define CHAR_STRIP_MAX_INDICES 1024u

/* Reconstruct strips without rotating or reordering any source triangle.
   The last vertex stays the provoking vertex. Repeated indices join runs;
   no material, palette or draw boundary is crossed by this helper.
   Input and output must be disjoint. Zero means retain the triangle list;
   rejected inputs leave output untouched. Workspace is bounded, no heap. */
static unsigned char_strip_indices(unsigned short *out,unsigned capacity,
                                  const unsigned short *idx,unsigned count)
{
    if(!out || !idx || count<6 || count>CHAR_STRIP_MAX_INDICES || count%3)return 0;
    unsigned triangles=count/3;
    for(unsigned i=0;i<count;i+=3)
        if(idx[i]==idx[i+1] || idx[i]==idx[i+2] || idx[i+1]==idx[i+2])return 0;

    /* State is the parity of the emitted index count. After ordered
       (a,b,c), odd counts end in (b,c), even counts in (a,c).
       Bit 0: predecessor parity. Bit 1: restart instead of extension. */
    unsigned char previous[CHAR_STRIP_MAX_INDICES/3][2];
    unsigned char path[CHAR_STRIP_MAX_INDICES/3];
    unsigned cost[2]={4,3};
    for(unsigned i=1;i<triangles;i++) {
        unsigned next[2]={~0u,~0u};
        const unsigned short *a=idx+(i-1)*3,*b=idx+i*3;
        for(unsigned p=0;p<2;p++) {
            for(unsigned q=0;q<2;q++) {
                unsigned candidate=cost[p]+(p==q ? 6u:5u);
                if(candidate<next[q]){next[q]=candidate;previous[i][q]=(unsigned char)(p|2u);}
            }
            if(b[0]==(p ? a[2]:a[0]) && b[1]==(p ? a[1]:a[2])) {
                unsigned q=p^1u,candidate=cost[p]+1;
                if(candidate<next[q]){next[q]=candidate;previous[i][q]=(unsigned char)p;}
            }
        }
        cost[0]=next[0];cost[1]=next[1];
    }
    unsigned parity=cost[1]<=cost[0] ? 1u:0u;
    unsigned total=cost[parity];
    if(total>=count || total>capacity)return 0;
    for(unsigned i=triangles-1;i;i--) {
        unsigned step=previous[i][parity];path[i]=(unsigned char)(step|(parity<<2));parity=step&1u;
    }

    unsigned used=0;
    if(parity){out[used++]=idx[0];out[used++]=idx[1];}
    else {out[used++]=idx[1];out[used++]=idx[1];out[used++]=idx[0];}
    out[used++]=idx[2];
    for(unsigned i=1;i<triangles;i++) {
        const unsigned short *t=idx+i*3;
        if(path[i]&2u) {
            unsigned p=used&1u,q=(path[i]>>2)&1u;
            unsigned short last=out[used-1],first=t[q ? 0:1],second=t[q ? 1:0];
            out[used++]=last;
            if(p==q)out[used++]=last;
            out[used++]=first;out[used++]=first;out[used++]=second;
        }
        out[used++]=t[2];
    }
    return used;
}

#endif
