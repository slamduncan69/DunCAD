/*
 * ts_trig.h — Degree-based trigonometry
 *
 * CRITICAL: OpenSCAD uses DEGREES for all trig functions.
 * Standard C uses radians. This is the #1 source of bugs
 * when porting OpenSCAD math.
 *
 * All functions here accept/return degrees to match OpenSCAD.
 * GPU: convert to radians for OpenCL sin/cos, convert back.
 * The conversion is trivially parallel.
 *
 * OpenSCAD equivalents:
 *   sin, cos, tan, asin, acos, atan, atan2
 */
#ifndef TS_TRIG_H
#define TS_TRIG_H

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- Conversion constants --- */
#define TS_DEG_TO_RAD (M_PI / 180.0)
#define TS_RAD_TO_DEG (180.0 / M_PI)

/* --- Conversion functions --- */
/* GPU: trivially parallel, single multiply */
static inline double ts_deg2rad(double deg) {
    return deg * TS_DEG_TO_RAD;
}

static inline double ts_rad2deg(double rad) {
    return rad * TS_RAD_TO_DEG;
}

/* --- Sine (degrees) --- */
/* OpenSCAD: sin(90) = 1.0 */
/* GPU: convert to rad, call sin(), one kernel */
static inline double ts_sin_deg(double deg) {
    return sin(deg * TS_DEG_TO_RAD);
}

/* --- Cosine (degrees) --- */
/* GPU: convert to rad, call cos() */
static inline double ts_cos_deg(double deg) {
    return cos(deg * TS_DEG_TO_RAD);
}

/* --- Tangent (degrees) --- */
/* GPU: convert to rad, call tan() */
static inline double ts_tan_deg(double deg) {
    return tan(deg * TS_DEG_TO_RAD);
}

/* --- Arc sine (returns degrees) --- */
/* OpenSCAD: asin(1) = 90 */
/* GPU: asin() then multiply by RAD_TO_DEG */
static inline double ts_asin_deg(double x) {
    return asin(x) * TS_RAD_TO_DEG;
}

/* --- Arc cosine (returns degrees) --- */
/* GPU: acos() then multiply */
static inline double ts_acos_deg(double x) {
    return acos(x) * TS_RAD_TO_DEG;
}

/* --- Arc tangent (returns degrees) --- */
/* GPU: atan() then multiply */
static inline double ts_atan_deg(double x) {
    return atan(x) * TS_RAD_TO_DEG;
}

/* --- Arc tangent 2 (returns degrees) --- */
/* OpenSCAD: atan2(y, x) */
/* GPU: atan2() then multiply */
static inline double ts_atan2_deg(double y, double x) {
    return atan2(y, x) * TS_RAD_TO_DEG;
}

/* --- Sincos (simultaneous sin and cos) --- */
/* Not in OpenSCAD but critical for rotation matrices on GPU */
/* GPU: sincos() is a single instruction on most hardware */
static inline void ts_sincos_deg(double deg, double *out_sin, double *out_cos) {
    double rad = deg * TS_DEG_TO_RAD;
    *out_sin = sin(rad);
    *out_cos = cos(rad);
}

#endif /* TS_TRIG_H */
