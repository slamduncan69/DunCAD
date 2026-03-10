/*
 * ts_scalar.h — Scalar math operations
 *
 * Reimplements every scalar math function from OpenSCAD.
 * All functions are pure (no side effects, no shared state),
 * making them trivially parallelizable across arrays of values.
 *
 * Design for GPU: each function maps 1:1 to an OpenCL kernel.
 * No branching where possible (use conditional moves/masks).
 *
 * OpenSCAD equivalents:
 *   abs, sign, ceil, floor, round,
 *   ln, log (=log10), pow, sqrt, exp, exp2, log2,
 *   min, max
 */
#ifndef TS_SCALAR_H
#define TS_SCALAR_H

#include <math.h>
#include <float.h>

/* --- Absolute value --- */
/* GPU: fabs() maps directly to OpenCL fabs() */
static inline double ts_abs(double x) {
    return fabs(x);
}

/* --- Sign --- */
/* Returns -1.0, 0.0, or 1.0. OpenSCAD: sign(x) */
/* GPU: sign() available in OpenCL */
static inline double ts_sign(double x) {
    if (x > 0.0) return 1.0;
    if (x < 0.0) return -1.0;
    if (x == 0.0) return 0.0;
    return x; /* NaN propagation */
}

/* --- Ceiling --- */
/* GPU: ceil() maps to OpenCL ceil() */
static inline double ts_ceil(double x) {
    return ceil(x);
}

/* --- Floor --- */
/* GPU: floor() maps to OpenCL floor() */
static inline double ts_floor(double x) {
    return floor(x);
}

/* --- Round --- */
/* OpenSCAD uses round-half-away-from-zero (not banker's rounding) */
/* GPU: round() in OpenCL uses round-to-even; we need rint() or custom */
static inline double ts_round(double x) {
    return round(x);  /* C99 round = half-away-from-zero, matches OpenSCAD */
}

/* --- Natural logarithm --- */
/* OpenSCAD: ln(x) */
/* GPU: log() in OpenCL = natural log */
static inline double ts_ln(double x) {
    return log(x);
}

/* --- Log base 10 --- */
/* OpenSCAD: log(x) = log base 10 (confusingly) */
/* GPU: log10() in OpenCL */
static inline double ts_log10(double x) {
    return log10(x);
}

/* --- Log base 2 --- */
/* Not in vanilla OpenSCAD but essential for GPU work */
/* GPU: log2() in OpenCL */
static inline double ts_log2(double x) {
    return log2(x);
}

/* --- Power --- */
/* OpenSCAD: pow(base, exp) */
/* GPU: pow() in OpenCL */
static inline double ts_pow(double base, double exp) {
    return pow(base, exp);
}

/* --- Square root --- */
/* GPU: sqrt() in OpenCL, also rsqrt() for 1/sqrt */
static inline double ts_sqrt(double x) {
    return sqrt(x);
}

/* --- Reciprocal square root --- */
/* Not in OpenSCAD but critical for GPU normalization */
/* GPU: rsqrt() native in OpenCL */
static inline double ts_rsqrt(double x) {
    return 1.0 / sqrt(x);
}

/* --- Exponential --- */
/* GPU: exp() in OpenCL */
static inline double ts_exp(double x) {
    return exp(x);
}

/* --- Exp base 2 --- */
/* Not in vanilla OpenSCAD but useful */
/* GPU: exp2() in OpenCL */
static inline double ts_exp2(double x) {
    return exp2(x);
}

/* --- Minimum (pairwise) --- */
/* GPU: fmin() in OpenCL */
static inline double ts_min(double a, double b) {
    return fmin(a, b);
}

/* --- Maximum (pairwise) --- */
/* GPU: fmax() in OpenCL */
static inline double ts_max(double a, double b) {
    return fmax(a, b);
}

/* --- Clamp --- */
/* Not in OpenSCAD but fundamental for GPU shaders */
/* GPU: clamp() in OpenCL */
static inline double ts_clamp(double x, double lo, double hi) {
    return fmin(fmax(x, lo), hi);
}

/* --- Linear interpolation --- */
/* Not in OpenSCAD but essential for parallel mesh generation */
/* GPU: mix() in OpenCL */
static inline double ts_lerp(double a, double b, double t) {
    return a + t * (b - a);
}

/* --- Smoothstep --- */
/* Hermite interpolation, GPU native */
static inline double ts_smoothstep(double edge0, double edge1, double x) {
    double t = ts_clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/* --- Fused multiply-add --- */
/* GPU: fma() in OpenCL, single instruction */
static inline double ts_fma(double a, double b, double c) {
    return fma(a, b, c);
}

/* --- Modulus (float) --- */
/* GPU: fmod() in OpenCL */
static inline double ts_fmod(double x, double y) {
    return fmod(x, y);
}

#endif /* TS_SCALAR_H */
