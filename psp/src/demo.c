#include <pspkernel.h>
#include <stdlib.h>
#include <string.h>
#include "demo.h"
int demo_load(Demo *d, const char *path)
{
    memset(d, 0, sizeof *d); SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return -1;
    int len = sceIoLseek(fd, 0, PSP_SEEK_END); sceIoLseek(fd, 0, PSP_SEEK_SET); d->data = malloc(len + 16); if (!d->data) { sceIoClose(fd); return -2; }
    sceIoRead(fd, d->data, len); sceIoClose(fd);
    if (len < 8 || memcmp(d->data, "PDM1", 4)) return -3;
    int n = *(int *)(d->data + 4); unsigned char *p = d->data + 8;
    for (int i = 0; i < n && i < 4 && p + 36 <= d->data + len; i++) {
        DemoSkater *k = &d->sk[i]; int *h = (int *)p; float *f = (float *)p;
        k->s.chr = h[0]; k->s.speed = f[1]; k->s.x = f[2]; k->s.y = f[3]; k->s.z = f[4]; k->s.w5 = h[5]; k->s.yaw = h[6]; k->s.w7 = h[7];
        k->s.nrec = h[8]; k->s.rec = p + 36; p += 36 + k->s.nrec * 4;
        k->nev = *(int *)p; p += 4; k->ev = (const DemoEv *)p; p += k->nev * sizeof(DemoEv); d->n++;
    }
    return 0;
}
void demo_free(Demo *d) { free(d->data); memset(d, 0, sizeof *d); }
