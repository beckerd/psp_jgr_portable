"""Compare the bounded visibility cache with the original independent scan.

Synthetic inputs cover rolling-row reuse, stable score ties, the top-64 cutoff,
negative grid coordinates, bit-exact accumulated node scores across wide
finite input ranges, and allocation-failure and size-limit fallback.
No original game data is required.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int fail_cache;
static unsigned allocations;
static size_t largest;
static void *grid_malloc(size_t size)
{
    allocations++;
    if(size>largest)largest=size;
    return fail_cache ? NULL:malloc(size);
}
#define malloc grid_malloc
#include "portable_stage_derived.c"
#undef malloc

static void reference_node(const VSelected *selected,unsigned count,double x,double z,double *best)
{
    static const double heights[]={-128,0,64,192,384,768};
    for(unsigned iy=0;iy<6;iy++)for(unsigned i=0;i<count;i++) {
        double d[3]={(double)selected[i].center[0]-x,(double)selected[i].center[1]-heights[iy],(double)selected[i].center[2]-z};
        double squared=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];if(squared<25)squared=25;
        double facing=selected[i].area[0]*d[0]+selected[i].area[1]*d[1]+selected[i].area[2]*d[2];
        double fraction=facing/squared,score=fraction*fraction/squared;if(score>best[i])best[i]=score;
    }
}
static int node_bits(void)
{
    VSelected selected[128]={0};unsigned random=1999;
    for(unsigned iteration=0;iteration<100;iteration++) {
        for(unsigned i=0;i<128;i++)for(unsigned k=0;k<3;k++) {
            random=random*1664525u+1013904223u;
            selected[i].center[k]=ldexpf((float)((int)(random&65535)-32768),(int)(random%200)-100);
            random=random*1664525u+1013904223u;
            selected[i].area[k]=ldexpf((float)((int)(random&65535)-32768),(int)(random%200)-100);
        }
        double expected[128]={0},actual[128]={0};
        for(unsigned node=0;node<10;node++) {
            double x=((int)iteration-50)*256.,z=((int)node-5)*256.;
            reference_node(selected,128,x,z,expected);vis_grid_node(selected,128,x,z,actual);
            if(memcmp(expected,actual,sizeof expected))return 25;
        }
    }
    return 0;
}

/* Frozen, uncached cell scan: the independent output oracle. */
static int reference_grid(FILE *out,const VSelected *selected,unsigned count,int gx,int gz,unsigned nx,unsigned nz)
{
    static const double heights[]={-128,0,64,192,384,768};
    double *best=calloc(count ? count:1,sizeof *best);if(!best)return -1;
    for(unsigned z=0;z<nz;z++)for(unsigned x=0;x<nx;x++) {
        memset(best,0,count*sizeof *best);
        for(unsigned ix=0;ix<3;ix++)for(unsigned iz=0;iz<3;iz++)for(unsigned iy=0;iy<6;iy++) {
            double camera[3]={(gx+(int)x+ix*.5)*512,heights[iy],(gz+(int)z+iz*.5)*512};
            for(unsigned i=0;i<count;i++) {
                double d[3];for(unsigned k=0;k<3;k++)d[k]=(double)selected[i].center[k]-camera[k];
                double squared=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];if(squared<25)squared=25;
                double facing=selected[i].area[0]*d[0]+selected[i].area[1]*d[1]+selected[i].area[2]*d[2];double fraction=facing/squared,score=fraction*fraction/squared;if(score>best[i])best[i]=score;
            }
        }
        unsigned ids[64],n=0;for(unsigned i=0;i<count;i++) {
            unsigned at=n;while(at && best[i]>best[ids[at-1]]){if(at<64)ids[at]=ids[at-1];at--;}
            if(at<64){ids[at]=i;if(n<64)n++;}
        }
        unsigned char row[128];for(unsigned i=0;i<64;i++){unsigned id=i<n ? ids[i]:65535;row[i*2]=id;row[i*2+1]=id>>8;}
        if(der_write(out,row,sizeof row)){free(best);return -1;}
    }
    free(best);return 0;
}
static int compare(const VSelected *selected,unsigned count,int gx,int gz,unsigned nx,unsigned nz)
{
    FILE *reference=tmpfile();if(!reference)return 1;
    if(reference_grid(reference,selected,count,gx,gz,nx,nz))return 2;
    for(unsigned mode=0;mode<2;mode++) {
        FILE *actual=tmpfile();if(!actual)return 3;
        fail_cache=(int)mode;allocations=0;largest=0;
        if(vis_grid(actual,selected,count,gx,gz,nx,nz))return 4;
        size_t bytes=((size_t)nx*2+1)*count*3*sizeof(double);
        if(allocations!=(unsigned)(count && bytes<=340u*1024u) || largest>340u*1024u)return 5;
        if(ftell(actual)!=(long)(nx*nz*128u))return 6;
        rewind(reference);rewind(actual);
        for(unsigned i=0;i<nx*nz*128u;i++)if(fgetc(actual)!=fgetc(reference))return 7;
        fclose(actual);
    }
    fclose(reference);return 0;
}
int main(void)
{
    int result=node_bits();if(result)return result;
    const unsigned counts[]={0,1,63,64,65,137,629,1024};
    VSelected selected[1024]={0};
    unsigned random=17;
    for(unsigned i=0;i<1024;i++) {
        for(unsigned k=0;k<3;k++) {
            random=random*1664525u+1013904223u;selected[i].center[k]=(float)((int)(random%81920)-40960)*.125f;
            random=random*1664525u+1013904223u;selected[i].area[k]=(float)((int)(random%8192)-4096)*.25f;
        }
        if(i%8 && i%8<4)selected[i]=selected[i-1];
        if(i%11==0)memset(selected[i].area,0,sizeof selected[i].area);
    }
    /* Camera-on-plane clamp and exact score ties at the top-64 boundary. */
    for(unsigned i=0;i<70;i++) {
        memset(selected[i].center,0,sizeof selected[i].center);
        selected[i].area[0]=0;selected[i].area[1]=4096;selected[i].area[2]=0;
    }
    for(unsigned i=0;i<sizeof counts/sizeof counts[0];i++) {
        int result=compare(selected,counts[i],-3,-4,5,7);
        if(result){fprintf(stderr,"count=%u result=%d\n",counts[i],result);return result;}
    }
    result=compare(selected,1024,65530,-65535,9,2); /* cache size limit */
    if(!result)result=compare(selected,65,-65535,65530,1,1);
    if(!result)result=compare(selected,629,-3,-4,11,13); /* largest real cache */
    return result;
}
'''


class VisibilityGridTests(unittest.TestCase):
    def test_exact_ranking_and_optional_cache_fallback(self):
        with tempfile.TemporaryDirectory(prefix="jgr-vis-grid-") as directory:
            work = Path(directory)
            source = work / "grid.c"
            source.write_text(HARNESS)
            exe = work / "grid"
            flags = os.environ.get("JGR_TEST_CFLAGS", "").split()
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c99", "-D_XOPEN_SOURCE=700",
                "-O2", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror",
                *flags, "-I" + str(ROOT / "psp/src"), str(source),
                str(ROOT / "psp/src/portable_disc.c"),
                str(ROOT / "psp/src/portable_indexed.c"),
                str(ROOT / "psp/src/portable_convert.c"), "-lm", "-o", str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True, timeout=60)


if __name__ == "__main__":
    unittest.main()
