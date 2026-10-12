#ifndef V3_H
#define V3_H

#include <math.h>

typedef struct { float x, y, z; } v3;

static inline v3 v3make(float x, float y, float z) { v3 r = { x, y, z }; return r; }
static inline v3 v3add(v3 a, v3 b) { return v3make(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3 v3sub(v3 a, v3 b) { return v3make(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3 v3scale(v3 a, float s) { return v3make(a.x * s, a.y * s, a.z * s); }
static inline float v3dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3len(v3 a) { return sqrtf(v3dot(a, a)); }

static inline v3 v3norm(v3 a)
{
    float l = v3len(a);
    if (l < 1e-6f)
        return v3make(0.0f, 1.0f, 0.0f);
    return v3scale(a, 1.0f / l);
}

static inline v3 v3lerp(v3 a, v3 b, float t)
{
    return v3make(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t);
}

#endif
