#include "ge_submit.h"
#include "gfx.h"
#include <pspkernel.h>
#include <pspgu.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <limits.h>
#include "tex2d.h"
typedef struct { float u, v; unsigned color; float x, y, z; } FV;
static struct {int active,count;Tex2d texture;FV vertices[64];} batch;
static void sprite_run(const Tex2d *t,const FV *vertices,int count)
{
    FV *v=sceGuGetMemory(count*sizeof *v);
    for(int i=0;i<count;i++)v[i]=vertices[i];
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_4444,0,0,0);sceGuTexImage(0,t->w,t->h,t->w,t->px);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    ge_submit_aux(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,count,0,v);
    sceGuDisable(GU_TEXTURE_2D);
}
static void batch_flush(void)
{if(batch.count){sprite_run(&batch.texture,batch.vertices,batch.count);batch.count=0;}}
void tex2d_batch_begin(void){batch.active=ge_submit_aux_batched();}
void tex2d_batch_end(void){batch_flush();batch.active=0;}
int tex2d_load(Tex2dSet *s, const char *path)
{
    memset(s,0,sizeof *s);
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0777);if(fd<0)return -1;
    SceOff size=sceIoLseek(fd,0,PSP_SEEK_END);
    if(size<8 || size>INT_MAX-128 || sceIoLseek(fd,0,PSP_SEEK_SET)<0){sceIoClose(fd);return -3;}
    int len=(int)size;
    s->data=memalign(16,len+128);if(!s->data){sceIoClose(fd);return -2;}
    int received=0;
    while(received<len){int n=sceIoRead(fd,s->data+received,len-received);if(n<=0)break;received+=n;}
    sceIoClose(fd);
    if(received!=len || memcmp(s->data,"TEX1",4))goto invalid;
    unsigned n;memcpy(&n,s->data+4,4);if(!n || n>8)goto invalid;
    unsigned source[8],target[8],bytes[8],cursor=8,packed=8;
    for(unsigned i=0;i<n;i++) {
        if(cursor>(unsigned)len-4)goto invalid;
        unsigned short w,h;memcpy(&w,s->data+cursor,2);memcpy(&h,s->data+cursor+2,2);cursor+=4;
        if(!w || !h || (unsigned)w*h>((unsigned)len-cursor)/2)goto invalid;
        unsigned amount=(unsigned)w*h*2;
        /* Keep the legacy skip for the 1024-square boot logo. */
        if(w<=512 && h<=512) {
            unsigned j=s->n++;
            source[j]=cursor;bytes[j]=amount;
            if(packed<cursor)packed=cursor;
            target[j]=(packed+15)&~15u;packed=target[j]+amount;
            s->t[j]=(Tex2d){(unsigned short *)(s->data+target[j]),w,h};
        }
        cursor+=amount;
    }
    if(cursor!=(unsigned)len)goto invalid;
    /* A TEX1 file starts its first pixels at +12, despite an aligned
       allocation. The GE discards those low address bits, shifting 4444
       HUD glyphs by six pixels. Align each image, not only the bank.
       Move backwards in the same allocation: no second texture bank or
       extra draw-time copies, and no on-disk asset format change. */
    for(int i=s->n-1;i>=0;i--)memmove(s->data+target[i],s->data+source[i],bytes[i]);
    sceKernelDcacheWritebackAll();return 0;
invalid:
    free(s->data);memset(s,0,sizeof *s);return -3;
}

void tex2d_free(Tex2dSet *s) {gfx_wait_idle(); free(s->data); memset(s, 0, sizeof *s); }
static unsigned argb2abgr(unsigned c) { return (c & 0xff00ff00u) | ((c >> 16) & 0xff) | ((c & 0xff) << 16); }
void tex2d_draw_entry(const Tex2d *t, float x, float y, float w, float h, float pu, float pv, unsigned argb, float u0, float v0, float u1, float v1)
{
    if (!t || !t->px) return;
    const float SX = 480.f / 640.f, SY = 272.f / 480.f;
    float x0 = (x - w * pu) * SX, y0 = (y - h * pv) * SY, x1 = (x + w * (1.f - pu)) * SX, y1 = (y + h * (1.f - pv)) * SY;
    FV v[2];unsigned c=argb2abgr(argb);
    v[0].u = u0 * t->w; v[0].v = v0 * t->h; v[0].color = c; v[0].x = x0; v[0].y = y0; v[0].z = 0;
    v[1].u = u1 * t->w; v[1].v = v1 * t->h; v[1].color = c; v[1].x = x1; v[1].y = y1; v[1].z = 0;
    if(!batch.active){sprite_run(t,v,2);return;}
    if(batch.count && (batch.texture.px!=t->px || batch.texture.w!=t->w || batch.texture.h!=t->h || batch.count==64))batch_flush();
    batch.texture=*t;
    batch.vertices[batch.count++]=v[0];batch.vertices[batch.count++]=v[1];
}
