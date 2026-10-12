#ifndef HELIWRECK_H
#define HELIWRECK_H
#include "charanim.h"
#include "helidestruct.h"
/* Asset bank is shared by all aircraft, not allocated per mission child. */
int heliwreck_load(Chars *,const char *);
void heliwreck_draw(Chars *,const HeliDestruction *,const float root[16]);
#endif
