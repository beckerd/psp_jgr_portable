#include "portable_replay_vm.h"
#include "source_fmac.h"
#include <math.h>
#include <fenv.h>
#include <string.h>

static uint32_t physical(uint32_t a){return a<0xe0000000u?a&0x1fffffffu:a;}
static int fail(PortableReplayVM *v,unsigned f,uint32_t a)
{if(!v->fault){v->fault=f;v->fault_pc=v->pc;v->fault_address=a;}return 0;}
void portable_replay_vm_init(PortableReplayVM *v){memset(v,0,sizeof *v);}
int portable_replay_vm_map(PortableReplayVM *v,uint32_t a,void *p,uint32_t n,unsigned flags)
{
    a=physical(a);
    if(!p || !n || n>UINT32_MAX-a || !flags || flags&~7u || v->nregions==PRVM_MAX_REGIONS)return 0;
    for(unsigned i=0;i<v->nregions;i++) {
        const PortableReplayRegion *r=v->region+i;
        if(a<r->address+r->bytes && r->address<a+n)return 0;
    }
    PortableReplayRegion *r=v->region+v->nregions++;
    r->address=a;r->bytes=n;r->permissions=flags;r->data=p;return 1;
}
int portable_replay_vm_direct_memory(PortableReplayVM *v,uint32_t a)
{
    a=physical(a);
    for(unsigned i=0;i<v->nregions;i++)if(v->region[i].address==a){
        v->direct_memory=v->region+i;return 1;
    }
    return 0;
}
static unsigned char *direct_access(PortableReplayVM *v,uint32_t a,unsigned n,unsigned flags)
{
    PortableReplayRegion *r=v->direct_memory;uint32_t p=physical(a);
    if(r && p>=r->address && n && n<=4 && !(n&(n-1)) && !(a&(n-1)) &&
       n<=r->bytes && p-r->address<=r->bytes-n && (r->permissions&flags)==flags)
        return r->data+(p-r->address);
    return NULL;
}
static unsigned char *direct_span(PortableReplayVM *v,uint32_t a,unsigned n,unsigned flags)
{
    PortableReplayRegion *r=v->direct_memory;uint32_t p=physical(a);
    if(r && n && p>=r->address && n<=r->bytes && p-r->address<=r->bytes-n &&
       (r->permissions&flags)==flags)return r->data+(p-r->address);
    return NULL;
}
unsigned portable_replay_vm_copy_loop(PortableReplayVM *v,unsigned budget)
{
    /* Recognize ISA operations, not a stored game function. Each iteration is
     * load-postincrement, decrement/test, store, delayed branch and pointer
     * increment. Batching ends before the caller's next device event. */
    if(v->fault || v->delayed || budget<5 || (v->pc&1))return 0;
    const unsigned char *code=direct_span(v,v->pc,10,PRVM_READ|PRVM_EXEC);
    if(!code)return 0;
    unsigned w[5];for(unsigned i=0;i<5;i++)w[i]=code[i*2]|(unsigned)code[i*2+1]<<8;
    unsigned floating=(w[0]&0xf00f)==0xf009;
    if(!floating && (w[0]&0xf00f)!=0x6006)return 0;
    unsigned value=(w[0]>>8)&15,source=(w[0]>>4)&15;
    unsigned counter=(w[1]>>8)&15,destination=(w[2]>>8)&15;
    unsigned width=floating && (v->fpscr&(1u<<20))?8:4;
    if((w[1]&0xf0ff)!=0x4010 || (w[2]&0xf00f)!=(floating?0xf00a:0x2002) ||
       ((w[2]>>4)&15)!=value || (w[3]&0xff00)!=0x8f00 ||
       (int8_t)(w[3]&255)*2+10!=0 || (w[4]&0xf0ff)!=(0x7000|width) ||
       ((w[4]>>8)&15)!=destination || source==destination ||
       counter==source || counter==destination ||
       (!floating && (value==source || value==destination || value==counter)))return 0;
    unsigned count=v->r[counter];
    if(!count)return 0; /* Preserve the original underflow/fault behavior. */
    if(count>budget/5)count=budget/5;
    if(count>UINT32_MAX/width)return 0;
    unsigned bytes=count*width;
    if((v->r[source]&3) || (v->r[destination]&3))return 0;
    unsigned char *src=direct_span(v,v->r[source],bytes,PRVM_READ);
    unsigned char *dst=direct_span(v,v->r[destination],bytes,PRVM_WRITE);
    if(!src)return 0;
    /* A write into this loop could change subsequent decoded instructions. */
    uint32_t output=physical(v->r[destination]),pc=physical(v->pc);
    if(output<pc+10 && pc<output+bytes)return 0;
    if(!dst && (!v->copy_output_hook ||
       v->copy_output_hook(v,v->r[destination],bytes,v->copy_output_context)!=1))return 0;
    uint32_t last[2]={0,0};
    for(unsigned i=dst?0:count-1;i<count;i++){
        /* Forward element copies preserve overlapping-copy semantics. */
        for(unsigned j=0;j<width/4;j++){
            const unsigned char *p=src+i*width+j*4;
            last[j]=p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        }
        if(dst)for(unsigned j=0;j<width;j++)dst[i*width+j]=(unsigned char)(last[j/4]>>((j&3)*8));
    }
    if(floating){
        uint32_t *reg=width==8?(value&1?v->xf:v->fr)+(value&14):v->fr+value;
        reg[0]=last[0];if(width==8)reg[1]=last[1];
    }else v->r[value]=last[0];
    v->r[source]+=bytes;v->r[destination]+=bytes;v->r[counter]-=count;
    v->sr=(v->sr&~1u)|(v->r[counter]==0);
    v->pc+=v->r[counter]?0:10;v->delay=v->pc;
    v->fault_opcode=w[4];v->instructions+=(uint64_t)count*5;
    return count*5;
}
unsigned portable_replay_vm_fill_loop(PortableReplayVM *v,unsigned budget)
{
    if(v->fault || v->delayed || budget<8 || (v->pc&1))return 0;
    const unsigned char *code=direct_span(v,v->pc,16,PRVM_READ|PRVM_EXEC);
    if(!code)return 0;
    unsigned w[8];for(unsigned i=0;i<8;i++)w[i]=code[i*2]|(unsigned)code[i*2+1]<<8;
    unsigned scratch=(w[0]>>8)&15,index=(w[0]>>4)&15;
    unsigned value=(w[1]>>4)&15,counter=(w[3]>>8)&15,limit=(w[5]>>8)&15;
    if((w[0]&0xf00f)!=0x6003 || w[1]!=(0x0006|(index<<8)|(value<<4)) ||
       w[2]!=(0x300c|(scratch<<8)) || (w[3]&0xf0ff)!=0x7002 ||
       w[4]!=(0x1001|(scratch<<8)|(value<<4)) ||
       w[5]!=(0x3007|(limit<<8)|(counter<<4)) ||
       (w[6]&0xff00)!=0x8d00 || (int8_t)(w[6]&255)*2+16!=0 ||
       w[7]!=(0x7008|(index<<8)))return 0;
    unsigned registers[]={0,scratch,index,value,counter,limit};
    for(unsigned i=1;i<6;i++)for(unsigned j=0;j<i;j++)if(registers[i]==registers[j])return 0;
    uint32_t start=v->r[counter],stop=v->r[limit];
    /* Signed comparisons with wraparound remain on the instruction path. */
    if(start>INT32_MAX-2u || stop>INT32_MAX)return 0;
    unsigned count=start<stop?(stop-start+1)/2:1;
    if(count>budget/8)count=budget/8;
    if(count>(INT32_MAX-start)/2 || count>UINT32_MAX/8)return 0;
    unsigned bytes=count*8;uint32_t address=v->r[0]+v->r[index];
    if(address&3)return 0;
    unsigned char *dst=direct_span(v,address,bytes,PRVM_WRITE);
    if(!dst)return 0;
    uint32_t output=physical(address),pc=physical(v->pc);
    if(output<pc+16 && pc<output+bytes)return 0;
    uint32_t word=v->r[value];
    if((word&255)*0x01010101u==word)memset(dst,(int)(word&255),bytes);
    else for(unsigned i=0;i<bytes;i++)dst[i]=(unsigned char)(word>>((i&3)*8));
    v->r[scratch]=address+bytes-8;v->r[index]+=bytes;v->r[counter]+=count*2;
    unsigned again=v->r[counter]<stop;
    v->sr=(v->sr&~1u)|again;v->pc+=again?0:16;v->delay=v->pc;
    v->fault_opcode=w[7];v->instructions+=(uint64_t)count*8;
    return count*8;
}
static unsigned char *access(PortableReplayVM *v,uint32_t a,unsigned n,unsigned flags)
{
    uint32_t p=physical(a);
    if((n!=1 && n!=2 && n!=4) || (a&(n-1))){fail(v,PRVM_ALIGNMENT,a);return NULL;}
    for(unsigned i=0;i<v->nregions;i++) {
        PortableReplayRegion *r=v->region+i;
        if(p>=r->address && n<=r->bytes && p-r->address<=r->bytes-n && (r->permissions&flags)==flags)
            return r->data+(p-r->address);
    }
    fail(v,PRVM_MEMORY,a);return NULL;
}
static uint32_t read_value(PortableReplayVM *v,uint32_t a,unsigned n,unsigned flags)
{
    const unsigned char *p=direct_access(v,a,n,flags);
    if(!p && v->read_hook && flags==PRVM_READ) {
        uint32_t x=0;int handled=v->read_hook(v,a,n,&x,v->read_context);
        if(handled<0){fail(v,PRVM_HOOK,a);return 0;}
        if(handled)return x;
    }
    if(!p)p=access(v,a,n,flags);
    uint32_t x=0;
    if(p){
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        if(n==4)memcpy(&x,p,4);
        else if(n==2){uint16_t h;memcpy(&h,p,2);x=h;}
        else x=*p;
#else
        for(unsigned i=0;i<n;i++)x|=(uint32_t)p[i]<<(i*8);
#endif
    }
    return x;
}
int portable_replay_vm_read(PortableReplayVM *v,uint32_t a,unsigned n,uint32_t *x)
{if(!x)return 0;*x=read_value(v,a,n,PRVM_READ);return !v->fault;}
int portable_replay_vm_write(PortableReplayVM *v,uint32_t a,unsigned n,uint32_t x)
{
    unsigned char *p=direct_access(v,a,n,PRVM_WRITE);
    if(!p && v->write_hook) {
        int handled=v->write_hook(v,a,n,x,v->write_context);
        if(handled<0)return fail(v,PRVM_HOOK,a);
        if(handled)return 1;
    }
    if(!p)p=access(v,a,n,PRVM_WRITE);
    if(!p)return 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if(n==4)memcpy(p,&x,4);
    else if(n==2){uint16_t h=(uint16_t)x;memcpy(p,&h,2);}
    else *p=(unsigned char)x;
#else
    for(unsigned i=0;i<n;i++)p[i]=(unsigned char)(x>>(i*8));
#endif
    return 1;
}
static uint32_t sext(uint32_t x,unsigned bits)
{uint32_t sign=1u<<(bits-1);return (x^sign)-sign;}
static uint32_t sar(uint32_t x,unsigned n)
{return n ? (x>>n)|((x&0x80000000u)?(~0u<<(32-n)):0):x;}
static void set_t(PortableReplayVM *v,int t){v->sr=(v->sr&~1u)|(t!=0);}
static float real(uint32_t x){float f;memcpy(&f,&x,4);return f;}
static uint32_t bits(float f){uint32_t x;memcpy(&x,&f,4);return x;}
/* The PSP C library's fenv entry points are unsupported stubs. Save and
 * restore the actual scalar FPU control register on that target. */
static unsigned rounding_zero_enter(void)
{
#ifdef __PSP__
    unsigned saved,next;
    __asm__ volatile("cfc1 %0, $31" : "=r"(saved));
    next=(saved&~3u)|1u;
    __asm__ volatile("ctc1 %0, $31\n\tnop\n\tnop\n\tnop" :: "r"(next) : "memory");
    return saved;
#else
    int saved=fegetround();fesetround(FE_TOWARDZERO);return (unsigned)saved;
#endif
}
static void rounding_leave(unsigned saved)
{
#ifdef __PSP__
    __asm__ volatile("ctc1 %0, $31\n\tnop\n\tnop\n\tnop" :: "r"(saved) : "memory");
#else
    fesetround((int)saved);
#endif
}
static uint32_t arithmetic_zero(uint32_t a,uint32_t b,unsigned op)
{
    unsigned saved=rounding_zero_enter();
    volatile float x=real(a),y=real(b),z;
    if(op==0)z=x+y;else if(op==1)z=x-y;else if(op==2)z=x*y;else z=x/y;
    uint32_t result=bits(z);rounding_leave(saved);return result;
}
static int fmac_zero(uint32_t x,uint32_t y,uint32_t z,uint32_t *out)
{
    if(!(x&0x7fffffffu) || !(y&0x7fffffffu)){*out=z;return 1;}
    if(!(z&0x7fffffffu)){*out=arithmetic_zero(x,y,2);return 1;}
    unsigned ex=(x>>23)&255,ey=(y>>23)&255,ez=(z>>23)&255;
    int shift=(int)ez-(int)ex-(int)ey+150,exponent=(int)ex+(int)ey-300;
    if(!ex || ex==255 || !ey || ey==255 || !ez || ez==255 || shift< -14 || shift>39)return 0;
    uint64_t p=(uint64_t)((x&0x7fffff)|0x800000)*((y&0x7fffff)|0x800000);
    uint64_t q=(z&0x7fffff)|0x800000,mag;uint32_t sign=(x^y)&0x80000000u;
    if(shift>=0)q<<=shift;else {p<<=-shift;exponent+=shift;}
    if(sign==(z&0x80000000u))mag=p+q;
    else if(p>=q)mag=p-q;else {mag=q-p;sign=z&0x80000000u;}
    if(!mag){*out=0;return 1;}
    unsigned hi=(unsigned)(mag>>32);int bit=hi?63-__builtin_clz(hi):31-__builtin_clz((unsigned)mag);
    int power=exponent+bit;if(power< -126 || power>127)return 0;
    uint32_t mantissa=bit>23?(uint32_t)(mag>>(bit-23)):(uint32_t)mag<<(23-bit);
    *out=sign|((unsigned)(power+126)*0x800000u+mantissa);return 1;
}
static uint32_t sqrt_zero(uint32_t a,int reciprocal)
{
    unsigned saved=rounding_zero_enter();
    volatile float x=real(a),z=sqrtf(x);if(reciprocal)z=1.f/z;
    uint32_t result=bits(z);rounding_leave(saved);return result;
}
static uint32_t integer_zero(uint32_t a)
{
    unsigned saved=rounding_zero_enter();
    volatile int32_t x=(int32_t)a;volatile float z=(float)x;
    uint32_t result=bits(z);rounding_leave(saved);return result;
}
static void fpscr_set(PortableReplayVM *v,uint32_t value)
{
    if((v->fpscr^value)&(1u<<21))for(unsigned i=0;i<16;i++){
        uint32_t w=v->fr[i];v->fr[i]=v->xf[i];v->xf[i]=w;
    }
    v->fpscr=value;
}
static void sr_set(PortableReplayVM *v,uint32_t value)
{
    if((v->sr^value)&(1u<<29))for(unsigned i=0;i<8;i++){
        uint32_t w=v->r[i];v->r[i]=v->bank[i];v->bank[i]=w;
    }
    v->sr=value;
}
int portable_replay_vm_interrupt(PortableReplayVM *v,unsigned priority)
{
    if(v->delayed || (v->sr&(1u<<28)) || !priority || priority>15 || priority<=((v->sr>>4)&15))return 0;
    v->ssr=v->sr;v->spc=v->pc;v->sgr=v->r[15];
    sr_set(v,v->sr|0x70000000u);v->pc=v->vbr+0x600;return 1;
}
static int branch(PortableReplayVM *v,uint32_t target,int slot)
{if(slot)return fail(v,PRVM_DELAY_SLOT,v->pc);v->delay=target;v->delayed=1;return 1;}

static int step(PortableReplayVM *v)
{
    uint32_t pc=v->pc,pending=v->delay;int slot=v->delayed!=0;
    uint32_t w=read_value(v,pc,2,PRVM_READ|PRVM_EXEC);
    if(v->fault)return 0;
    v->fault_opcode=w;v->delayed=0;v->instructions++;
    unsigned n=(w>>8)&15,m=(w>>4)&15,k=w&15;
    uint32_t next=pc+2,x=0,a=0;unsigned width=0;
#define R(i) (v->r[(i)])
#define RD(addr,size) read_value(v,(addr),(size),PRVM_READ)
#define WR(addr,size,value) portable_replay_vm_write(v,(addr),(size),(value))
    /* These complete major-opcode groups cannot overlap control/cache/system
     * instructions. Jump directly to their shared decoder instead of testing
     * every unrelated system opcode on each load, store, ALU or FPU operation. */
    switch(w>>12){
        case 2:goto register_store;
        case 3:goto register_arithmetic;
        case 6:goto register_load;
        case 15:goto floating_point;
        default:break;
    }
    if(w==0x0009){} /* nop */
    else if(w==0x000b)branch(v,v->pr,slot);
    else if(w==0x002b){if(branch(v,v->spc,slot))sr_set(v,v->ssr);}
    else if(w==0x0008)set_t(v,0);
    else if(w==0x0018)set_t(v,1);
    else if(w==0x0019)v->sr&=~0x301u;
    else if((w&0xf000)==0xe000)R(n)=sext(w&255,8);
    else if((w&0xf000)==0x7000)R(n)+=sext(w&255,8);
    else if((w&0xf000)==0x9000)R(n)=sext(RD(pc+4+(w&255)*2,2),16);
    else if((w&0xf000)==0xd000)R(n)=RD(((pc+4)&~3u)+(w&255)*4,4);
    else if((w&0xf000)==0x5000)R(n)=RD(R(m)+k*4,4);
    else if((w&0xf000)==0x1000)WR(R(n)+k*4,4,R(m));
    else if((w&0xff00)==0xc700)R(0)=((pc+4)&~3u)+(w&255)*4;
    else if((w&0xf000)==0xa000)branch(v,pc+4+sext(w&4095,12)*2,slot);
    else if((w&0xf000)==0xb000){v->pr=pc+4;branch(v,pc+4+sext(w&4095,12)*2,slot);}
    else if((w&0xfd00)==0x8900 || (w&0xfd00)==0x8d00) {
        int take=(v->sr&1)!=((w>>9)&1);
        uint32_t target=pc+4+sext(w&255,8)*2;
        if(slot)fail(v,PRVM_DELAY_SLOT,pc);
        else if(w&0x0400)branch(v,take?target:pc+4,0);
        else if(take)next=target;
    }
    else if((w&0xff00)==0x8800)set_t(v,R(0)==sext(w&255,8));
    else if((w&0xff00)==0xc800)set_t(v,!(R(0)&(w&255)));
    else if((w&0xff00)==0xc900)R(0)&=w&255;
    else if((w&0xff00)==0xca00)R(0)^=w&255;
    else if((w&0xff00)==0xcb00)R(0)|=w&255;
    else if((w&0xff00)==0x8000 || (w&0xff00)==0x8100)WR(R(m)+k*(1+((w>>8)&1)),1+((w>>8)&1),R(0));
    else if((w&0xff00)==0x8400 || (w&0xff00)==0x8500){width=1+((w>>8)&1);R(0)=sext(RD(R(m)+k*width,width),8*width);}
    else if((w&0xf0ff)==0x400b){v->pr=pc+4;branch(v,R(n),slot);}
    else if((w&0xf0ff)==0x402b)branch(v,R(n),slot);
    else if((w&0xf0ff)==0x0093 || (w&0xf0ff)==0x00a3 || (w&0xf0ff)==0x00b3){} /* coherent RAM cache maintenance */
    else if((w&0xf0ff)==0x0083){if((R(n)&0xfc000000u)==0xe0000000u &&
        (!v->store_queue_hook || !v->store_queue_hook(v,R(n),v->store_queue_context)))fail(v,PRVM_MEMORY,R(n));}
    else if((w&0xf0ff)==0x00c3)WR(R(n),4,R(0)); /* coherent RAM: cache allocate is a store */
    else if((w&0xf0ff)==0x0023)branch(v,pc+4+R(n),slot);
    else if((w&0xf0ff)==0x0003){v->pr=pc+4;branch(v,pc+4+R(n),slot);}
    else if((w&0xf0ff)==0x4022){R(n)-=4;WR(R(n),4,v->pr);}
    else if((w&0xf0ff)==0x4026){v->pr=RD(R(n),4);R(n)+=4;}
    else if((w&0xf0ff)==0x002a)R(n)=v->pr;
    else if((w&0xf0ff)==0x0002)R(n)=v->sr;
    else if((w&0xf0ff)==0x4003){R(n)-=4;WR(R(n),4,v->sr);}
    else if((w&0xf0ff)==0x400e)sr_set(v,R(n));
    else if((w&0xf0ff)==0x4007){x=RD(R(n),4);R(n)+=4;sr_set(v,x);}
    else if((w&0xf08f)==0x0082)R(n)=v->bank[(w>>4)&7];
    else if((w&0xf08f)==0x408e)v->bank[(w>>4)&7]=R(n);
    else if((w&0xf08f)==0x4083){R(n)-=4;WR(R(n),4,v->bank[(w>>4)&7]);}
    else if((w&0xf08f)==0x4087){v->bank[(w>>4)&7]=RD(R(n),4);R(n)+=4;}
    else if((w&0xf08f)==0x0002 && m>=1 && m<=4) {
        uint32_t *cr=m==1?&v->gbr:m==2?&v->vbr:m==3?&v->ssr:&v->spc;R(n)=*cr;
    }
    else if((w&0xf08f)==0x400e && m>=1 && m<=4) {
        uint32_t *cr=m==1?&v->gbr:m==2?&v->vbr:m==3?&v->ssr:&v->spc;*cr=R(n);
    }
    else if((w&0xf08f)==0x4003 && m>=1 && m<=4) {
        uint32_t *cr=m==1?&v->gbr:m==2?&v->vbr:m==3?&v->ssr:&v->spc;R(n)-=4;WR(R(n),4,*cr);
    }
    else if((w&0xf08f)==0x4007 && m>=1 && m<=4) {
        uint32_t *cr=m==1?&v->gbr:m==2?&v->vbr:m==3?&v->ssr:&v->spc;*cr=RD(R(n),4);R(n)+=4;
    }
    else if((w&0xf0ff)==0x402a)v->pr=R(n);
    else if((w&0xf0ff)==0x005a)R(n)=v->fpul;
    else if((w&0xf0ff)==0x405a)v->fpul=R(n);
    else if((w&0xf0ff)==0x006a)R(n)=v->fpscr;
    else if((w&0xf0ff)==0x406a)fpscr_set(v,R(n));
    else if((w&0xf0ff)==0x4052){R(n)-=4;WR(R(n),4,v->fpul);}
    else if((w&0xf0ff)==0x4056){v->fpul=RD(R(n),4);R(n)+=4;}
    else if((w&0xf0ff)==0x4062){R(n)-=4;WR(R(n),4,v->fpscr);}
    else if((w&0xf0ff)==0x4066){fpscr_set(v,RD(R(n),4));R(n)+=4;}
    else if((w&0xf0ff)==0x0029)R(n)=v->sr&1;
    else if((w&0xf0ff)==0x001a)R(n)=v->macl;
    else if((w&0xf0ff)==0x000a)R(n)=v->mach;
    else if((w&0xf0ff)==0x401a)v->macl=R(n);
    else if((w&0xf0ff)==0x400a)v->mach=R(n);
    else if((w&0xf0ef)==0x4002){R(n)-=4;WR(R(n),4,(w&16)?v->macl:v->mach);}
    else if((w&0xf0ef)==0x4006){x=RD(R(n),4);R(n)+=4;if(w&16)v->macl=x;else v->mach=x;}
    else if((w&0xf0ff)==0x401b){x=RD(R(n),1);set_t(v,x==0);WR(R(n),1,x|0x80);}
    else if((w&0xf0ff)==0x4010){R(n)--;set_t(v,R(n)==0);}
    else if((w&0xf0ff)==0x4011)set_t(v,(int32_t)R(n)>=0);
    else if((w&0xf0ff)==0x4015)set_t(v,(int32_t)R(n)>0);
    else if((w&0xf0ff)==0x4000 || (w&0xf0ff)==0x4020){set_t(v,R(n)>>31);R(n)<<=1;}
    else if((w&0xf0ff)==0x4001){set_t(v,R(n)&1);R(n)>>=1;}
    else if((w&0xf0ff)==0x4021){set_t(v,R(n)&1);R(n)=sar(R(n),1);}
    else if((w&0xf0ff)==0x4004){x=R(n)>>31;R(n)=(R(n)<<1)|x;set_t(v,x);}
    else if((w&0xf0ff)==0x4005){x=R(n)&1;R(n)=(R(n)>>1)|(x<<31);set_t(v,x);}
    else if((w&0xf0ff)==0x4024){x=R(n)>>31;R(n)=(R(n)<<1)|(v->sr&1);set_t(v,x);}
    else if((w&0xf0ff)==0x4025){x=R(n)&1;R(n)=(R(n)>>1)|((v->sr&1)<<31);set_t(v,x);}
    else if((w&0xf0ff)==0x4008)R(n)<<=2;
    else if((w&0xf0ff)==0x4018)R(n)<<=8;
    else if((w&0xf0ff)==0x4028)R(n)<<=16;
    else if((w&0xf0ff)==0x4009)R(n)>>=2;
    else if((w&0xf0ff)==0x4019)R(n)>>=8;
    else if((w&0xf0ff)==0x4029)R(n)>>=16;
    else if((w&0xf00f)==0x400c || (w&0xf00f)==0x400d) {
        if(!(R(m)&0x80000000u))R(n)<<=R(m)&31;
        else if(!(R(m)&31))R(n)=k==12?sar(R(n),31):0;
        else {unsigned count=(0u-R(m))&31;R(n)=k==12?sar(R(n),count):R(n)>>count;}
    }
    else if((w&0xf000)==0x6000) {
register_load:
        if(k<=2 || (k>=4 && k<=6)){width=1u<<(k&3);a=R(m);x=RD(a,width);if(k>=4 && n!=m)R(m)+=width;R(n)=width==4?x:sext(x,width*8);}
        else switch(k){
            case 3:R(n)=R(m);break;case 7:R(n)=~R(m);break;
            case 8:R(n)=(R(m)&0xffff0000u)|((R(m)&255)<<8)|((R(m)>>8)&255);break;
            case 9:R(n)=(R(m)<<16)|(R(m)>>16);break;
            case 10:{uint64_t t=(uint64_t)R(m)+(v->sr&1);R(n)=0-(uint32_t)t;set_t(v,t!=0);break;}
            case 11:R(n)=0-R(m);break;case 12:R(n)=R(m)&255;break;
            case 13:R(n)=R(m)&65535;break;case 14:R(n)=sext(R(m)&255,8);break;
            case 15:R(n)=sext(R(m)&65535,16);break;
            default:fail(v,PRVM_OPCODE,pc);
        }
    }
    else if((w&0xf000)==0x2000) {
register_store:
        if(k<=2 || (k>=4 && k<=6)){width=1u<<(k&3);x=R(m);if(k>=4)R(n)-=width;WR(R(n),width,x);}
        else switch(k){
            case 7:{unsigned q=R(n)>>31,b=R(m)>>31;v->sr=(v->sr&~0x301u)|(q<<8)|(b<<9)|(q^b);break;}
            case 8:set_t(v,!(R(n)&R(m)));break;case 9:R(n)&=R(m);break;
            case 10:R(n)^=R(m);break;case 11:R(n)|=R(m);break;
            case 12:x=R(n)^R(m);set_t(v,!(x&255) || !(x&0xff00) || !(x&0xff0000) || !(x&0xff000000u));break;
            case 13:R(n)=(R(n)>>16)|(R(m)<<16);break;
            case 14:v->macl=(R(n)&65535)*(R(m)&65535);break;
            case 15:v->macl=(uint32_t)((int32_t)(int16_t)R(n)*(int32_t)(int16_t)R(m));break;
            default:fail(v,PRVM_OPCODE,pc);
        }
    }
    else if((w&0xf000)==0x3000) {
register_arithmetic:
        switch(k){case 0:set_t(v,R(n)==R(m));break;
        case 4:{
            unsigned oldq=(v->sr>>8)&1,b=(v->sr>>9)&1,sign=R(n)>>31;
            uint32_t shifted=(R(n)<<1)|(v->sr&1);unsigned carry;
            if(oldq==b){carry=shifted<R(m);R(n)=shifted-R(m);}
            else {uint64_t sum=(uint64_t)shifted+R(m);carry=(unsigned)(sum>>32);R(n)=(uint32_t)sum;}
            unsigned q=sign^carry^b;v->sr=(v->sr&~0x101u)|(q<<8)|(q==b);break;
        }
        case 5:{uint64_t result=(uint64_t)R(n)*R(m);v->macl=(uint32_t)result;v->mach=(uint32_t)(result>>32);break;}
        case 13:{uint64_t result=(uint64_t)((int64_t)(int32_t)R(n)*(int32_t)R(m));v->macl=(uint32_t)result;v->mach=(uint32_t)(result>>32);break;}
        case 2:set_t(v,R(n)>=R(m));break;case 3:set_t(v,(int32_t)R(n)>=(int32_t)R(m));break;
        case 6:set_t(v,R(n)>R(m));break;case 7:set_t(v,(int32_t)R(n)>(int32_t)R(m));break;
        case 8:R(n)-=R(m);break;case 12:R(n)+=R(m);break;
        case 10:{uint32_t old=R(n);uint64_t sub=(uint64_t)R(m)+(v->sr&1);R(n)=old-(uint32_t)sub;set_t(v,(uint64_t)old<sub);break;}
        case 14:{uint64_t sum=(uint64_t)R(n)+R(m)+(v->sr&1);R(n)=(uint32_t)sum;set_t(v,sum>>32);break;}
        default:fail(v,PRVM_OPCODE,pc);}
    }
    else if((w&0xf00f)>=0x0004 && (w&0xf00f)<=0x0006){width=1u<<(k-4);WR(R(0)+R(n),width,R(m));}
    else if((w&0xf00f)>=0x000c && (w&0xf00f)<=0x000e){width=1u<<(k-12);x=RD(R(0)+R(m),width);R(n)=width==4?x:sext(x,width*8);}
    else if((w&0xf00f)==0x0007)v->macl=R(n)*R(m);
    else if((w&0xf000)==0xf000) {
floating_point:
        /* Arithmetic remains single precision, nearest-even. Wide register
         * transfers and bank swaps are also used by original matrix helpers. */
        if(w==0xfbfd)fpscr_set(v,v->fpscr^(1u<<21));
        else if(w==0xf3fd)v->fpscr^=1u<<20;
        else if((v->fpscr&(1u<<20)) && k>=6 && k<=12) {
            uint32_t *dst=(n&1?v->xf:v->fr)+(n&14),*src=(m&1?v->xf:v->fr)+(m&14);
            if(k==12){uint32_t lo=src[0],hi=src[1];dst[0]=lo;dst[1]=hi;}
            else if(k==6 || k==8 || k==9){a=R(m)+(k==6?R(0):0);dst[0]=RD(a,4);dst[1]=RD(a+4,4);if(k==9)R(m)+=8;}
            else {if(k==11)R(n)-=8;a=R(n)+(k==7?R(0):0);WR(a,4,src[0]);WR(a+4,4,src[1]);}
        }
        else if((v->fpscr&((1u<<19)|3u))==1 && k==14){if(!fmac_zero(v->fr[0],v->fr[m],v->fr[n],v->fr+n))fail(v,PRVM_FPU_MODE,pc);}
        else if((v->fpscr&((1u<<19)|3u))==1 && ((w&0xf0ff)==0xf06d || (w&0xf0ff)==0xf07d))v->fr[n]=sqrt_zero(v->fr[n],(w&0xf0ff)==0xf07d);
        else if((v->fpscr&((1u<<19)|3u))==1 && (w&0xf0ff)==0xf02d)v->fr[n]=integer_zero(v->fpul);
        else if((v->fpscr&((1u<<19)|3u))==1 && k<4)v->fr[n]=arithmetic_zero(v->fr[n],v->fr[m],k);
        else if((v->fpscr&((1u<<19)|3u)) && !(k>=6 && k<=12) &&
                (w&0xf0ff)!=0xf00d && (w&0xf0ff)!=0xf01d &&
                (w&0xf0ff)!=0xf08d && (w&0xf0ff)!=0xf09d &&
                !(!(v->fpscr&(1u<<19)) && (k==4 || k==5 || (w&0xf0ff)==0xf04d ||
                  (w&0xf0ff)==0xf05d || (w&0xf0ff)==0xf03d || (w&0xf1ff)==0xf0fd)))fail(v,PRVM_FPU_MODE,pc);
        else if((w&0xf1ff)==0xf0fd) {
            if(!v->fsca_positive || v->fsca_words!=32768)fail(v,PRVM_FPU_MODE,pc);
            else for(unsigned i=0;i<2;i++) {
                unsigned angle=(v->fpul+i*16384)&65535;
                v->fr[n+i]=v->fsca_positive[angle&32767]^(angle&32768?0x80000000u:0);
            }
        }
        else if((w&0xf0ff)==0xf0ed) {
            unsigned lhs=n&12,rhs=(n&3)*4;double total=0;
            for(unsigned i=0;i<4;i++){double product=(double)real(v->fr[lhs+i])*real(v->fr[rhs+i]);total=i?total+product:product;}
            v->fr[lhs+3]=bits((float)total);
        }
        else if((w&0xf3ff)==0xf1fd) {
            uint32_t result[4];unsigned vector=n&12;
            for(unsigned i=0;i<4;i++){
                double total=0;
                for(unsigned j=0;j<4;j++){double product=(double)real(v->xf[j*4+i])*real(v->fr[vector+j]);total=j?total+product:product;}
                result[i]=bits((float)total);
            }
            memcpy(v->fr+vector,result,sizeof result);
        }
        else if(k==8)v->fr[n]=RD(R(m),4);
        else if(k==9){v->fr[n]=RD(R(m),4);R(m)+=4;}
        else if(k==10)WR(R(n),4,v->fr[m]);
        else if(k==11){R(n)-=4;WR(R(n),4,v->fr[m]);}
        else if(k==12)v->fr[n]=v->fr[m];
        else if(k==6)v->fr[n]=RD(R(0)+R(m),4);
        else if(k==7)WR(R(0)+R(n),4,v->fr[m]);
        else if(k==0)v->fr[n]=bits(real(v->fr[n])+real(v->fr[m]));
        else if(k==1)v->fr[n]=bits(real(v->fr[n])-real(v->fr[m]));
        else if(k==2)v->fr[n]=bits(real(v->fr[n])*real(v->fr[m]));
        else if(k==3)v->fr[n]=bits(real(v->fr[n])/real(v->fr[m]));
        else if(k==4)set_t(v,real(v->fr[n])==real(v->fr[m]));
        else if(k==5)set_t(v,real(v->fr[n])>real(v->fr[m]));
        else if(k==14)v->fr[n]=bits(source_fmac(real(v->fr[0]),real(v->fr[m]),real(v->fr[n])));
        else if((w&0xf0ff)==0xf02d)v->fr[n]=bits((float)(int32_t)v->fpul);
        else if((w&0xf0ff)==0xf00d)v->fr[n]=v->fpul;
        else if((w&0xf0ff)==0xf01d)v->fpul=v->fr[n];
        else if((w&0xf0ff)==0xf04d)v->fr[n]^=0x80000000u;
        else if((w&0xf0ff)==0xf05d)v->fr[n]&=0x7fffffffu;
        else if((w&0xf0ff)==0xf06d)v->fr[n]=bits(sqrtf(real(v->fr[n])));
        else if((w&0xf0ff)==0xf07d)v->fr[n]=bits(1.f/sqrtf(real(v->fr[n])));
        else if((w&0xf0ff)==0xf03d){float f=real(v->fr[n]);v->fpul=!(f<2147483648.0f)?0x7fffffffu:f<-2147483648.0f?0x80000000u:(uint32_t)(int32_t)f;}
        else if((w&0xf0ff)==0xf08d)v->fr[n]=0;
        else if((w&0xf0ff)==0xf09d)v->fr[n]=0x3f800000u;
        else fail(v,PRVM_OPCODE,pc);
    }
    else fail(v,PRVM_OPCODE,pc);
    if(v->fault)return 0;
    v->pc=slot?pending:next;return 1;
#undef R
#undef RD
#undef WR
}
int portable_replay_vm_run(PortableReplayVM *v,uint32_t entry,uint32_t end,uint32_t budget)
{
    v->pc=entry;v->pr=end;v->delayed=0;
    return portable_replay_vm_continue(v,end,budget);
}
int portable_replay_vm_continue(PortableReplayVM *v,uint32_t end,uint32_t budget)
{
    if(v->fault && v->fault!=PRVM_BUDGET)return 0;
    v->fault=0;
    v->fault_address=v->fault_pc=v->fault_opcode=0;
    for(uint32_t used=0;used<budget;used++) {
        if(v->pc==end && !v->delayed){v->fault_opcode=0;return 1;}
        if(v->hook && !v->delayed){int handled=v->hook(v,v->context);if(handled<0)return fail(v,PRVM_HOOK,v->pc);if(handled)continue;}
        if(!step(v))return 0;
    }
    if(v->pc==end && !v->delayed){v->fault_opcode=0;return 1;}
    return fail(v,PRVM_BUDGET,v->pc);
}
const char *portable_replay_vm_fault_name(unsigned f)
{
    static const char *const names[]={"none","unmapped/protected memory","unaligned access","unsupported opcode","illegal delay slot","instruction budget","unsupported FPU mode","service hook failed"};
    return f<sizeof names/sizeof *names?names[f]:"unknown";
}
