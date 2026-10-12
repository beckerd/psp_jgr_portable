#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ctrl.h"
static unsigned char *readall(const char *path, int *len)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return 0;
    int n = sceIoLseek(fd, 0, PSP_SEEK_END); sceIoLseek(fd, 0, PSP_SEEK_SET);
    if(n<8){sceIoClose(fd);return NULL;}
    unsigned char *b = malloc((unsigned)n); if (!b) { sceIoClose(fd); return 0; }
    int got=sceIoRead(fd,b,n);sceIoClose(fd);
    if(got!=n){free(b);return NULL;} *len=n;return b;
}
static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static const unsigned char *load_sub(CtrlSub *c, const unsigned char *p, const unsigned char *end)
{
    memset(c, 0, sizeof *c);
    if ((size_t)(end-p)<4) return NULL;
    unsigned ntab = rd32(p); p += 4; if (ntab > 16) return NULL;
    for (unsigned t = 0; t < ntab; t++) {
        if((size_t)(end-p)<4)return NULL;
        unsigned n = rd32(p); p += 4;
        if(n>64 || n>(size_t)(end-p)/12)return NULL;
        c->segs[t] = (CtrlSeg *)p; c->nseg[t] = n; p += n * 12; c->ntab = t + 1;
    }
    if ((size_t)(end-p)<4) return NULL;
    unsigned ns = rd32(p); p += 4; if (ns > 16) return NULL;
    for (unsigned s = 0; s < ns; s++) {
        if((size_t)(end-p)<36)return NULL;
        CtrlStream *st = &c->streams[s];
        st->chr = (int)rd32(p); memcpy(&st->speed, p + 4, 4); memcpy(&st->x, p + 8, 4); memcpy(&st->y, p + 12, 4); memcpy(&st->z, p + 16, 4);
        st->w5 = rd32(p + 20); st->yaw = (int)rd32(p + 24); st->w7 = rd32(p + 28);
        unsigned n=rd32(p+32);p+=36;
        if(!n || n>(size_t)(end-p)/4 || !(p[(n-1)*4]&0x80))return NULL;
        st->nrec=n;st->rec=p;p+=n*4;c->nstream=s+1;
    }
    for(int t=0;t<c->ntab;t++)for(int s=0;s<c->nseg[t];s++)
        if(c->segs[t][s].stream<0 || c->segs[t][s].stream>=c->nstream)return NULL;
    if ((size_t)(end-p)<4) return NULL;
    unsigned nt = rd32(p); p += 4; if (nt > 64) return NULL;
    for (unsigned i = 0; i < nt; i++) {
        if((size_t)(end-p)<20)return NULL;
        TrialEntry *e = &c->trial[i];
        e->type = rd32(p); e->p1 = rd32(p + 4); e->p2 = rd32(p + 8); e->na = rd32(p + 12); e->nb = rd32(p + 16); p += 20;
        size_t quads=(size_t)(end-p)/144;
        if(e->type>9 || e->na>quads || e->nb>quads-e->na)return NULL;
        e->quads = (const float *)p; p += (e->na + e->nb) * 144; c->ntrial = i + 1;
    }
    return p;
}
int ctrl_load(CtrlSet *c, const char *path)
{
    int len = 0; unsigned char *d = readall(path, &len); if (!d) return -1;
    if (len < 8 || memcmp(d, "CTL2", 4)) { free(d); return -2; }
    unsigned n=rd32(d+4);if(!n || n>8){free(d);return -2;}
    /* Streams/trials borrow this allocation. Validate a replacement before
       freeing the old bank; callers retire live playback before swapping. */
    CtrlSet *next=calloc(1,sizeof *next);if(!next){free(d);return -1;}
    next->data=d;const unsigned char *p=d+8,*end=d+len;
    for(unsigned k=0;k<n;k++) {
        p=load_sub(next->sub+k,p,end);
        if(!p){ctrl_free(next);free(next);return -2;}
        next->nsub=k+1;
    }
    if(p!=end){ctrl_free(next);free(next);return -2;}
    ctrl_free(c);*c=*next;free(next);return 0;
}
void ctrl_free(CtrlSet *c) { free(c->data); memset(c, 0, sizeof *c); }
const CtrlSub *ctrl_sub(const CtrlSet *c,int index)
{ return c && c->data && index>=0 && index<c->nsub ? c->sub+index:NULL; }
void ctrl_play_start(CtrlPlay *p, const CtrlStream *s)
{
    memset(p, 0, sizeof *p); p->s = s; if (!s || s->nrec <= 0) return;
    memcpy(p->cur, s->rec, 4); p->pos = 0; p->repeat = p->cur[0] & 0x7f; p->active = 1;   /* FUN_8c0277d8: +0x70 = stream + 0x20, +0x78 = *rec & 0x7f */
}
int ctrl_play_tick(CtrlPlay *p)
{
    if (!p->active || p->ended) return 0;
    p->prev = p->cur[1];
    if (p->repeat == 0) {                                   /* next record */
        if (p->pos + 1 < p->s->nrec) { p->pos++; memcpy(p->cur, p->s->rec + p->pos * 4, 4); p->repeat = p->cur[0] & 0x7f; }
    } else p->repeat--;
    if (p->cur[0] & 0x80) { p->ended = 1; p->active = 0; return 0; }   /* end record: completion flag, task killed */
    return 1;
}
void ctrl_play_input(const CtrlPlay *p, float *ax, float *ay, unsigned *buttons)
{
    *ax = *ay = 0; *buttons = 0; if (!p->active) return;
    float mag = p->cur[3] / 255.f;                                     /* FUN_8c0277a0 uses (m/255)^2 for the speed target; the stick vector here is unit-scaled */
    float ang = (float)((signed char)p->cur[2] * 256) * (2.f * 3.14159265f / 65536.f);   /* direction byte = BAM >> 8 (njArcTan2(x, -y)); no negative signed shift */
    *ax = -sinf(ang) * mag; *ay = -cosf(ang) * mag;   /* FUN_8c06c048: target yaw = camera yaw - stick angle (DC heading = (-sin, -cos)), i.e. mirrored against the live atan2(ax,-ay) mapping */
    *buttons = p->cur[1];
}
