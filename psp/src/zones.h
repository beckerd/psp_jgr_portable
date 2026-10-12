/* Stage zones (sectors): zone volumes from FUN_8c0dcd3c's list and the neighbour table of FUN_8c0c2cf8 ('ZON1' from tools/jgs_zones.py).
   Objects of the current zone and its neighbours are active (FUN_8c030934 streams sectors on zone change). */
#ifndef JGR_ZONES_H
#define JGR_ZONES_H
typedef struct { int zone, aux; float bb[6]; unsigned na, nb; const float *qa, *qb; } ZoneRec;
typedef struct { unsigned char *data; int nzones, nrec; unsigned counts[6]; ZoneRec rec[128]; const int *nb[256]; int nnb[256]; } Zones;
int  zones_load(Zones *z, const char *path);
void zones_free(Zones *z);
int  zone_of(const Zones *z, float x, float y, float z_);      /* global zone index or -1 */
int  zone_neighbour(const Zones *z, int a, int b);
int  zone_area(const Zones *z, int zone);                       /* area 0..5 owning a global zone index (counts[] are per area) */              /* FUN_8c0c2cf8: a == b or b in a's list */
#endif
