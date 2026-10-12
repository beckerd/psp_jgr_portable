#include "portable_replay_arm.h"
#include <string.h>
static int fail(PortableReplayArm*a,unsigned fault,uint32_t address)
{if(!a->fault){a->fault=fault;a->address=address;}return 0;}
static unsigned char *memory(PortableReplayArm*a,uint32_t p,unsigned n)
{
    if((n!=1 && n!=4) || (p&(n-1))){fail(a,2,p);return NULL;}
    if(p<a->ram_bytes && n<=a->ram_bytes-p)return a->ram+p;
    if(p>=0x800000 && p-0x800000<a->register_bytes && n<=a->register_bytes-(p-0x800000))return a->registers+(p-0x800000);
    fail(a,1,p);return NULL;
}
static uint32_t rd(PortableReplayArm*a,uint32_t p,unsigned n)
{unsigned shift=n==4?(p&3)*8:0;unsigned char*b=memory(a,n==4?p&~3u:p,n);uint32_t v=0;if(b)for(unsigned i=0;i<n;i++)v|=(uint32_t)b[i]<<(8*i);return shift?(v>>shift)|(v<<(32-shift)):v;}
static void wr(PortableReplayArm*a,uint32_t p,unsigned n,uint32_t v)
{unsigned char*b=memory(a,n==4?p&~3u:p,n);if(b)for(unsigned i=0;i<n;i++)b[i]=(unsigned char)(v>>(i*8));}
static uint32_t reg(PortableReplayArm*a,unsigned r){return r==15?a->pc+8:a->r[r];}
static uint32_t rotate(uint32_t a,unsigned n){return n?(a>>n)|(a<<(32-n)):a;}
static int status(PortableReplayArm*a,uint32_t value)
{
    unsigned before=a->cpsr&31,after=value&31;
    if(after!=before){if((before!=0x13 && before!=0x11) || (after!=0x13 && after!=0x11))return fail(a,3,a->pc);
        for(unsigned i=0;i<7;i++){uint32_t x=a->r[8+i];a->r[8+i]=a->bank[i];a->bank[i]=x;}
    }a->cpsr=value;return 1;
}
static int condition(PortableReplayArm*a,unsigned c)
{
    unsigned n=a->cpsr>>31,z=(a->cpsr>>30)&1,carry=(a->cpsr>>29)&1,v=(a->cpsr>>28)&1;
    switch(c){case 0:return z;case 1:return !z;case 2:return carry;case 3:return !carry;
    case 4:return n;case 5:return !n;case 6:return v;case 7:return !v;
    case 8:return carry&&!z;case 9:return !carry||z;case 10:return n==v;case 11:return n!=v;
    case 12:return !z&&n==v;case 13:return z||n!=v;case 14:return 1;default:return 0;}
}
static uint32_t shift(uint32_t x,unsigned type,unsigned count,int by_register,unsigned *carry)
{
    if(!count){if(by_register)return x;if(type==0)return x;if(type==3){uint32_t y=(*carry<<31)|(x>>1);*carry=x&1;return y;}count=32;}
    if(type==0){*carry=count<=32?(x>>(32-count))&1:0;return count<32?x<<count:0;}
    if(type==1){*carry=count<=32?(x>>(count-1))&1:0;return count<32?x>>count:0;}
    if(type==2){*carry=count<32?(x>>(count-1))&1:x>>31;return count<32?(x>>count)|((x>>31)?(~0u<<(32-count)):0):(x>>31?~0u:0);}
    count&=31;if(!count){*carry=x>>31;return x;}*carry=(x>>(count-1))&1;return rotate(x,count);
}
static uint32_t operand(PortableReplayArm*a,uint32_t w,unsigned *carry)
{
    if(w&(1u<<25)){unsigned n=((w>>8)&15)*2;uint32_t x=rotate(w&255,n);if(n)*carry=x>>31;return x;}
    unsigned count=(w>>7)&31,byreg=(w>>4)&1;
    if(byreg){if(w&128){fail(a,3,a->pc);return 0;}count=reg(a,(w>>8)&15)&255;}
    return shift(reg(a,w&15),(w>>5)&3,count,byreg,carry);
}
static void flags(PortableReplayArm*a,uint32_t x,unsigned carry,unsigned overflow)
{a->cpsr=(a->cpsr&0x0fffffffu)|(x&0x80000000u)|(!x?0x40000000u:0)|(carry<<29)|(overflow<<28);}
void portable_replay_arm_init(PortableReplayArm*a,void*ram,uint32_t n,void*regs,uint32_t regs_n)
{memset(a,0,sizeof *a);a->ram=ram;a->ram_bytes=n;a->registers=regs;a->register_bytes=regs_n;a->cpsr=0xd3;}
int portable_replay_arm_run(PortableReplayArm*a,uint32_t end,uint32_t budget)
{
    a->fault=0;
    for(uint32_t used=0;used<budget;used++){
        a->pc=a->r[15];if(a->pc&3)return fail(a,2,a->pc);if(a->pc==end && used)return 1;
        uint32_t w=rd(a,a->pc,4);a->opcode=w;if(a->fault)return 0;
        a->instructions++;a->r[15]=a->pc+4;
        if((w>>28)==15)return fail(a,3,a->pc);
        if(!condition(a,w>>28))continue;
        unsigned n=(w>>16)&15,d=(w>>12)&15,s=(w>>20)&1;
        if((w&0x0fbf0fffu)==0x010f0000u){if(w&(1u<<22))return fail(a,3,a->pc);a->r[d]=a->cpsr;}
        else if((w&0x0db0f000u)==0x0120f000u){
            unsigned c=(a->cpsr>>29)&1;uint32_t value=operand(a,w,&c),mask=0;
            if(w&(1u<<22))return fail(a,3,a->pc);
            if(w&(1u<<16))mask|=255;
            if(w&(1u<<19))mask|=0xff000000u;
            if(!status(a,(a->cpsr&~mask)|(value&mask)))return 0;
        }
        else if((w&0x0e000000u)==0x0a000000u){
            int32_t disp=(int32_t)(w<<8)>>6;
            if(w&(1u<<24))a->r[14]=a->pc+4;
            a->r[15]=a->pc+8+(uint32_t)disp;
        }
        else if((w&0x0fc000f0u)==0x00000090u){
            uint32_t x=reg(a,w&15)*reg(a,(w>>8)&15);if(w&(1u<<21))x+=reg(a,d);
            a->r[n]=x;if(s)flags(a,x,(a->cpsr>>29)&1,(a->cpsr>>28)&1);
        }
        else if((w&0x0e000000u)==0x08000000u){
            unsigned count=0;for(unsigned i=0;i<16;i++)count+=(w>>i)&1;
            if(!count || ((w&(1u<<22)) && (!s || !(w&0x8000) || (a->cpsr&31)!=0x11)))return fail(a,3,a->pc);
            uint32_t base=reg(a,n),p=base;
            int up=(w&(1u<<23))!=0,pre=(w&(1u<<24))!=0;
            if(!up)p-=count*4;
            if(pre==up)p+=4;
            for(unsigned i=0;i<16;i++)if(w&(1u<<i)){if(s)a->r[i]=rd(a,p,4);else wr(a,p,4,reg(a,i));p+=4;}
            if(w&(1u<<21))a->r[n]=up?base+count*4:base-count*4;
            a->r[15]&=~3u;
            if((w&(1u<<22)) && !status(a,a->spsr_fiq))return 0;
        }
        else if((w&0x0c000000u)==0x04000000u){
            uint32_t base=reg(a,n),off=w&4095;unsigned c=(a->cpsr>>29)&1;
            if(w&(1u<<25)){if(w&16)return fail(a,3,a->pc);off=shift(reg(a,w&15),(w>>5)&3,(w>>7)&31,0,&c);}
            uint32_t changed=(w&(1u<<23))?base+off:base-off;
            uint32_t p=(w&(1u<<24))?changed:base;unsigned size=(w&(1u<<22))?1:4;
            if(s)a->r[d]=rd(a,p,size);else wr(a,p,size,reg(a,d));
            if(!(w&(1u<<24)) || (w&(1u<<21)))a->r[n]=changed;
            a->r[15]&=~3u;
        }
        else if((w&0x0c000000u)==0){
            unsigned carry=(a->cpsr>>29)&1,overflow=(a->cpsr>>28)&1,op=(w>>21)&15;
            uint32_t x=reg(a,n),y=operand(a,w,&carry),z=0;uint64_t wide;
            switch(op){case 0:case 8:z=x&y;break;case 1:case 9:z=x^y;break;
            case 2:case 10:z=x-y;carry=x>=y;overflow=((x^y)&(x^z))>>31;break;
            case 3:z=y-x;carry=y>=x;overflow=((y^x)&(y^z))>>31;break;
            case 4:case 11:wide=(uint64_t)x+y;z=(uint32_t)wide;carry=wide>>32;overflow=(~(x^y)&(x^z))>>31;break;
            case 5:wide=(uint64_t)x+y+((a->cpsr>>29)&1);z=(uint32_t)wide;carry=wide>>32;overflow=(~(x^y)&(x^z))>>31;break;
            case 6:wide=(uint64_t)y+1-((a->cpsr>>29)&1);z=x-(uint32_t)wide;carry=(uint64_t)x>=wide;overflow=((x^y)&(x^z))>>31;break;
            case 7:wide=(uint64_t)x+1-((a->cpsr>>29)&1);z=y-(uint32_t)wide;carry=(uint64_t)y>=wide;overflow=((y^x)&(y^z))>>31;break;
            case 12:z=x|y;break;case 13:z=y;break;case 14:z=x&~y;break;case 15:z=~y;break;}
            if(op<8 || op>11){if(d==15 && s)return fail(a,3,a->pc);a->r[d]=d==15?z&~3u:z;}
            if(s || (op>=8 && op<=11))flags(a,z,carry,overflow);
        } else return fail(a,3,a->pc);
        if(a->fault)return 0;
    }
    return fail(a,4,a->r[15]);
}

int portable_replay_arm_fiq(PortableReplayArm*a,uint32_t budget)
{
    if(a->cpsr&0x40)return 1;
    uint32_t pc=a->r[15];a->spsr_fiq=a->cpsr;
    if(!status(a,(a->cpsr&~31u)|0x11|0xc0))return 0;
    a->r[14]=pc+4;a->r[15]=0x1c;
    return portable_replay_arm_run(a,pc,budget);
}
