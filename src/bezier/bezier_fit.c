#include "bezier/bezier_fit.h"

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Maximum recursion depth for subdivision */
#define FIT_MAX_DEPTH 16

/* -------------------------------------------------------------------------
 * Vector helpers
 * ---------------------------------------------------------------------- */

static DC_Point2
v2_sub(DC_Point2 a, DC_Point2 b)
{
    DC_Point2 r = { a.x - b.x, a.y - b.y };
    return r;
}

static DC_Point2
v2_add(DC_Point2 a, DC_Point2 b)
{
    DC_Point2 r = { a.x + b.x, a.y + b.y };
    return r;
}

static DC_Point2
v2_scale(DC_Point2 v, double s)
{
    DC_Point2 r = { v.x * s, v.y * s };
    return r;
}

static double
v2_dot(DC_Point2 a, DC_Point2 b)
{
    return a.x * b.x + a.y * b.y;
}

static double
v2_len(DC_Point2 v)
{
    return sqrt(v.x * v.x + v.y * v.y);
}

static DC_Point2
v2_normalize(DC_Point2 v)
{
    double len = v2_len(v);
    if (len < 1e-12) { DC_Point2 z = {0, 0}; return z; }
    return v2_scale(v, 1.0 / len);
}

/* -------------------------------------------------------------------------
 * Evaluate cubic bezier at t
 * ---------------------------------------------------------------------- */
static DC_Point2
bezier_eval(DC_Point2 p0, DC_Point2 p1, DC_Point2 p2, DC_Point2 p3, double t)
{
    double u = 1.0 - t;
    double u2 = u * u, u3 = u2 * u;
    double t2 = t * t, t3 = t2 * t;
    DC_Point2 r;
    r.x = u3*p0.x + 3*u2*t*p1.x + 3*u*t2*p2.x + t3*p3.x;
    r.y = u3*p0.y + 3*u2*t*p1.y + 3*u*t2*p2.y + t3*p3.y;
    return r;
}

/* Derivative of cubic bezier at t: B'(t) */
static DC_Point2
bezier_deriv(DC_Point2 p0, DC_Point2 p1, DC_Point2 p2, DC_Point2 p3, double t)
{
    double u = 1.0 - t;
    DC_Point2 r;
    r.x = 3*u*u*(p1.x-p0.x) + 6*u*t*(p2.x-p1.x) + 3*t*t*(p3.x-p2.x);
    r.y = 3*u*u*(p1.y-p0.y) + 6*u*t*(p2.y-p1.y) + 3*t*t*(p3.y-p2.y);
    return r;
}

/* Second derivative B''(t) */
static DC_Point2
bezier_deriv2(DC_Point2 p0, DC_Point2 p1, DC_Point2 p2, DC_Point2 p3, double t)
{
    double u = 1.0 - t;
    DC_Point2 r;
    r.x = 6*u*(p2.x - 2*p1.x + p0.x) + 6*t*(p3.x - 2*p2.x + p1.x);
    r.y = 6*u*(p2.y - 2*p1.y + p0.y) + 6*t*(p3.y - 2*p2.y + p1.y);
    return r;
}

/* -------------------------------------------------------------------------
 * Chord-length parameterization
 * ---------------------------------------------------------------------- */
static double *
chord_length_parameterize(const DC_Point2 *pts, int first, int last)
{
    int n = last - first + 1;
    double *u = malloc((size_t)n * sizeof(double));
    if (!u) return NULL;

    u[0] = 0.0;
    for (int i = 1; i < n; i++) {
        DC_Point2 d = v2_sub(pts[first + i], pts[first + i - 1]);
        u[i] = u[i-1] + v2_len(d);
    }

    double total = u[n-1];
    if (total > 1e-12) {
        for (int i = 1; i < n; i++)
            u[i] /= total;
    }

    return u;
}

/* -------------------------------------------------------------------------
 * Generate bezier — least-squares fit from tangents + parameterization
 * ---------------------------------------------------------------------- */
static void
generate_bezier(const DC_Point2 *pts, int first, int last,
                const double *u, DC_Point2 t_hat1, DC_Point2 t_hat2,
                DC_Point2 bezier[4])
{
    int n = last - first + 1;
    DC_Point2 p0 = pts[first];
    DC_Point2 p3 = pts[last];

    /* Compute A matrix columns (pre-multiply by tangent vectors) */
    double c00 = 0, c01 = 0, c11 = 0;
    double x0 = 0, x1 = 0;

    for (int i = 0; i < n; i++) {
        double t = u[i];
        double b1 = 3.0 * t * (1.0-t) * (1.0-t);
        double b2 = 3.0 * t * t * (1.0-t);

        DC_Point2 a1 = v2_scale(t_hat1, b1);
        DC_Point2 a2 = v2_scale(t_hat2, b2);

        c00 += v2_dot(a1, a1);
        c01 += v2_dot(a1, a2);
        c11 += v2_dot(a2, a2);

        double b0 = (1.0-t)*(1.0-t)*(1.0-t);
        double b3 = t*t*t;
        DC_Point2 tmp = v2_sub(pts[first + i],
                               v2_add(v2_scale(p0, b0 + b1),
                                      v2_scale(p3, b2 + b3)));
        x0 += v2_dot(a1, tmp);
        x1 += v2_dot(a2, tmp);
    }

    /* Solve 2x2 system */
    double det = c00 * c11 - c01 * c01;
    double alpha1, alpha2;

    if (fabs(det) < 1e-12) {
        /* Degenerate: use chord-length heuristic */
        double dist = v2_len(v2_sub(p3, p0)) / 3.0;
        alpha1 = dist;
        alpha2 = dist;
    } else {
        alpha1 = (c11 * x0 - c01 * x1) / det;
        alpha2 = (c00 * x1 - c01 * x0) / det;
    }

    /* If alpha is negative or zero, use chord-length heuristic */
    double seg_len = v2_len(v2_sub(p3, p0));
    double epsilon = 1e-6 * seg_len;

    if (alpha1 < epsilon || alpha2 < epsilon) {
        double dist = seg_len / 3.0;
        alpha1 = dist;
        alpha2 = dist;
    }

    bezier[0] = p0;
    bezier[1] = v2_add(p0, v2_scale(t_hat1, alpha1));
    bezier[2] = v2_add(p3, v2_scale(t_hat2, alpha2));
    bezier[3] = p3;
}

/* -------------------------------------------------------------------------
 * Newton-Raphson reparameterization
 * ---------------------------------------------------------------------- */
static double
newton_raphson_root(DC_Point2 bezier[4], DC_Point2 point, double u)
{
    DC_Point2 q = bezier_eval(bezier[0], bezier[1], bezier[2], bezier[3], u);
    DC_Point2 d = v2_sub(q, point);
    DC_Point2 q1 = bezier_deriv(bezier[0], bezier[1], bezier[2], bezier[3], u);
    DC_Point2 q2 = bezier_deriv2(bezier[0], bezier[1], bezier[2], bezier[3], u);

    double num = v2_dot(d, q1);
    double den = v2_dot(q1, q1) + v2_dot(d, q2);

    if (fabs(den) < 1e-12) return u;

    return u - num / den;
}

static double *
reparameterize(const DC_Point2 *pts, int first, int last,
               const double *u, DC_Point2 bezier[4])
{
    int n = last - first + 1;
    double *u_prime = malloc((size_t)n * sizeof(double));
    if (!u_prime) return NULL;

    for (int i = 0; i < n; i++) {
        u_prime[i] = newton_raphson_root(bezier, pts[first + i], u[i]);
        /* Clamp to [0, 1] */
        if (u_prime[i] < 0.0) u_prime[i] = 0.0;
        if (u_prime[i] > 1.0) u_prime[i] = 1.0;
    }

    return u_prime;
}

/* -------------------------------------------------------------------------
 * Compute maximum error and split point
 * ---------------------------------------------------------------------- */
static double
compute_max_error(const DC_Point2 *pts, int first, int last,
                  DC_Point2 bezier[4], const double *u, int *split_point)
{
    double max_dist = 0.0;
    *split_point = (first + last) / 2;

    for (int i = first + 1; i < last; i++) {
        DC_Point2 p = bezier_eval(bezier[0], bezier[1], bezier[2], bezier[3],
                                   u[i - first]);
        DC_Point2 d = v2_sub(p, pts[i]);
        double dist = v2_dot(d, d);  /* squared distance */
        if (dist > max_dist) {
            max_dist = dist;
            *split_point = i;
        }
    }

    return max_dist;
}

/* -------------------------------------------------------------------------
 * Emit a single cubic segment to output arrays
 * ---------------------------------------------------------------------- */
static void
emit_segment(DC_Point2 bezier[4], DC_Array *out_pts, DC_Array *out_junctures,
             int is_first)
{
    uint8_t flag;

    if (is_first) {
        /* First segment: emit P0 */
        flag = 1;
        dc_array_push(out_pts, &bezier[0]);
        dc_array_push(out_junctures, &flag);
    }

    /* C0 */
    flag = 0;
    dc_array_push(out_pts, &bezier[1]);
    dc_array_push(out_junctures, &flag);

    /* C1 */
    flag = 0;
    dc_array_push(out_pts, &bezier[2]);
    dc_array_push(out_junctures, &flag);

    /* P1 (juncture) */
    flag = 1;
    dc_array_push(out_pts, &bezier[3]);
    dc_array_push(out_junctures, &flag);
}

/* -------------------------------------------------------------------------
 * Recursive curve fitting
 * ---------------------------------------------------------------------- */
static void
fit_cubic(const DC_Point2 *pts, int first, int last,
          DC_Point2 t_hat1, DC_Point2 t_hat2,
          double error_tol,
          DC_Array *out_pts, DC_Array *out_junctures,
          int *is_first, int depth)
{
    int n = last - first + 1;

    /* Base case: 2 points — generate linear segment */
    if (n == 2) {
        double dist = v2_len(v2_sub(pts[last], pts[first])) / 3.0;
        DC_Point2 bezier[4];
        bezier[0] = pts[first];
        bezier[3] = pts[last];
        bezier[1] = v2_add(bezier[0], v2_scale(t_hat1, dist));
        bezier[2] = v2_add(bezier[3], v2_scale(t_hat2, dist));
        emit_segment(bezier, out_pts, out_junctures, *is_first);
        *is_first = 0;
        return;
    }

    /* Parameterize and fit */
    double *u = chord_length_parameterize(pts, first, last);
    if (!u) return;

    DC_Point2 bezier[4];
    generate_bezier(pts, first, last, u, t_hat1, t_hat2, bezier);

    int split_point;
    double max_err = compute_max_error(pts, first, last, bezier, u, &split_point);

    if (max_err < error_tol) {
        emit_segment(bezier, out_pts, out_junctures, *is_first);
        *is_first = 0;
        free(u);
        return;
    }

    /* Try reparameterization (up to 4 iterations) */
    if (max_err < error_tol * 4.0) {
        for (int iter = 0; iter < 4; iter++) {
            double *u_prime = reparameterize(pts, first, last, u, bezier);
            if (!u_prime) break;

            generate_bezier(pts, first, last, u_prime, t_hat1, t_hat2, bezier);
            max_err = compute_max_error(pts, first, last, bezier, u_prime,
                                        &split_point);

            free(u);
            u = u_prime;

            if (max_err < error_tol) {
                emit_segment(bezier, out_pts, out_junctures, *is_first);
                *is_first = 0;
                free(u);
                return;
            }
        }
    }

    free(u);

    /* Depth cap */
    if (depth >= FIT_MAX_DEPTH) {
        emit_segment(bezier, out_pts, out_junctures, *is_first);
        *is_first = 0;
        return;
    }

    /* Subdivide at the split point */
    DC_Point2 t_hat_center = v2_normalize(v2_sub(pts[split_point - 1],
                                                   pts[split_point + 1]));
    DC_Point2 t_hat_center_neg = v2_scale(t_hat_center, -1.0);

    fit_cubic(pts, first, split_point, t_hat1, t_hat_center,
              error_tol, out_pts, out_junctures, is_first, depth + 1);
    fit_cubic(pts, split_point, last, t_hat_center_neg, t_hat2,
              error_tol, out_pts, out_junctures, is_first, depth + 1);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool
dc_bezier_fit(const DC_Point2 *points, int n,
              double error_tol,
              DC_Array *out_pts,
              DC_Array *out_junctures,
              DC_Error *err)
{
    if (!points || !out_pts || !out_junctures) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL argument");
        return false;
    }

    if (n < 2) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG,
                              "need at least 2 points, got %d", n);
        return false;
    }

    if (error_tol <= 0.0) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG,
                              "error_tol must be positive");
        return false;
    }

    /* Compute endpoint tangents from neighbor points */
    DC_Point2 t_hat1, t_hat2;

    if (n == 2) {
        t_hat1 = v2_normalize(v2_sub(points[1], points[0]));
        t_hat2 = v2_scale(t_hat1, -1.0);
    } else {
        t_hat1 = v2_normalize(v2_sub(points[1], points[0]));
        t_hat2 = v2_normalize(v2_sub(points[n-2], points[n-1]));
    }

    int is_first = 1;
    fit_cubic(points, 0, n - 1, t_hat1, t_hat2,
              error_tol, out_pts, out_junctures, &is_first, 0);

    return true;
}
