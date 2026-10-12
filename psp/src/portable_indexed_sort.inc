/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Adapted from the FreeBSD qsort used by Apple Libc. Fixed byte swaps make
 * tie ordering independent of target word size and libc. Only bounded color
 * cubes (<=8192 entries) reach this implementation. */
typedef int IndexedCompare(const void *,const void *);
static void iswap(char *a,char *b,size_t n)
{while(n--){char t=*a;*a++=*b;*b++=t;}}
static char *imed3(char *a,char *b,char *c,IndexedCompare *cmp)
{return cmp(a,b)<0 ? (cmp(b,c)<0?b:cmp(a,c)<0?c:a) : (cmp(b,c)>0?b:cmp(a,c)<0?a:c);}
static int iinsert(char *a,size_t n,size_t es,IndexedCompare *cmp,unsigned limit)
{
    unsigned count=0;
    for(char *p=a+es;p<a+n*es;p+=es)for(char *q=p;q>a && cmp(q-es,q)>0;q-=es) {
        iswap(q,q-es,es);if(limit && ++count>limit)return 0;
    }
    return 1;
}
static void indexed_sort(void *input,size_t n,size_t es,IndexedCompare *cmp)
{
    char *a=input;
    while(n>1) {
        if(n<=7){iinsert(a,n,es,cmp,0);return;}
        char *pl=a,*pm=a+(n/2)*es,*pn=a+(n-1)*es;
        if(n>40){size_t d=n/8*es;pl=imed3(pl,pl+d,pl+2*d,cmp);pm=imed3(pm-d,pm,pm+d,cmp);pn=imed3(pn-2*d,pn-d,pn,cmp);}
        pm=imed3(pl,pm,pn,cmp);iswap(a,pm,es);
        char *pa=a+es,*pb=pa,*pc=a+(n-1)*es,*pd=pc;
        int swapped=0,c;
        for(;;) {
            while(pb<=pc && (c=cmp(pb,a))<=0){if(!c){swapped=1;iswap(pa,pb,es);pa+=es;}pb+=es;}
            while(pb<=pc && (c=cmp(pc,a))>=0){if(!c){swapped=1;iswap(pc,pd,es);pd-=es;}pc-=es;}
            if(pb>pc)break;
            iswap(pb,pc,es);swapped=1;pb+=es;pc-=es;
        }
        pn=a+n*es;
        size_t d1=(size_t)(pa-a)<(size_t)(pb-pa)?(size_t)(pa-a):(size_t)(pb-pa);
        if(d1)iswap(a,pb-d1,d1);
        d1=(size_t)(pd-pc)<(size_t)(pn-pd-es)?(size_t)(pd-pc):(size_t)(pn-pd-es);
        if(d1)iswap(pb,pn-d1,d1);
        if(!swapped && iinsert(a,n,es,cmp,1+n/4))return;
        d1=pb-pa;size_t d2=pd-pc;
        /* Recurse only into the smaller partition: logarithmic stack bound. */
        if(d1<=d2){if(d1>es)indexed_sort(a,d1/es,es,cmp);a=pn-d2;n=d2/es;}
        else {if(d2>es)indexed_sort(pn-d2,d2/es,es,cmp);n=d1/es;}
    }
}
