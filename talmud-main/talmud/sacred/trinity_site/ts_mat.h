/*
 * ts_mat.h — 4x4 matrix operations
 *
 * Row-major 4x4 matrix (double[16]).
 * Indices: m[row*4 + col], so m[0..3] = row 0, m[4..7] = row 1, etc.
 *
 * GPU mapping: mat4 multiply = 16 dot products, each independent.
 * OpenCL: use float16 or manual unrolling.
 *
 * OpenSCAD equivalents:
 *   multmatrix(m) — ts_mat4_multiply
 *   translate([x,y,z]) — ts_mat4_translate
 *   rotate([x,y,z]) — ts_mat4_rotate_{x,y,z}
 *   rotate(a, v) — ts_mat4_rotate_axis
 *   scale([x,y,z]) — ts_mat4_scale
 *   mirror([x,y,z]) — ts_mat4_mirror
 */
#ifndef TS_MAT_H
#define TS_MAT_H

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    double m[16];
} ts_mat4;

/* --- Identity --- */
static inline ts_mat4 ts_mat4_identity(void) {
    ts_mat4 r;
    memset(r.m, 0, sizeof(r.m));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
    return r;
}

/* --- Zero --- */
static inline ts_mat4 ts_mat4_zero(void) {
    ts_mat4 r;
    memset(r.m, 0, sizeof(r.m));
    return r;
}

/* --- Multiply --- */
/* GPU: 16 independent dot products (each = 4 muls + 3 adds) */
/* This is the core operation — maps directly to GPU matrix multiply */
static inline ts_mat4 ts_mat4_multiply(ts_mat4 a, ts_mat4 b) {
    ts_mat4 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i*4+j] = a.m[i*4+0] * b.m[0*4+j]
                        + a.m[i*4+1] * b.m[1*4+j]
                        + a.m[i*4+2] * b.m[2*4+j]
                        + a.m[i*4+3] * b.m[3*4+j];
        }
    }
    return r;
}

/* --- Transpose --- */
/* GPU: trivial memory reorder */
static inline ts_mat4 ts_mat4_transpose(ts_mat4 a) {
    ts_mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i*4+j] = a.m[j*4+i];
    return r;
}

/* --- Transform a point (w=1) --- */
/* Applies full affine transform including translation */
/* GPU: 4 dot products */
static inline ts_vec3 ts_mat4_transform_point(ts_mat4 m, ts_vec3 p) {
    double w = m.m[12]*p.v[0] + m.m[13]*p.v[1] + m.m[14]*p.v[2] + m.m[15];
    if (fabs(w) < 1e-15) w = 1.0;
    return (ts_vec3){{
        (m.m[0]*p.v[0] + m.m[1]*p.v[1] + m.m[2]*p.v[2] + m.m[3]) / w,
        (m.m[4]*p.v[0] + m.m[5]*p.v[1] + m.m[6]*p.v[2] + m.m[7]) / w,
        (m.m[8]*p.v[0] + m.m[9]*p.v[1] + m.m[10]*p.v[2] + m.m[11]) / w
    }};
}

/* --- Transform a direction (w=0) --- */
/* No translation applied */
static inline ts_vec3 ts_mat4_transform_dir(ts_mat4 m, ts_vec3 d) {
    return (ts_vec3){{
        m.m[0]*d.v[0] + m.m[1]*d.v[1] + m.m[2]*d.v[2],
        m.m[4]*d.v[0] + m.m[5]*d.v[1] + m.m[6]*d.v[2],
        m.m[8]*d.v[0] + m.m[9]*d.v[1] + m.m[10]*d.v[2]
    }};
}

/* --- Translation matrix --- */
/* OpenSCAD: translate([x,y,z]) */
static inline ts_mat4 ts_mat4_translate(double x, double y, double z) {
    ts_mat4 r = ts_mat4_identity();
    r.m[3]  = x;
    r.m[7]  = y;
    r.m[11] = z;
    return r;
}

/* --- Scale matrix --- */
/* OpenSCAD: scale([x,y,z]) */
static inline ts_mat4 ts_mat4_scale(double x, double y, double z) {
    ts_mat4 r = ts_mat4_zero();
    r.m[0]  = x;
    r.m[5]  = y;
    r.m[10] = z;
    r.m[15] = 1.0;
    return r;
}

/* --- Rotation around X axis (degrees) --- */
static inline ts_mat4 ts_mat4_rotate_x(double deg) {
    double rad = deg * (M_PI / 180.0);
    double s = sin(rad), c = cos(rad);
    ts_mat4 r = ts_mat4_identity();
    r.m[5]  =  c;  r.m[6]  = -s;
    r.m[9]  =  s;  r.m[10] =  c;
    return r;
}

/* --- Rotation around Y axis (degrees) --- */
static inline ts_mat4 ts_mat4_rotate_y(double deg) {
    double rad = deg * (M_PI / 180.0);
    double s = sin(rad), c = cos(rad);
    ts_mat4 r = ts_mat4_identity();
    r.m[0]  =  c;  r.m[2]  =  s;
    r.m[8]  = -s;  r.m[10] =  c;
    return r;
}

/* --- Rotation around Z axis (degrees) --- */
static inline ts_mat4 ts_mat4_rotate_z(double deg) {
    double rad = deg * (M_PI / 180.0);
    double s = sin(rad), c = cos(rad);
    ts_mat4 r = ts_mat4_identity();
    r.m[0] =  c;  r.m[1] = -s;
    r.m[4] =  s;  r.m[5] =  c;
    return r;
}

/* --- Rotation around arbitrary axis (degrees) --- */
/* OpenSCAD: rotate(a, v=[x,y,z]) */
/* Rodrigues' rotation formula — GPU friendly */
static inline ts_mat4 ts_mat4_rotate_axis(double deg, ts_vec3 axis) {
    double rad = deg * (M_PI / 180.0);
    double s = sin(rad), c = cos(rad);
    double t = 1.0 - c;

    /* Normalize axis */
    double len = sqrt(axis.v[0]*axis.v[0] + axis.v[1]*axis.v[1] + axis.v[2]*axis.v[2]);
    if (len < 1e-15) return ts_mat4_identity();
    double x = axis.v[0]/len, y = axis.v[1]/len, z = axis.v[2]/len;

    ts_mat4 r = ts_mat4_identity();
    r.m[0]  = t*x*x + c;     r.m[1]  = t*x*y - s*z;   r.m[2]  = t*x*z + s*y;
    r.m[4]  = t*x*y + s*z;   r.m[5]  = t*y*y + c;     r.m[6]  = t*y*z - s*x;
    r.m[8]  = t*x*z - s*y;   r.m[9]  = t*y*z + s*x;   r.m[10] = t*z*z + c;
    return r;
}

/* --- OpenSCAD-style rotation: rotate([rx, ry, rz]) --- */
/* Applies Z * Y * X rotation (OpenSCAD convention) */
static inline ts_mat4 ts_mat4_rotate_euler(double rx, double ry, double rz) {
    ts_mat4 mx = ts_mat4_rotate_x(rx);
    ts_mat4 my = ts_mat4_rotate_y(ry);
    ts_mat4 mz = ts_mat4_rotate_z(rz);
    return ts_mat4_multiply(mz, ts_mat4_multiply(my, mx));
}

/* --- Mirror matrix --- */
/* OpenSCAD: mirror([x,y,z]) — reflects across the plane with given normal */
static inline ts_mat4 ts_mat4_mirror(ts_vec3 normal) {
    /* Normalize */
    double len = sqrt(normal.v[0]*normal.v[0] + normal.v[1]*normal.v[1] +
                      normal.v[2]*normal.v[2]);
    if (len < 1e-15) return ts_mat4_identity();
    double x = normal.v[0]/len, y = normal.v[1]/len, z = normal.v[2]/len;

    /* Householder reflection: I - 2*n*n^T */
    ts_mat4 r = ts_mat4_identity();
    r.m[0]  = 1.0 - 2.0*x*x;   r.m[1]  = -2.0*x*y;       r.m[2]  = -2.0*x*z;
    r.m[4]  = -2.0*x*y;         r.m[5]  = 1.0 - 2.0*y*y;  r.m[6]  = -2.0*y*z;
    r.m[8]  = -2.0*x*z;         r.m[9]  = -2.0*y*z;        r.m[10] = 1.0 - 2.0*z*z;
    return r;
}

/* --- Determinant (3x3 upper-left) --- */
/* Needed for normal transform and CSG orientation checks */
static inline double ts_mat4_det3(ts_mat4 m) {
    return m.m[0] * (m.m[5]*m.m[10] - m.m[6]*m.m[9])
         - m.m[1] * (m.m[4]*m.m[10] - m.m[6]*m.m[8])
         + m.m[2] * (m.m[4]*m.m[9]  - m.m[5]*m.m[8]);
}

/* --- Full 4x4 determinant --- */
static inline double ts_mat4_det(ts_mat4 m) {
    double a0 = m.m[0]*m.m[5]  - m.m[1]*m.m[4];
    double a1 = m.m[0]*m.m[6]  - m.m[2]*m.m[4];
    double a2 = m.m[0]*m.m[7]  - m.m[3]*m.m[4];
    double a3 = m.m[1]*m.m[6]  - m.m[2]*m.m[5];
    double a4 = m.m[1]*m.m[7]  - m.m[3]*m.m[5];
    double a5 = m.m[2]*m.m[7]  - m.m[3]*m.m[6];
    double b0 = m.m[8]*m.m[13] - m.m[9]*m.m[12];
    double b1 = m.m[8]*m.m[14] - m.m[10]*m.m[12];
    double b2 = m.m[8]*m.m[15] - m.m[11]*m.m[12];
    double b3 = m.m[9]*m.m[14] - m.m[10]*m.m[13];
    double b4 = m.m[9]*m.m[15] - m.m[11]*m.m[13];
    double b5 = m.m[10]*m.m[15] - m.m[11]*m.m[14];
    return a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0;
}

/* --- Inverse (full 4x4) --- */
/* Returns identity if singular. GPU: batch-invertible via adjugate method */
static inline ts_mat4 ts_mat4_inverse(ts_mat4 m) {
    double a0 = m.m[0]*m.m[5]  - m.m[1]*m.m[4];
    double a1 = m.m[0]*m.m[6]  - m.m[2]*m.m[4];
    double a2 = m.m[0]*m.m[7]  - m.m[3]*m.m[4];
    double a3 = m.m[1]*m.m[6]  - m.m[2]*m.m[5];
    double a4 = m.m[1]*m.m[7]  - m.m[3]*m.m[5];
    double a5 = m.m[2]*m.m[7]  - m.m[3]*m.m[6];
    double b0 = m.m[8]*m.m[13] - m.m[9]*m.m[12];
    double b1 = m.m[8]*m.m[14] - m.m[10]*m.m[12];
    double b2 = m.m[8]*m.m[15] - m.m[11]*m.m[12];
    double b3 = m.m[9]*m.m[14] - m.m[10]*m.m[13];
    double b4 = m.m[9]*m.m[15] - m.m[11]*m.m[13];
    double b5 = m.m[10]*m.m[15] - m.m[11]*m.m[14];

    double det = a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0;
    if (fabs(det) < 1e-15) return ts_mat4_identity();

    double inv_det = 1.0 / det;
    ts_mat4 r;

    r.m[0]  = ( m.m[5]*b5  - m.m[6]*b4  + m.m[7]*b3 ) * inv_det;
    r.m[1]  = (-m.m[1]*b5  + m.m[2]*b4  - m.m[3]*b3 ) * inv_det;
    r.m[2]  = ( m.m[13]*a5 - m.m[14]*a4 + m.m[15]*a3) * inv_det;
    r.m[3]  = (-m.m[9]*a5  + m.m[10]*a4 - m.m[11]*a3) * inv_det;
    r.m[4]  = (-m.m[4]*b5  + m.m[6]*b2  - m.m[7]*b1 ) * inv_det;
    r.m[5]  = ( m.m[0]*b5  - m.m[2]*b2  + m.m[3]*b1 ) * inv_det;
    r.m[6]  = (-m.m[12]*a5 + m.m[14]*a2 - m.m[15]*a1) * inv_det;
    r.m[7]  = ( m.m[8]*a5  - m.m[10]*a2 + m.m[11]*a1) * inv_det;
    r.m[8]  = ( m.m[4]*b4  - m.m[5]*b2  + m.m[7]*b0 ) * inv_det;
    r.m[9]  = (-m.m[0]*b4  + m.m[1]*b2  - m.m[3]*b0 ) * inv_det;
    r.m[10] = ( m.m[12]*a4 - m.m[13]*a2 + m.m[15]*a0) * inv_det;
    r.m[11] = (-m.m[8]*a4  + m.m[9]*a2  - m.m[11]*a0) * inv_det;
    r.m[12] = (-m.m[4]*b3  + m.m[5]*b1  - m.m[6]*b0 ) * inv_det;
    r.m[13] = ( m.m[0]*b3  - m.m[1]*b1  + m.m[2]*b0 ) * inv_det;
    r.m[14] = (-m.m[12]*a3 + m.m[13]*a1 - m.m[14]*a0) * inv_det;
    r.m[15] = ( m.m[8]*a3  - m.m[9]*a1  + m.m[10]*a0) * inv_det;

    return r;
}

#endif /* TS_MAT_H */
