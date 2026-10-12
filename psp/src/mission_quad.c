#include <math.h>
#include "mission_quad.h"

static float dot(const float *a, const float *b)
{ return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
/* Ninja helpers use FMAC, with Y first, then X and Z. */
static float ninja_dot(const float *a, const float *b)
{ return fmaf(a[2], b[2], fmaf(a[0], b[0], a[1]*b[1])); }
static void cross(float *v, const float *a, const float *b)
{
    v[0] = fmaf(-a[2], b[1], a[1]*b[2]);
    v[1] = fmaf(a[0], -b[2], a[2]*b[0]);
    v[2] = fmaf(a[1], -b[0], a[0]*b[1]);
}
void mission_quad_prepare(MissionQuad *q)
{
    for (int j = 0; j < 3; j++)
        q->center[j] = (q->vertex[0][j] + q->vertex[1][j] + q->vertex[2][j] + q->vertex[3][j]) / 4.f;
    float maximum = 0.f;
    for (int i = 0; i < 4; i++) {
        float d[3]; for (int j = 0; j < 3; j++) d[j] = q->vertex[i][j] - q->center[j];
        float r = dot(d,d); if (i == 0 || maximum < r) maximum = r;
    }
    q->radius_sq = maximum * 1.2f; /* literal 8c0c2e88 */
}
int mission_quad_near(const MissionQuad *q, const float s[6])
{
    float d[3]; for (int j = 0; j < 3; j++) d[j] = s[j] - q->center[j];
    if (dot(d,d) > q->radius_sq) return 0;
    for (int j = 0; j < 3; j++) d[j] += s[3+j];
    return dot(d,d) <= q->radius_sq;
}
unsigned mission_quad_cross(const MissionQuad *q, const float s[6])
{
    const float *n = q->normal, *d = s+3;
    if (d[0] == 0.f && d[1] == 0.f && d[2] == 0.f) return 0;
    /* 272304 -> 2755fc: near-parallel lines project their START onto the
       plane (272518). Replacing this with an epsilon/no-hit is not equivalent. */
    float nd = ninja_dot(n,d), nn = ninja_dot(n,n), dd = ninja_dot(d,d);
    int parallel = fabsf(nd / (sqrtf(dd)*sqrtf(nn))) < .025f;
    float plane = (-n[0]*q->vertex[0][0] - n[1]*q->vertex[0][1]) - n[2]*q->vertex[0][2];
    float t = -(ninja_dot(n,s) + plane) / (parallel ? nn : nd);
    float hit[3], a[3], b[3];
    for (int j = 0; j < 3; j++) {
        hit[j] = fmaf(t, parallel ? n[j] : d[j], s[j]);
        if (!isfinite(hit[j])) return 0; /* malformed source guard */
        a[j] = hit[j] - s[j]; b[j] = d[j] - a[j];
    }
    if (dot(a,b) < 0.f) return 0;
    /* 0c3100..0c3242: four adjacent cross-products, strict sign tests.
       Points exactly on an edge/corner fail; do not add a containment epsilon. */
    static const int order[4] = {0,1,3,2};
    float v[4][3], c[4][3];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) v[i][j] = q->vertex[order[i]][j] - hit[j];
    for (int i = 0; i < 4; i++) cross(c[i],v[i],v[(i+1)%4]);
    int positive = 1, negative = 1;
    for (int i = 0; i < 4; i++) { float f = dot(c[i],c[(i+1)%4]); positive &= f > 0.f; negative &= f < 0.f; }
    if (!(positive || negative)) return 0;
    return dot(n,d) > 0.f ? 0x10000u : 0x20000u;
}
