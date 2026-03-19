/*
 * ts_bezier_surface.h — Quadratic Bezier Surface Patch
 *
 * Tensor-product quadratic bezier patch: S(u,v) = sum Bi(u)*Bj(v)*P[i][j]
 * 3x3 grid of control points (9 total) per patch.
 *
 * The MATH is the MESH. Voxels are just a lens.
 *
 * Each patch evaluates to a smooth parametric surface with:
 *   - Exact point evaluation at any (u,v) in [0,1]x[0,1]
 *   - Analytical surface normals via dS/du x dS/dv
 *   - Axis-aligned bounding box from control point hull
 *   - Closest point via Newton iteration (for SDF voxelization)
 *
 * Pure C, no heap allocation for single-patch ops.
 * Depends only on ts_vec.h.
 *
 * GPU: evaluation is per-point parallel (TRIVIAL).
 * 9 control points -> 20 FLOPs per surface point.
 *
 * Parallelism: SIMD for eval/normal, GPU for batch eval.
 */
#ifndef TS_BEZIER_SURFACE_H
#define TS_BEZIER_SURFACE_H

#include "ts_vec.h"
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- Quadratic Bezier basis functions --- */
/* B0(t) = (1-t)^2,  B1(t) = 2t(1-t),  B2(t) = t^2 */

static inline double ts_qbasis0(double t) { return (1.0-t)*(1.0-t); }
static inline double ts_qbasis1(double t) { return 2.0*t*(1.0-t); }
static inline double ts_qbasis2(double t) { return t*t; }

/* Derivatives: B0'(t) = -2(1-t), B1'(t) = 2-4t, B2'(t) = 2t */
static inline double ts_qbasis0d(double t) { return -2.0*(1.0-t); }
static inline double ts_qbasis1d(double t) { return 2.0 - 4.0*t; }
static inline double ts_qbasis2d(double t) { return 2.0*t; }

/* --- Bezier Surface Patch --- */

typedef struct {
    ts_vec3 cp[3][3];  /* control points: cp[row_v][col_u] */
} ts_bezier_patch;

/* --- Evaluate S(u,v) --- */
/* Tensor product: S(u,v) = sum_i sum_j Bi(u) * Bj(v) * cp[j][i] */
static inline ts_vec3 ts_bezier_patch_eval(const ts_bezier_patch *p,
                                            double u, double v) {
    double bu[3] = { ts_qbasis0(u), ts_qbasis1(u), ts_qbasis2(u) };
    double bv[3] = { ts_qbasis0(v), ts_qbasis1(v), ts_qbasis2(v) };

    ts_vec3 result = ts_vec3_zero();
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            double w = bu[i] * bv[j];
            result = ts_vec3_add(result, ts_vec3_scale(p->cp[j][i], w));
        }
    }
    return result;
}

/* --- Partial derivative dS/du --- */
static inline ts_vec3 ts_bezier_patch_dsu(const ts_bezier_patch *p,
                                           double u, double v) {
    double dbu[3] = { ts_qbasis0d(u), ts_qbasis1d(u), ts_qbasis2d(u) };
    double bv[3]  = { ts_qbasis0(v),  ts_qbasis1(v),  ts_qbasis2(v) };

    ts_vec3 result = ts_vec3_zero();
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            double w = dbu[i] * bv[j];
            result = ts_vec3_add(result, ts_vec3_scale(p->cp[j][i], w));
        }
    }
    return result;
}

/* --- Partial derivative dS/dv --- */
static inline ts_vec3 ts_bezier_patch_dsv(const ts_bezier_patch *p,
                                           double u, double v) {
    double bu[3]  = { ts_qbasis0(u),  ts_qbasis1(u),  ts_qbasis2(u) };
    double dbv[3] = { ts_qbasis0d(v), ts_qbasis1d(v), ts_qbasis2d(v) };

    ts_vec3 result = ts_vec3_zero();
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            double w = bu[i] * dbv[j];
            result = ts_vec3_add(result, ts_vec3_scale(p->cp[j][i], w));
        }
    }
    return result;
}

/* --- Surface normal at (u,v) --- */
/* Normal = normalize(dS/du x dS/dv) */
static inline ts_vec3 ts_bezier_patch_normal(const ts_bezier_patch *p,
                                              double u, double v) {
    ts_vec3 du = ts_bezier_patch_dsu(p, u, v);
    ts_vec3 dv = ts_bezier_patch_dsv(p, u, v);
    return ts_vec3_normalize(ts_vec3_cross(du, dv));
}

/* --- Axis-aligned bounding box --- */
/* The surface is contained within the convex hull of its control points. */
static inline void ts_bezier_patch_bbox(const ts_bezier_patch *p,
                                         ts_vec3 *out_min, ts_vec3 *out_max) {
    *out_min = p->cp[0][0];
    *out_max = p->cp[0][0];
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            *out_min = ts_vec3_min(*out_min, p->cp[j][i]);
            *out_max = ts_vec3_max(*out_max, p->cp[j][i]);
        }
    }
}

/* --- Closest point on surface to a query point --- */
/* Newton iteration in (u,v) space to minimize ||S(u,v) - query||^2.
 * Returns the (u,v) parameters. Caller can evaluate S(u,v) from those.
 * max_iter: typically 10-20 is sufficient.
 * Returns 0 on convergence, -1 on failure. */
static inline int ts_bezier_patch_closest_uv(const ts_bezier_patch *p,
                                              ts_vec3 query,
                                              double *out_u, double *out_v,
                                              int max_iter) {
    /* Start from center of parameter space */
    double u = 0.5, v = 0.5;

    for (int iter = 0; iter < max_iter; iter++) {
        ts_vec3 S  = ts_bezier_patch_eval(p, u, v);
        ts_vec3 Su = ts_bezier_patch_dsu(p, u, v);
        ts_vec3 Sv = ts_bezier_patch_dsv(p, u, v);
        ts_vec3 diff = ts_vec3_sub(S, query);

        /* Gradient of f(u,v) = ||S - query||^2:
         * df/du = 2 * dot(S-query, dS/du)
         * df/dv = 2 * dot(S-query, dS/dv) */
        double fu = ts_vec3_dot(diff, Su);
        double fv = ts_vec3_dot(diff, Sv);

        /* Check convergence */
        if (fabs(fu) < 1e-12 && fabs(fv) < 1e-12) {
            *out_u = u;
            *out_v = v;
            return 0;
        }

        /* Hessian (Gauss-Newton approximation):
         * H = [ dot(Su,Su)  dot(Su,Sv) ]
         *     [ dot(Su,Sv)  dot(Sv,Sv) ] */
        double H00 = ts_vec3_dot(Su, Su);
        double H01 = ts_vec3_dot(Su, Sv);
        double H11 = ts_vec3_dot(Sv, Sv);

        /* Solve 2x2 system: H * [du, dv]^T = -[fu, fv]^T */
        double det = H00 * H11 - H01 * H01;
        if (fabs(det) < 1e-20) break;  /* degenerate */

        double du = -(H11 * fu - H01 * fv) / det;
        double dv = -(H00 * fv - H01 * fu) / det;

        u += du;
        v += dv;

        /* Clamp to [0,1] */
        if (u < 0.0) u = 0.0;
        if (u > 1.0) u = 1.0;
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
    }

    *out_u = u;
    *out_v = v;
    return -1;  /* did not converge to tolerance */
}

/* --- Signed distance from query point to surface --- */
/* Positive = outside (in direction of normal), negative = inside.
 * Uses closest point + normal dot product for sign. */
static inline double ts_bezier_patch_sdf(const ts_bezier_patch *p,
                                          ts_vec3 query, int max_iter) {
    double u, v;
    ts_bezier_patch_closest_uv(p, query, &u, &v, max_iter);
    ts_vec3 closest = ts_bezier_patch_eval(p, u, v);
    ts_vec3 normal  = ts_bezier_patch_normal(p, u, v);
    ts_vec3 diff    = ts_vec3_sub(query, closest);
    double dist     = ts_vec3_norm(diff);
    double sign     = ts_vec3_dot(diff, normal) >= 0.0 ? 1.0 : -1.0;
    return sign * dist;
}

/* --- Convenience: make a flat patch in XY plane --- */
/* Control points on a regular grid from (x0,y0,z) to (x1,y1,z). */
static inline ts_bezier_patch ts_bezier_patch_flat(double x0, double y0,
                                                    double x1, double y1,
                                                    double z) {
    ts_bezier_patch p;
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            double u = (double)i / 2.0;
            double v = (double)j / 2.0;
            p.cp[j][i] = ts_vec3_make(
                x0 + u * (x1 - x0),
                y0 + v * (y1 - y0),
                z
            );
        }
    }
    return p;
}

/* --- Convenience: make a dome/hill patch --- */
/* Flat base from (x0,y0,0) to (x1,y1,0) with center CP raised to height. */
static inline ts_bezier_patch ts_bezier_patch_dome(double x0, double y0,
                                                    double x1, double y1,
                                                    double height) {
    ts_bezier_patch p = ts_bezier_patch_flat(x0, y0, x1, y1, 0.0);
    /* Raise center control point */
    p.cp[1][1].v[2] = height;
    return p;
}

/* ================================================================
 * Closed manifold primitives — surfaces with inside AND outside.
 * The SDF is meaningful: negative inside, positive outside.
 * "Everything that is not a dick is a hole." — Triclaude
 * ================================================================ */

/* --- Closed Bezier Sphere --- */
/* 6 quadratic bezier patches forming a sealed sphere.
 * Each patch covers one face of the circumscribed cube, with CPs
 * projected onto the sphere surface. Adjacent patches share boundary
 * CPs for watertight C0 continuity.
 *
 * Topology: cube → sphere projection.
 * Face 0: +X, Face 1: -X, Face 2: +Y, Face 3: -Y, Face 4: +Z, Face 5: -Z
 *
 * The quadratic bezier cannot perfectly represent a sphere (it's a
 * rational curve problem), but the approximation is excellent —
 * max error ~5.4% of radius at face centers. Good enough for SDF
 * voxelization where the narrowband handles the rest.
 */
typedef struct {
    ts_bezier_patch faces[6];
    double radius;
    ts_vec3 center;
} ts_bezier_sphere;

/* Project a point onto a sphere of given radius centered at origin. */
static inline ts_vec3 ts_project_to_sphere(ts_vec3 p, double radius) {
    double len = ts_vec3_norm(p);
    if (len < 1e-15) return ts_vec3_make(0.0, 0.0, radius);
    return ts_vec3_scale(p, radius / len);
}

static inline ts_bezier_sphere ts_bezier_sphere_new(ts_vec3 center,
                                                      double radius) {
    ts_bezier_sphere s;
    s.radius = radius;
    s.center = center;
    double r = radius;

    /* For each cube face, create 3x3 control points.
     * Start with cube coordinates, project onto sphere, then offset by center.
     *
     * The key insight: for a quadratic bezier to approximate a sphere,
     * the edge midpoint CPs must be on the sphere (not at the cube edge).
     * The corner CPs are shared between 3 faces.
     * The edge midpoint CPs are shared between 2 faces.
     * The center CP is unique to each face.
     *
     * Cube coordinates: each face has one axis fixed at ±1,
     * the other two vary in [-1, 0, +1].
     */

    /* Helper: cube face u,v to 3D cube point.
     * face: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
     * u,v in {-1, 0, 1} */
    static const int axes[6][3] = {
        /* {fixed_axis, u_axis, v_axis} with sign */
        {0, 1, 2},  /* +X: x=+1, u=y, v=z */
        {0, 1, 2},  /* -X: x=-1, u=y, v=z */
        {1, 0, 2},  /* +Y: y=+1, u=x, v=z */
        {1, 0, 2},  /* -Y: y=-1, u=x, v=z */
        {2, 0, 1},  /* +Z: z=+1, u=x, v=y */
        {2, 0, 1},  /* -Z: z=-1, u=x, v=y */
    };
    static const double signs[6] = { 1, -1, 1, -1, 1, -1 };

    for (int face = 0; face < 6; face++) {
        int fa = axes[face][0]; /* fixed axis */
        int ua = axes[face][1]; /* u axis */
        int va = axes[face][2]; /* v axis */
        double fs = signs[face]; /* fixed axis sign */

        for (int jv = 0; jv < 3; jv++) {
            for (int iu = 0; iu < 3; iu++) {
                double coords[3];
                coords[fa] = fs;                        /* fixed at ±1 */
                coords[ua] = (double)(iu - 1);          /* -1, 0, +1 */
                coords[va] = (double)(jv - 1);          /* -1, 0, +1 */

                /* Project cube point onto sphere */
                ts_vec3 cube_pt = ts_vec3_make(coords[0], coords[1], coords[2]);
                ts_vec3 sphere_pt = ts_project_to_sphere(cube_pt, r);

                /* Offset by center */
                s.faces[face].cp[jv][iu] = ts_vec3_add(sphere_pt, center);
            }
        }
    }

    return s;
}

/* Evaluate a point on the bezier sphere.
 * face: 0-5, u,v in [0,1]. */
static inline ts_vec3 ts_bezier_sphere_eval(const ts_bezier_sphere *s,
                                             int face, double u, double v) {
    return ts_bezier_patch_eval(&s->faces[face], u, v);
}

/* Normal at a point on the bezier sphere. */
static inline ts_vec3 ts_bezier_sphere_normal(const ts_bezier_sphere *s,
                                               int face, double u, double v) {
    return ts_bezier_patch_normal(&s->faces[face], u, v);
}

/* SDF of the bezier sphere at a query point.
 * Tests all 6 faces, returns minimum absolute distance with sign. */
static inline double ts_bezier_sphere_sdf(const ts_bezier_sphere *s,
                                           ts_vec3 query, int max_iter) {
    double best_dist = 1e30;
    double best_sign = 1.0;

    for (int face = 0; face < 6; face++) {
        double u, v;
        ts_bezier_patch_closest_uv(&s->faces[face], query, &u, &v, max_iter);
        ts_vec3 closest = ts_bezier_patch_eval(&s->faces[face], u, v);
        ts_vec3 normal  = ts_bezier_patch_normal(&s->faces[face], u, v);
        ts_vec3 diff    = ts_vec3_sub(query, closest);
        double dist     = ts_vec3_norm(diff);
        double sign     = ts_vec3_dot(diff, normal) >= 0.0 ? 1.0 : -1.0;

        if (dist < best_dist) {
            best_dist = dist;
            best_sign = sign;
        }
    }

    return best_sign * best_dist;
}

/* Bounding box of the entire sphere (from all 6 patch AABBs). */
static inline void ts_bezier_sphere_bbox(const ts_bezier_sphere *s,
                                          ts_vec3 *out_min, ts_vec3 *out_max) {
    ts_bezier_patch_bbox(&s->faces[0], out_min, out_max);
    for (int f = 1; f < 6; f++) {
        ts_vec3 fmin, fmax;
        ts_bezier_patch_bbox(&s->faces[f], &fmin, &fmax);
        *out_min = ts_vec3_min(*out_min, fmin);
        *out_max = ts_vec3_max(*out_max, fmax);
    }
}

/* --- Closed Bezier Torus --- */
/* A torus from a grid of patches wrapping in both U and V.
 * Major radius R (center to tube center), minor radius r (tube radius).
 * rows patches around the tube, cols patches around the ring.
 * Both dimensions wrap — fully closed manifold. */
typedef struct {
    ts_bezier_patch *faces;  /* rows * cols patches */
    int rows, cols;
    double major_r, minor_r;
    ts_vec3 center;
} ts_bezier_torus;

static inline ts_bezier_torus ts_bezier_torus_new(ts_vec3 center,
                                                    double major_r,
                                                    double minor_r,
                                                    int rows, int cols) {
    ts_bezier_torus t;
    t.center = center;
    t.major_r = major_r;
    t.minor_r = minor_r;
    t.rows = rows;
    t.cols = cols;
    t.faces = (ts_bezier_patch *)calloc((size_t)(rows * cols),
                                         sizeof(ts_bezier_patch));

    /* For each patch (r,c): spans a section of the torus.
     * theta = major angle (around ring), phi = minor angle (around tube).
     * Each patch covers (2*pi/cols) in theta, (2*pi/rows) in phi.
     * 3 CPs per patch in each direction → 2 intervals per patch.
     * With wrapping, CPs at patch boundaries are shared. */
    for (int pr = 0; pr < rows; pr++) {
        for (int pc = 0; pc < cols; pc++) {
            ts_bezier_patch *p = &t.faces[pr * cols + pc];

            for (int jv = 0; jv < 3; jv++) {
                for (int iu = 0; iu < 3; iu++) {
                    /* Parameter along ring (theta) and tube (phi) */
                    double theta_frac = ((double)pc + (double)iu / 2.0) / (double)cols;
                    double phi_frac   = ((double)pr + (double)jv / 2.0) / (double)rows;
                    double theta = 2.0 * M_PI * theta_frac;
                    double phi   = 2.0 * M_PI * phi_frac;

                    /* Torus parametric: */
                    double ct = cos(theta), st = sin(theta);
                    double cp_a = cos(phi), sp = sin(phi);
                    double x = (major_r + minor_r * cp_a) * ct;
                    double y = (major_r + minor_r * cp_a) * st;
                    double z = minor_r * sp;

                    p->cp[jv][iu] = ts_vec3_add(
                        ts_vec3_make(x, y, z), center);
                }
            }
        }
    }

    return t;
}

static inline void ts_bezier_torus_free(ts_bezier_torus *t) {
    free(t->faces);
    t->faces = NULL;
}

/* SDF of the bezier torus. Tests all patches. */
static inline double ts_bezier_torus_sdf(const ts_bezier_torus *t,
                                          ts_vec3 query, int max_iter) {
    double best_dist = 1e30;
    double best_sign = 1.0;
    int total = t->rows * t->cols;

    for (int f = 0; f < total; f++) {
        /* AABB cull */
        ts_vec3 pmin, pmax;
        ts_bezier_patch_bbox(&t->faces[f], &pmin, &pmax);
        double dx = fmax(pmin.v[0] - query.v[0], fmax(0.0, query.v[0] - pmax.v[0]));
        double dy = fmax(pmin.v[1] - query.v[1], fmax(0.0, query.v[1] - pmax.v[1]));
        double dz = fmax(pmin.v[2] - query.v[2], fmax(0.0, query.v[2] - pmax.v[2]));
        if (sqrt(dx*dx + dy*dy + dz*dz) > best_dist) continue;

        double u, v;
        ts_bezier_patch_closest_uv(&t->faces[f], query, &u, &v, max_iter);
        ts_vec3 closest = ts_bezier_patch_eval(&t->faces[f], u, v);
        ts_vec3 normal  = ts_bezier_patch_normal(&t->faces[f], u, v);
        ts_vec3 diff    = ts_vec3_sub(query, closest);
        double dist     = ts_vec3_norm(diff);
        double sign     = ts_vec3_dot(diff, normal) >= 0.0 ? 1.0 : -1.0;

        if (dist < best_dist) {
            best_dist = dist;
            best_sign = sign;
        }
    }

    return best_sign * best_dist;
}

static inline void ts_bezier_torus_bbox(const ts_bezier_torus *t,
                                         ts_vec3 *out_min, ts_vec3 *out_max) {
    if (t->rows * t->cols == 0) {
        *out_min = *out_max = t->center;
        return;
    }
    ts_bezier_patch_bbox(&t->faces[0], out_min, out_max);
    for (int f = 1; f < t->rows * t->cols; f++) {
        ts_vec3 fmin, fmax;
        ts_bezier_patch_bbox(&t->faces[f], &fmin, &fmax);
        *out_min = ts_vec3_min(*out_min, fmin);
        *out_max = ts_vec3_max(*out_max, fmax);
    }
}

#endif /* TS_BEZIER_SURFACE_H */
