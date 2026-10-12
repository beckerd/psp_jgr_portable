#include <pspkernel.h>
#include <stdlib.h>
#include <string.h>
#include "zones.h"
static int vol_inside(const float *q, float x, float y, float z)   /* FUN_8c0c7ff4: 6 planes {point, normal} */
{
    for (int i = 0; i < 6; i++) { const float *p = q + i * 3, *n = q + 18 + i * 3; if ((x - p[0]) * n[0] + (y - p[1]) * n[1] + (z - p[2]) * n[2] > 0.f) return 0; }   /* normals point outward (same as the trial volumes) */
    return 1;
}
int zones_load(Zones *z, const char *path)
{
    memset(z, 0, sizeof *z); SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777); if (fd < 0) return -1;
    int len = sceIoLseek(fd, 0, PSP_SEEK_END); sceIoLseek(fd, 0, PSP_SEEK_SET); z->data = malloc(len + 16); if (!z->data) { sceIoClose(fd); return -2; }
    sceIoRead(fd, z->data, len); sceIoClose(fd);
    if (len < 36 || memcmp(z->data, "ZON1", 4)) return -3;
    unsigned char *p = z->data + 4; z->nzones = *(int *)p; z->nrec = *(int *)(p + 4); memcpy(z->counts, p + 8, 24); p += 32;
    if (z->nzones > 256) z->nzones = 256; if (z->nrec > 128) z->nrec = 128;
    for (int i = 0; i < z->nrec; i++) { ZoneRec *r = &z->rec[i]; r->zone = *(int *)p; r->aux = *(int *)(p + 4); memcpy(r->bb, p + 8, 24); r->na = *(unsigned *)(p + 32); r->nb = *(unsigned *)(p + 36); p += 40; r->qa = (const float *)p; p += r->na * 144; r->qb = (const float *)p; p += r->nb * 144; }
    for (int i = 0; i < z->nzones; i++) { z->nnb[i] = *(int *)p; p += 4; z->nb[i] = (const int *)p; p += z->nnb[i] * 4; }
    return 0;
}
void zones_free(Zones *z) { free(z->data); memset(z, 0, sizeof *z); }
int zone_of(const Zones *z, float x, float y, float zz)
{
    for (int i = 0; i < z->nrec; i++) {
        const ZoneRec *r = &z->rec[i];
        if (x < r->bb[0] || y < r->bb[1] || zz < r->bb[2] || x > r->bb[3] || y > r->bb[4] || zz > r->bb[5]) continue;   /* FUN_8c0c8084 */
        unsigned k; for (k = 0; k < r->na; k++) if (vol_inside(r->qa + k * 36, x, y, zz)) break;
        if (k >= r->na) continue;
        for (k = 0; k < r->nb; k++) if (vol_inside(r->qb + k * 36, x, y, zz)) break;
        if (k < r->nb) continue;
        int base = 0; for (int a = 0; a < r->zone - 1 && a < 6; a++) base += (int)z->counts[a];   /* area (+0x28, 1-based) / index (+0x2c) -> global zone = sum(counts[:area-1]) + index (FUN_8c0c2c1c / camera FUN_8c0cf96c) */
        return base + r->aux;
    }
    return -1;
}
int zone_neighbour(const Zones *z, int a, int b)
{
    if (a < 0) return 0; if (a == b) return 1; if (a >= z->nzones) return 0;
    for (int i = 0; i < z->nnb[a]; i++) if (z->nb[a][i] == b) return 1;
    return 0;
}

int zone_area(const Zones *z, int zone) { unsigned base = 0; for (int a = 0; a < 6; a++) { if (zone >= 0 && (unsigned)zone < base + z->counts[a]) return a; base += z->counts[a]; } return 0; }
