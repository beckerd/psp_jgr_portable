#include "portable_replay_gdrom.h"
#include <string.h>
void portable_replay_gdrom_init(PortableReplayGdrom*g,JgrDisc*d,void*ram,uint32_t bytes)
{memset(g,0,sizeof *g);g->disc=d;g->ram=ram;g->ram_bytes=bytes;g->next_request=1;}
static int put(PortableReplayVM*v,uint32_t a,uint32_t x)
{return portable_replay_vm_write(v,a,4,x);}
static int execute(PortableReplayGdrom*g,PortableReplayVM*v)
{
    uint32_t *p=g->parameter;
    if(g->command==0x28) {
        /* Installer drive identity, not a copied BIOS identification string.
         * The final byte is the documented drive-interface revision. */
        const char version[]="Portable installer drive ";
        for(unsigned i=0;i<sizeof version;i++)if(!portable_replay_vm_write(v,p[0]+i,1,
            i==sizeof version-1?2:(unsigned char)version[i]))return 0;
    } else if(g->command==0x18){} /* initialize existing supported drive */
    else if(g->command==0x10 || g->command==0x11) {
        uint32_t addr=p[2]&0x1fffffffu,count=p[1],fad=p[0];
        if(fad<150 || !count || count>UINT32_MAX/2048 || addr<0x0c000000 ||
           addr-0x0c000000>g->ram_bytes || count*2048>g->ram_bytes-(addr-0x0c000000))return 0;
        unsigned char*dest=g->ram+(addr-0x0c000000);
        for(uint32_t i=0;i<count;){unsigned n=count-i;if(n>32)n=32;
            if(jgr_disc_sectors(g->disc,fad-150+i,n,dest+i*2048,n*2048)<0)return 0;
            g->sectors_read+=n;i+=n;
        }
        g->result[2]=count*2048;
    } else if(g->command==0x13) {
        /* The supported high-density area has one data track, number 3.
         * Sector positions and length always come from the user's GDI. */
        uint32_t first,count;
        if(p[0]!=1 || jgr_disc_sector_bounds(g->disc,&first,&count)<0)return 0;
        for(unsigned i=0;i<102;i++)if(!put(v,p[1]+i*4,UINT32_MAX))return 0;
        if(!put(v,p[1]+2*4,0x41000000u|(first+150)) ||
           !put(v,p[1]+99*4,0x41030000u) || !put(v,p[1]+100*4,0x41030000u) ||
           !put(v,p[1]+101*4,0x41000000u|(first+count+150)))return 0;
        g->result[2]=408;
    } else return 0;
    g->status=2;return 1;
}
int portable_replay_gdrom_service(PortableReplayGdrom*g,PortableReplayVM*v)
{
    if(v->r[6]!=0)return 0;
    switch(v->r[7]) {
    case 3:case 9:g->status=0;g->next_request=1;g->request=0;v->r[0]=0;break;
    case 0:
        if(g->status){v->r[0]=0;break;}
        g->command=v->r[4];
        for(unsigned i=0;i<4;i++){
            g->parameter[i]=0;
            if(v->r[5] && !portable_replay_vm_read(v,v->r[5]+i*4,4,g->parameter+i))return 0;
        }
        memset(g->result,0,sizeof g->result);g->request=g->next_request++;
        if(!g->next_request)g->next_request=1;
        g->status=1;v->r[0]=g->request;g->commands++;break;
    case 1:
        for(unsigned i=0;i<4;i++)if(!put(v,v->r[5]+i*4,g->result[i]))return 0;
        v->r[0]=v->r[4]==g->request?g->status:0;
        if(v->r[0]==2)g->status=0;
        break;
    case 2:if(g->status==1 && !execute(g,v))return 0;break;
    case 4:if(!put(v,v->r[4],1) || !put(v,v->r[4]+4,0x80))return 0;v->r[0]=0;break;
    case 10:{
        uint32_t mode,bytes;
        if(!portable_replay_vm_read(v,v->r[4],4,&mode) ||
           !portable_replay_vm_read(v,v->r[4]+12,4,&bytes) || mode!=0 || bytes!=2048)return 0;
        v->r[0]=0;break;
    }
    default:return 0;
    }
    return !v->fault;
}
