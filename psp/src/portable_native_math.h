#ifndef JGR_PORTABLE_NATIVE_MATH_H
#define JGR_PORTABLE_NATIVE_MATH_H
#include "portable_disc.h"
int jgr_native_math_load(JgrDisc *); /* 1 on success, 0 on failure. */
void jgr_native_math_clear(void);
float jgr_native_math_power(float base,float exponent);
#endif
