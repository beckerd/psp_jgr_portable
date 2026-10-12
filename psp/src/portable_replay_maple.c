#include "portable_replay_maple.h"
#include <string.h>
static uint32_t swap(uint32_t x)
{return (x<<24)|((x&0xff00)<<8)|((x>>8)&0xff00)|(x>>24);}
static int ram(uint32_t a,unsigned bytes)
{a&=0x1fffffffu;return !(a&3) && a>=0x0c000000 && a<0x0d000000 && bytes<=0x0d000000-a;}
int portable_replay_maple_dma(PortableReplayVM*v,uint32_t addr,unsigned native,uint16_t buttons)
{
    for(unsigned commands=0;commands<256;commands++){
        uint32_t header,dest,frame;
        if(!ram(addr,4) || !portable_replay_vm_read(v,addr,4,&header))return 0;
        unsigned pattern=(header>>8)&7,words=(header&255)+1,bus=(header>>16)&3;
        if(pattern==0){
            if(!ram(addr,(words+2)*4) || !portable_replay_vm_read(v,addr+4,4,&dest) || !portable_replay_vm_read(v,addr+8,4,&frame))return 0;
            dest&=0x1fffffe0u;if(!native)frame=swap(frame);
            unsigned cmd=frame&255,recipient=(frame>>8)&255,sender=(frame>>16)&255;
            if((frame>>24)+1!=words)return 0;
            unsigned char response[116]={0};unsigned length=4;
            if(bus || recipient!=0x20)memset(response,255,4);
            else {
                unsigned code=0xfd;
                if(cmd==1){
                    code=5;length=116;
                    response[7]=1; /* input-device function mask */
                    response[8]=0;response[9]=0x0f;response[10]=6;response[11]=0xfe;
                    response[20]=255;
                    memset(response+22,' ',90);
                    memcpy(response+22,"Portable setup controller",25);
                    memcpy(response+52,"Independent virtual input device",32);
                    response[112]=10;response[114]=20;
                }else if(cmd==9){
                    if(words!=2)return 0;
                    uint32_t function;if(!portable_replay_vm_read(v,addr+12,4,&function))return 0;
                    if(!native)function=swap(function);
                    if(function==0x01000000){
                        code=8;length=16;response[7]=1;
                        response[8]=(unsigned char)buttons;response[9]=(unsigned char)(buttons>>8);
                        memset(response+12,128,4);
                    }else code=0xfe;
                }else if(cmd==3 || cmd==4)code=7;
                response[0]=(unsigned char)code;response[1]=(unsigned char)sender;
                response[2]=(unsigned char)recipient;response[3]=(unsigned char)(length/4-1);
            }
            if(!ram(dest,length))return 0;
            for(unsigned j=0;j<length;j+=4){
                uint32_t x=(uint32_t)response[j]|(uint32_t)response[j+1]<<8|(uint32_t)response[j+2]<<16|(uint32_t)response[j+3]<<24;
                if(!native)x=swap(x);
                if(!portable_replay_vm_write(v,dest+j,4,x))return 0;
            }
            addr+=(words+2)*4;
        }else if(pattern==2 || pattern==3 || pattern==4 || pattern==7)addr+=4;
        else return 0;
        if(header&0x80000000u)return 1;
    }
    return 0;
}
