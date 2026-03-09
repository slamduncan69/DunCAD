/*
 * ts_vec.h — 3D vector operations
 *
 * Fixed-size vec3 (3 doubles). No heap allocation.
 * Every operation is pure — suitable for SIMD and GPU.
 *
 * GPU mapping: each vec3 op maps to 3 parallel scalar ops.
 * On OpenCL, use double3 type for native vectorization.
 *
 * OpenSCAD equivalents:
 *   norm(v) = ts_vec3_norm
 *   cross(a,b) = ts_vec3_cross
 *   + vector arithmetic used implicitly everywhere
 */
#ifndef TS_VEC_H
#define TS_VEC_H

#include <math.h>

typedef struct {
    double v[3];
} ts_vec3;

/* --- Constructors --- */

static inline ts_vec3 ts_vec3_make(double x, double y, double z) {
    return (ts_vec3){{ x, y, z }};
}

static inline ts_vec3 ts_vec3_zero(void) {
    return (ts_vec3){{ 0.0, 0.0, 0.0 }};
}

static inline ts_vec3 ts_vec3_one(void) {
    return (ts_vec3){{ 1.0, 1.0, 1.0 }};
}

/* --- Arithmetic (component-wise, trivially parallel) --- */

/* GPU: 3 parallel adds */
static inline ts_vec3 ts_vec3_add(ts_vec3 a, ts_vec3 b) {
    return (ts_vec3){{ a.v[0]+b.v[0], a.v[1]+b.v[1], a.v[2]+b.v[2] }};
}

/* GPU: 3 parallel subs */
static inline ts_vec3 ts_vec3_sub(ts_vec3 a, ts_vec3 b) {
    return (ts_vec3){{ a.v[0]-b.v[0], a.v[1]-b.v[1], a.v[2]-b.v[2] }};
}

/* GPU: 3 parallel muls */
static inline ts_vec3 ts_vec3_mul(ts_vec3 a, ts_vec3 b) {
    return (ts_vec3){{ a.v[0]*b.v[0], a.v[1]*b.v[1], a.v[2]*b.v[2] }};
}

/* GPU: 3 parallel divs */
static inline ts_vec3 ts_vec3_div(ts_vec3 a, ts_vec3 b) {
    return (ts_vec3){{ a.v[0]/b.v[0], a.v[1]/b.v[1], a.v[2]/b.v[2] }};
}

/* Uniform scale: GPU: 3 parallel muls */
static inline ts_vec3 ts_vec3_scale(ts_vec3 a, double s) {
    return (ts_vec3){{ a.v[0]*s, a.v[1]*s, a.v[2]*s }};
}

/* Negate: GPU: 3 parallel negations */
static inline ts_vec3 ts_vec3_negate(ts_vec3 a) {
    return (ts_vec3){{ -a.v[0], -a.v[1], -a.v[2] }};
}

/* --- Dot product --- */
/* GPU: 3 parallel muls + 2 adds (reduction) */
/* Parallelism: REDUCIBLE — multiply is parallel, sum is reduction */
static inline double ts_vec3_dot(ts_vec3 a, ts_vec3 b) {
    return a.v[0]*b.v[0] + a.v[1]*b.v[1] + a.v[2]*b.v[2];
}

/* --- Cross product --- */
/* OpenSCAD: cross([a,b,c], [d,e,f]) */
/* GPU: 6 muls + 3 subs, all independent per-component */
static inline ts_vec3 ts_vec3_cross(ts_vec3 a, ts_vec3 b) {
    return (ts_vec3){{
        a.v[1]*b.v[2] - a.v[2]*b.v[1],
        a.v[2]*b.v[0] - a.v[0]*b.v[2],
        a.v[0]*b.v[1] - a.v[1]*b.v[0]
    }};
}

/* --- Magnitude (norm) --- */
/* OpenSCAD: norm(v) */
/* GPU: dot + sqrt (reduction + scalar) */
static inline double ts_vec3_norm(ts_vec3 a) {
    return sqrt(ts_vec3_dot(a, a));
}

/* Squared magnitude — avoids sqrt, useful for comparisons */
static inline double ts_vec3_norm_sq(ts_vec3 a) {
    return ts_vec3_dot(a, a);
}

/* --- Normalize --- */
/* GPU: norm + 3 divides (or rsqrt + 3 muls for fast path) */
static inline ts_vec3 ts_vec3_normalize(ts_vec3 a) {
    double len = ts_vec3_norm(a);
    if (len < 1e-15) return ts_vec3_zero();
    return ts_vec3_scale(a, 1.0 / len);
}

/* --- Distance --- */
static inline double ts_vec3_distance(ts_vec3 a, ts_vec3 b) {
    return ts_vec3_norm(ts_vec3_sub(a, b));
}

/* --- Linear interpolation --- */
/* GPU: 3 parallel lerps */
static inline ts_vec3 ts_vec3_lerp(ts_vec3 a, ts_vec3 b, double t) {
    return (ts_vec3){{
        a.v[0] + t * (b.v[0] - a.v[0]),
        a.v[1] + t * (b.v[1] - a.v[1]),
        a.v[2] + t * (b.v[2] - a.v[2])
    }};
}

/* --- Component-wise min/max --- */
/* GPU: 3 parallel fmin/fmax */
static inline ts_vec3 ts_vec3_min(ts_vec3 a, ts_vec3 b) {
    return (ts_vec3){{
        fmin(a.v[0], b.v[0]),
        fmin(a.v[1], b.v[1]),
        fmin(a.v[2], b.v[2])
    }};
}

static inline ts_vec3 ts_vec3_max(ts_vec3 a, ts_vec3 b) {
    return (ts_vec3){{
        fmax(a.v[0], b.v[0]),
        fmax(a.v[1], b.v[1]),
        fmax(a.v[2], b.v[2])
    }};
}

/* --- Reflect --- */
/* Reflect vector v around normal n (assumes n is normalized) */
/* GPU: dot (reduction) + scale + sub (parallel) */
static inline ts_vec3 ts_vec3_reflect(ts_vec3 v, ts_vec3 n) {
    double d = 2.0 * ts_vec3_dot(v, n);
    return ts_vec3_sub(v, ts_vec3_scale(n, d));
}

/* --- Equality (with epsilon) --- */
static inline int ts_vec3_near(ts_vec3 a, ts_vec3 b, double eps) {
    return fabs(a.v[0]-b.v[0]) <= eps &&
           fabs(a.v[1]-b.v[1]) <= eps &&
           fabs(a.v[2]-b.v[2]) <= eps;
}

#endif /* TS_VEC_H */
