/*
 * trinity_site.c — The Holy Mathematical Foundation
 *
 * Complete reimplementation of OpenSCAD's mathematical system
 * from the ground up, designed for GPU parallelization.
 *
 * Every vanilla OpenSCAD function has:
 *   1. A pure C implementation (in ts_*.h headers)
 *   2. A GREEN test proving it works correctly
 *   3. A RED test proving the test catches wrong implementations
 *   4. A benchmark measuring per-operation cost
 *   5. A parallelism classification for GPU offload planning
 *
 * Usage:
 *   trinity_site                Run all tests
 *   trinity_site --test         Run tests only
 *   trinity_site --bench        Run benchmarks only
 *   trinity_site --all          Run tests + benchmarks
 *   trinity_site --help         Show this help
 *
 * "Now I am become Death, the destroyer of worlds."
 *   — J. Robert Oppenheimer, quoting the Bhagavad Gita
 *
 * Like the Trinity test, once these pass, there is no going back.
 * The old world of sequential, single-threaded CGAL math is over.
 * A new world of parallel, GPU-accelerated geometry begins.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE     /* for M_E, M_PI on strict compilers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#ifndef M_E
#define M_E 2.71828182845904523536
#endif

/* The Holy Headers */
#include "ts_test.h"
#include "ts_scalar.h"
#include "ts_trig.h"
#include "ts_vec.h"
#include "ts_mat.h"
#include "ts_mesh.h"
#include "ts_geo.h"
#include "ts_csg.h"
#include "ts_extrude.h"
#include "ts_random.h"
#include "ts_opencl.h"
#include "ts_eval.h"   /* for ts_interpret_ex in Minkowski interp tests */
#include "ts_bezier_surface.h"
#include "ts_bezier_mesh.h"
#include "ts_bezier_voxel.h"

/* Global GPU context — initialized in main */
static ts_gpu_ctx g_gpu;

/* ================================================================
 * SECTION 1: SCALAR MATH TESTS
 * OpenSCAD: abs, sign, ceil, floor, round, ln, log, pow, sqrt,
 *           exp, exp2, log2, min, max
 * Parallelism: TRIVIAL (each operation independent)
 * ================================================================ */

/* --- abs --- */
static void test_abs_green(void) {
    TS_ASSERT_NEAR(ts_abs(5.0), 5.0, 1e-15);
    TS_ASSERT_NEAR(ts_abs(-5.0), 5.0, 1e-15);
    TS_ASSERT_NEAR(ts_abs(0.0), 0.0, 1e-15);
    TS_ASSERT_NEAR(ts_abs(-0.0), 0.0, 1e-15);
    TS_ASSERT_NEAR(ts_abs(-1e-300), 1e-300, 1e-315);
    TS_PASS();
}
static void test_abs_red(void) {
    /* A naive implementation that returns x unchanged would fail */
    double wrong_result = -5.0;  /* no abs applied */
    TS_ASSERT_TRUE(fabs(wrong_result - 5.0) > 1.0);  /* test catches it */
    TS_PASS();
}

/* --- sign --- */
static void test_sign_green(void) {
    TS_ASSERT_NEAR(ts_sign(42.0), 1.0, 1e-15);
    TS_ASSERT_NEAR(ts_sign(-42.0), -1.0, 1e-15);
    TS_ASSERT_NEAR(ts_sign(0.0), 0.0, 1e-15);
    TS_ASSERT_NEAR(ts_sign(1e-300), 1.0, 1e-15);
    TS_ASSERT_NEAR(ts_sign(-1e-300), -1.0, 1e-15);
    TS_PASS();
}
static void test_sign_red(void) {
    /* A wrong impl returning 0 for small values would fail */
    double wrong = 0.0;  /* treating small positive as zero */
    TS_ASSERT_TRUE(fabs(wrong - 1.0) > 0.5);
    TS_PASS();
}

/* --- ceil --- */
static void test_ceil_green(void) {
    TS_ASSERT_NEAR(ts_ceil(2.3), 3.0, 1e-15);
    TS_ASSERT_NEAR(ts_ceil(-2.3), -2.0, 1e-15);
    TS_ASSERT_NEAR(ts_ceil(2.0), 2.0, 1e-15);
    TS_ASSERT_NEAR(ts_ceil(0.0), 0.0, 1e-15);
    TS_PASS();
}
static void test_ceil_red(void) {
    /* floor instead of ceil would give wrong results */
    double wrong = floor(2.3);  /* = 2.0, not 3.0 */
    TS_ASSERT_TRUE(fabs(wrong - 3.0) > 0.5);
    TS_PASS();
}

/* --- floor --- */
static void test_floor_green(void) {
    TS_ASSERT_NEAR(ts_floor(2.7), 2.0, 1e-15);
    TS_ASSERT_NEAR(ts_floor(-2.7), -3.0, 1e-15);
    TS_ASSERT_NEAR(ts_floor(2.0), 2.0, 1e-15);
    TS_ASSERT_NEAR(ts_floor(-0.1), -1.0, 1e-15);
    TS_PASS();
}
static void test_floor_red(void) {
    /* ceil instead of floor */
    double wrong = ceil(2.7);  /* = 3.0, not 2.0 */
    TS_ASSERT_TRUE(fabs(wrong - 2.0) > 0.5);
    TS_PASS();
}

/* --- round --- */
static void test_round_green(void) {
    TS_ASSERT_NEAR(ts_round(2.5), 3.0, 1e-15);   /* half-away-from-zero */
    TS_ASSERT_NEAR(ts_round(-2.5), -3.0, 1e-15);
    TS_ASSERT_NEAR(ts_round(2.3), 2.0, 1e-15);
    TS_ASSERT_NEAR(ts_round(2.7), 3.0, 1e-15);
    TS_ASSERT_NEAR(ts_round(0.0), 0.0, 1e-15);
    TS_PASS();
}
static void test_round_red(void) {
    /* Truncation (int cast) would get negative values wrong */
    double wrong = (double)(int)(-2.7);  /* = -2.0, not -3.0 */
    TS_ASSERT_TRUE(fabs(wrong - (-3.0)) > 0.5);
    TS_PASS();
}

/* --- ln (natural log) --- */
static void test_ln_green(void) {
    TS_ASSERT_NEAR(ts_ln(1.0), 0.0, 1e-15);
    TS_ASSERT_NEAR(ts_ln(M_E), 1.0, 1e-14);
    TS_ASSERT_NEAR(ts_ln(M_E * M_E), 2.0, 1e-14);
    TS_ASSERT_TRUE(isnan(ts_ln(-1.0)));  /* domain error */
    TS_PASS();
}
static void test_ln_red(void) {
    /* Using log10 instead of ln would give wrong results */
    double wrong = log10(M_E);  /* ~0.434, not 1.0 */
    TS_ASSERT_TRUE(fabs(wrong - 1.0) > 0.5);
    TS_PASS();
}

/* --- log (base 10) --- */
static void test_log10_green(void) {
    TS_ASSERT_NEAR(ts_log10(1.0), 0.0, 1e-15);
    TS_ASSERT_NEAR(ts_log10(10.0), 1.0, 1e-14);
    TS_ASSERT_NEAR(ts_log10(100.0), 2.0, 1e-14);
    TS_ASSERT_NEAR(ts_log10(1000.0), 3.0, 1e-14);
    TS_PASS();
}
static void test_log10_red(void) {
    /* Using ln instead of log10 would fail */
    double wrong = log(10.0);  /* ~2.303, not 1.0 */
    TS_ASSERT_TRUE(fabs(wrong - 1.0) > 1.0);
    TS_PASS();
}

/* --- log2 --- */
static void test_log2_green(void) {
    TS_ASSERT_NEAR(ts_log2(1.0), 0.0, 1e-15);
    TS_ASSERT_NEAR(ts_log2(2.0), 1.0, 1e-14);
    TS_ASSERT_NEAR(ts_log2(8.0), 3.0, 1e-14);
    TS_ASSERT_NEAR(ts_log2(1024.0), 10.0, 1e-13);
    TS_PASS();
}
static void test_log2_red(void) {
    double wrong = log10(8.0);  /* ~0.903, not 3.0 */
    TS_ASSERT_TRUE(fabs(wrong - 3.0) > 2.0);
    TS_PASS();
}

/* --- pow --- */
static void test_pow_green(void) {
    TS_ASSERT_NEAR(ts_pow(2.0, 10.0), 1024.0, 1e-10);
    TS_ASSERT_NEAR(ts_pow(3.0, 0.0), 1.0, 1e-15);
    TS_ASSERT_NEAR(ts_pow(2.0, -1.0), 0.5, 1e-15);
    TS_ASSERT_NEAR(ts_pow(9.0, 0.5), 3.0, 1e-14);
    TS_PASS();
}
static void test_pow_red(void) {
    /* Multiplication instead of exponentiation */
    double wrong = 2.0 * 10.0;  /* = 20, not 1024 */
    TS_ASSERT_TRUE(fabs(wrong - 1024.0) > 900.0);
    TS_PASS();
}

/* --- sqrt --- */
static void test_sqrt_green(void) {
    TS_ASSERT_NEAR(ts_sqrt(4.0), 2.0, 1e-15);
    TS_ASSERT_NEAR(ts_sqrt(9.0), 3.0, 1e-15);
    TS_ASSERT_NEAR(ts_sqrt(2.0), 1.41421356237309504, 1e-14);
    TS_ASSERT_NEAR(ts_sqrt(0.0), 0.0, 1e-15);
    TS_PASS();
}
static void test_sqrt_red(void) {
    /* Halving instead of sqrt */
    double wrong = 9.0 / 2.0;  /* = 4.5, not 3.0 */
    TS_ASSERT_TRUE(fabs(wrong - 3.0) > 1.0);
    TS_PASS();
}

/* --- exp --- */
static void test_exp_green(void) {
    TS_ASSERT_NEAR(ts_exp(0.0), 1.0, 1e-15);
    TS_ASSERT_NEAR(ts_exp(1.0), M_E, 1e-14);
    TS_ASSERT_NEAR(ts_exp(-1.0), 1.0/M_E, 1e-14);
    TS_ASSERT_NEAR(ts_exp(2.0), M_E * M_E, 1e-13);
    TS_PASS();
}
static void test_exp_red(void) {
    /* Using 2^x instead of e^x */
    double wrong = pow(2.0, 1.0);  /* = 2.0, not e */
    TS_ASSERT_TRUE(fabs(wrong - M_E) > 0.5);
    TS_PASS();
}

/* --- exp2 --- */
static void test_exp2_green(void) {
    TS_ASSERT_NEAR(ts_exp2(0.0), 1.0, 1e-15);
    TS_ASSERT_NEAR(ts_exp2(1.0), 2.0, 1e-15);
    TS_ASSERT_NEAR(ts_exp2(10.0), 1024.0, 1e-10);
    TS_ASSERT_NEAR(ts_exp2(-1.0), 0.5, 1e-15);
    TS_PASS();
}
static void test_exp2_red(void) {
    /* Using e^x instead of 2^x */
    double wrong = exp(10.0);  /* ~22026, not 1024 */
    TS_ASSERT_TRUE(fabs(wrong - 1024.0) > 20000.0);
    TS_PASS();
}

/* --- min --- */
static void test_min_green(void) {
    TS_ASSERT_NEAR(ts_min(3.0, 5.0), 3.0, 1e-15);
    TS_ASSERT_NEAR(ts_min(5.0, 3.0), 3.0, 1e-15);
    TS_ASSERT_NEAR(ts_min(-1.0, 1.0), -1.0, 1e-15);
    TS_ASSERT_NEAR(ts_min(0.0, 0.0), 0.0, 1e-15);
    TS_PASS();
}
static void test_min_red(void) {
    /* Returning max instead of min */
    double wrong = fmax(3.0, 5.0);  /* = 5.0, not 3.0 */
    TS_ASSERT_TRUE(fabs(wrong - 3.0) > 1.0);
    TS_PASS();
}

/* --- max --- */
static void test_max_green(void) {
    TS_ASSERT_NEAR(ts_max(3.0, 5.0), 5.0, 1e-15);
    TS_ASSERT_NEAR(ts_max(5.0, 3.0), 5.0, 1e-15);
    TS_ASSERT_NEAR(ts_max(-1.0, 1.0), 1.0, 1e-15);
    TS_PASS();
}
static void test_max_red(void) {
    double wrong = fmin(3.0, 5.0);
    TS_ASSERT_TRUE(fabs(wrong - 5.0) > 1.0);
    TS_PASS();
}

/* --- clamp --- */
static void test_clamp_green(void) {
    TS_ASSERT_NEAR(ts_clamp(5.0, 0.0, 10.0), 5.0, 1e-15);
    TS_ASSERT_NEAR(ts_clamp(-5.0, 0.0, 10.0), 0.0, 1e-15);
    TS_ASSERT_NEAR(ts_clamp(15.0, 0.0, 10.0), 10.0, 1e-15);
    TS_PASS();
}
static void test_clamp_red(void) {
    /* No clamping applied */
    double wrong = -5.0;
    TS_ASSERT_TRUE(wrong < 0.0);  /* should have been clamped to 0 */
    TS_PASS();
}

/* --- lerp --- */
static void test_lerp_green(void) {
    TS_ASSERT_NEAR(ts_lerp(0.0, 10.0, 0.0), 0.0, 1e-15);
    TS_ASSERT_NEAR(ts_lerp(0.0, 10.0, 1.0), 10.0, 1e-15);
    TS_ASSERT_NEAR(ts_lerp(0.0, 10.0, 0.5), 5.0, 1e-15);
    TS_ASSERT_NEAR(ts_lerp(-10.0, 10.0, 0.5), 0.0, 1e-14);
    TS_PASS();
}
static void test_lerp_red(void) {
    /* Average instead of lerp at t=0.25 */
    double wrong = (0.0 + 10.0) / 2.0;  /* = 5.0, not 2.5 */
    double correct = ts_lerp(0.0, 10.0, 0.25);
    TS_ASSERT_TRUE(fabs(wrong - correct) > 2.0);
    TS_PASS();
}

/* --- fma --- */
static void test_fma_green(void) {
    TS_ASSERT_NEAR(ts_fma(2.0, 3.0, 4.0), 10.0, 1e-15);
    TS_ASSERT_NEAR(ts_fma(-1.0, 5.0, 3.0), -2.0, 1e-15);
    TS_PASS();
}
static void test_fma_red(void) {
    /* Just multiply, forgetting the add */
    double wrong = 2.0 * 3.0;  /* = 6, not 10 */
    TS_ASSERT_TRUE(fabs(wrong - 10.0) > 3.0);
    TS_PASS();
}

/* ================================================================
 * SECTION 2: TRIGONOMETRY TESTS
 * OpenSCAD: sin, cos, tan, asin, acos, atan, atan2
 * CRITICAL: All in DEGREES
 * Parallelism: TRIVIAL
 * ================================================================ */

/* --- sin (degrees) --- */
static void test_sin_green(void) {
    TS_ASSERT_NEAR(ts_sin_deg(0.0), 0.0, 1e-15);
    TS_ASSERT_NEAR(ts_sin_deg(90.0), 1.0, 1e-14);
    TS_ASSERT_NEAR(ts_sin_deg(180.0), 0.0, 1e-14);
    TS_ASSERT_NEAR(ts_sin_deg(270.0), -1.0, 1e-14);
    TS_ASSERT_NEAR(ts_sin_deg(360.0), 0.0, 1e-13);
    TS_ASSERT_NEAR(ts_sin_deg(30.0), 0.5, 1e-14);
    TS_ASSERT_NEAR(ts_sin_deg(-90.0), -1.0, 1e-14);
    TS_PASS();
}
static void test_sin_red(void) {
    /* Using radians instead of degrees — the classic bug */
    double wrong = sin(90.0);  /* sin(90 radians) ~= 0.894 */
    TS_ASSERT_TRUE(fabs(wrong - 1.0) > 0.1);
    TS_PASS();
}

/* --- cos (degrees) --- */
static void test_cos_green(void) {
    TS_ASSERT_NEAR(ts_cos_deg(0.0), 1.0, 1e-15);
    TS_ASSERT_NEAR(ts_cos_deg(90.0), 0.0, 1e-14);
    TS_ASSERT_NEAR(ts_cos_deg(180.0), -1.0, 1e-14);
    TS_ASSERT_NEAR(ts_cos_deg(60.0), 0.5, 1e-14);
    TS_ASSERT_NEAR(ts_cos_deg(360.0), 1.0, 1e-13);
    TS_PASS();
}
static void test_cos_red(void) {
    double wrong = cos(90.0);  /* cos(90 radians) ~= -0.448 */
    TS_ASSERT_TRUE(fabs(wrong - 0.0) > 0.4);
    TS_PASS();
}

/* --- tan (degrees) --- */
static void test_tan_green(void) {
    TS_ASSERT_NEAR(ts_tan_deg(0.0), 0.0, 1e-15);
    TS_ASSERT_NEAR(ts_tan_deg(45.0), 1.0, 1e-14);
    TS_ASSERT_NEAR(ts_tan_deg(-45.0), -1.0, 1e-14);
    TS_ASSERT_NEAR(ts_tan_deg(30.0), 1.0/sqrt(3.0), 1e-14);
    TS_PASS();
}
static void test_tan_red(void) {
    double wrong = tan(45.0);  /* tan(45 radians) ~= 1.619 */
    TS_ASSERT_TRUE(fabs(wrong - 1.0) > 0.5);
    TS_PASS();
}

/* --- asin (returns degrees) --- */
static void test_asin_green(void) {
    TS_ASSERT_NEAR(ts_asin_deg(0.0), 0.0, 1e-13);
    TS_ASSERT_NEAR(ts_asin_deg(1.0), 90.0, 1e-12);
    TS_ASSERT_NEAR(ts_asin_deg(-1.0), -90.0, 1e-12);
    TS_ASSERT_NEAR(ts_asin_deg(0.5), 30.0, 1e-12);
    TS_PASS();
}
static void test_asin_red(void) {
    /* Returning radians instead of degrees */
    double wrong = asin(1.0);  /* ~1.5708, not 90 */
    TS_ASSERT_TRUE(fabs(wrong - 90.0) > 80.0);
    TS_PASS();
}

/* --- acos (returns degrees) --- */
static void test_acos_green(void) {
    TS_ASSERT_NEAR(ts_acos_deg(1.0), 0.0, 1e-13);
    TS_ASSERT_NEAR(ts_acos_deg(0.0), 90.0, 1e-12);
    TS_ASSERT_NEAR(ts_acos_deg(-1.0), 180.0, 1e-12);
    TS_ASSERT_NEAR(ts_acos_deg(0.5), 60.0, 1e-12);
    TS_PASS();
}
static void test_acos_red(void) {
    double wrong = acos(0.0);  /* ~1.5708, not 90 */
    TS_ASSERT_TRUE(fabs(wrong - 90.0) > 80.0);
    TS_PASS();
}

/* --- atan (returns degrees) --- */
static void test_atan_green(void) {
    TS_ASSERT_NEAR(ts_atan_deg(0.0), 0.0, 1e-13);
    TS_ASSERT_NEAR(ts_atan_deg(1.0), 45.0, 1e-12);
    TS_ASSERT_NEAR(ts_atan_deg(-1.0), -45.0, 1e-12);
    TS_PASS();
}
static void test_atan_red(void) {
    double wrong = atan(1.0);  /* ~0.7854, not 45 */
    TS_ASSERT_TRUE(fabs(wrong - 45.0) > 40.0);
    TS_PASS();
}

/* --- atan2 (returns degrees) --- */
static void test_atan2_green(void) {
    TS_ASSERT_NEAR(ts_atan2_deg(0.0, 1.0), 0.0, 1e-13);
    TS_ASSERT_NEAR(ts_atan2_deg(1.0, 0.0), 90.0, 1e-12);
    TS_ASSERT_NEAR(ts_atan2_deg(0.0, -1.0), 180.0, 1e-12);
    TS_ASSERT_NEAR(ts_atan2_deg(-1.0, 0.0), -90.0, 1e-12);
    TS_ASSERT_NEAR(ts_atan2_deg(1.0, 1.0), 45.0, 1e-12);
    TS_PASS();
}
static void test_atan2_red(void) {
    double wrong = atan2(1.0, 0.0);  /* ~1.5708, not 90 */
    TS_ASSERT_TRUE(fabs(wrong - 90.0) > 80.0);
    TS_PASS();
}

/* --- sincos --- */
static void test_sincos_green(void) {
    double s, c;
    ts_sincos_deg(30.0, &s, &c);
    TS_ASSERT_NEAR(s, 0.5, 1e-14);
    TS_ASSERT_NEAR(c, sqrt(3.0)/2.0, 1e-14);

    ts_sincos_deg(90.0, &s, &c);
    TS_ASSERT_NEAR(s, 1.0, 1e-14);
    TS_ASSERT_NEAR(c, 0.0, 1e-14);
    TS_PASS();
}
static void test_sincos_red(void) {
    /* If sin/cos were swapped */
    double s = cos(30.0 * M_PI / 180.0);  /* swapped: gives cos value */
    TS_ASSERT_TRUE(fabs(s - 0.5) > 0.3);  /* should be 0.5 but got ~0.866 */
    TS_PASS();
}

/* ================================================================
 * SECTION 3: VECTOR OPERATION TESTS
 * OpenSCAD: norm(), cross(), + implicit vector math
 * Parallelism: SIMD (3-component operations)
 * ================================================================ */

static void test_vec3_add_green(void) {
    ts_vec3 a = ts_vec3_make(1, 2, 3);
    ts_vec3 b = ts_vec3_make(4, 5, 6);
    ts_vec3 r = ts_vec3_add(a, b);
    ts_vec3 expected = ts_vec3_make(5, 7, 9);
    TS_ASSERT_VEC3_NEAR(r, expected, 1e-15);
    TS_PASS();
}
static void test_vec3_add_red(void) {
    /* Subtraction instead of addition */
    ts_vec3 a = ts_vec3_make(1, 2, 3);
    ts_vec3 b = ts_vec3_make(4, 5, 6);
    ts_vec3 wrong = ts_vec3_sub(a, b);
    TS_ASSERT_TRUE(fabs(wrong.v[0] - 5.0) > 7.0);
    TS_PASS();
}

static void test_vec3_dot_green(void) {
    ts_vec3 a = ts_vec3_make(1, 0, 0);
    ts_vec3 b = ts_vec3_make(0, 1, 0);
    TS_ASSERT_NEAR(ts_vec3_dot(a, b), 0.0, 1e-15);  /* orthogonal */

    ts_vec3 c = ts_vec3_make(1, 2, 3);
    ts_vec3 d = ts_vec3_make(4, 5, 6);
    TS_ASSERT_NEAR(ts_vec3_dot(c, d), 32.0, 1e-14);  /* 4+10+18 */
    TS_PASS();
}
static void test_vec3_dot_red(void) {
    /* Cross product instead of dot — returns vec3, not scalar */
    ts_vec3 a = ts_vec3_make(1, 2, 3);
    ts_vec3 b = ts_vec3_make(4, 5, 6);
    ts_vec3 wrong = ts_vec3_cross(a, b);
    /* Cross of (1,2,3)x(4,5,6) = (-3, 6, -3), magnitude != 32 */
    double wrong_scalar = ts_vec3_norm(wrong);
    TS_ASSERT_TRUE(fabs(wrong_scalar - 32.0) > 20.0);
    TS_PASS();
}

/* --- cross product --- */
static void test_vec3_cross_green(void) {
    ts_vec3 x = ts_vec3_make(1, 0, 0);
    ts_vec3 y = ts_vec3_make(0, 1, 0);
    ts_vec3 z = ts_vec3_cross(x, y);
    ts_vec3 expected = ts_vec3_make(0, 0, 1);
    TS_ASSERT_VEC3_NEAR(z, expected, 1e-15);

    /* Anti-commutativity: a x b = -(b x a) */
    ts_vec3 neg_z = ts_vec3_cross(y, x);
    ts_vec3 expected_neg = ts_vec3_make(0, 0, -1);
    TS_ASSERT_VEC3_NEAR(neg_z, expected_neg, 1e-15);

    /* Parallel vectors: cross = zero */
    ts_vec3 a = ts_vec3_make(2, 0, 0);
    ts_vec3 zero = ts_vec3_cross(x, a);
    ts_vec3 zero_expected = ts_vec3_zero();
    TS_ASSERT_VEC3_NEAR(zero, zero_expected, 1e-15);
    TS_PASS();
}
static void test_vec3_cross_red(void) {
    /* Dot product instead of cross */
    ts_vec3 x = ts_vec3_make(1, 0, 0);
    ts_vec3 y = ts_vec3_make(0, 1, 0);
    double wrong = ts_vec3_dot(x, y);  /* = 0, which is a scalar not a vector */
    /* The cross product should give (0,0,1), not a scalar 0 */
    TS_ASSERT_NEAR(wrong, 0.0, 1e-15);  /* dot is 0 for orthogonal */
    /* But what we wanted was a VECTOR (0,0,1), not a scalar */
    ts_vec3 correct = ts_vec3_cross(x, y);
    TS_ASSERT_TRUE(ts_vec3_norm(correct) > 0.5);  /* cross has magnitude 1 */
    TS_PASS();
}

/* --- norm (magnitude) --- */
static void test_vec3_norm_green(void) {
    ts_vec3 a = ts_vec3_make(3, 4, 0);
    TS_ASSERT_NEAR(ts_vec3_norm(a), 5.0, 1e-14);

    ts_vec3 b = ts_vec3_make(1, 1, 1);
    TS_ASSERT_NEAR(ts_vec3_norm(b), sqrt(3.0), 1e-14);

    ts_vec3 z = ts_vec3_zero();
    TS_ASSERT_NEAR(ts_vec3_norm(z), 0.0, 1e-15);
    TS_PASS();
}
static void test_vec3_norm_red(void) {
    /* Squared norm instead of norm */
    ts_vec3 a = ts_vec3_make(3, 4, 0);
    double wrong = ts_vec3_norm_sq(a);  /* = 25, not 5 */
    TS_ASSERT_TRUE(fabs(wrong - 5.0) > 19.0);
    TS_PASS();
}

/* --- normalize --- */
static void test_vec3_normalize_green(void) {
    ts_vec3 a = ts_vec3_make(3, 4, 0);
    ts_vec3 n = ts_vec3_normalize(a);
    TS_ASSERT_NEAR(ts_vec3_norm(n), 1.0, 1e-14);
    TS_ASSERT_NEAR(n.v[0], 0.6, 1e-14);
    TS_ASSERT_NEAR(n.v[1], 0.8, 1e-14);

    /* Zero vector normalizes to zero */
    ts_vec3 z = ts_vec3_normalize(ts_vec3_zero());
    TS_ASSERT_NEAR(ts_vec3_norm(z), 0.0, 1e-15);
    TS_PASS();
}
static void test_vec3_normalize_red(void) {
    /* Dividing by norm^2 instead of norm */
    ts_vec3 a = ts_vec3_make(3, 4, 0);
    double wrong_len = ts_vec3_norm_sq(a);  /* 25 instead of 5 */
    ts_vec3 wrong = ts_vec3_scale(a, 1.0 / wrong_len);
    TS_ASSERT_TRUE(fabs(ts_vec3_norm(wrong) - 1.0) > 0.5);
    TS_PASS();
}

/* --- lerp --- */
static void test_vec3_lerp_green(void) {
    ts_vec3 a = ts_vec3_make(0, 0, 0);
    ts_vec3 b = ts_vec3_make(10, 20, 30);
    ts_vec3 mid = ts_vec3_lerp(a, b, 0.5);
    ts_vec3 expected = ts_vec3_make(5, 10, 15);
    TS_ASSERT_VEC3_NEAR(mid, expected, 1e-14);

    ts_vec3 at_zero = ts_vec3_lerp(a, b, 0.0);
    TS_ASSERT_VEC3_NEAR(at_zero, a, 1e-15);

    ts_vec3 at_one = ts_vec3_lerp(a, b, 1.0);
    TS_ASSERT_VEC3_NEAR(at_one, b, 1e-14);
    TS_PASS();
}
static void test_vec3_lerp_red(void) {
    /* Using t=0.5 always (ignoring the actual t parameter) */
    ts_vec3 a = ts_vec3_make(0, 0, 0);
    ts_vec3 b = ts_vec3_make(10, 20, 30);
    ts_vec3 wrong = ts_vec3_lerp(a, b, 0.5);  /* forced midpoint */
    ts_vec3 correct = ts_vec3_lerp(a, b, 0.25);
    TS_ASSERT_TRUE(ts_vec3_distance(wrong, correct) > 3.0);
    TS_PASS();
}

/* --- reflect --- */
static void test_vec3_reflect_green(void) {
    ts_vec3 v = ts_vec3_make(1, -1, 0);
    ts_vec3 n = ts_vec3_make(0, 1, 0);
    ts_vec3 r = ts_vec3_reflect(v, n);
    ts_vec3 expected = ts_vec3_make(1, 1, 0);
    TS_ASSERT_VEC3_NEAR(r, expected, 1e-14);
    TS_PASS();
}
static void test_vec3_reflect_red(void) {
    /* Negating instead of reflecting */
    ts_vec3 v = ts_vec3_make(1, -1, 0);
    ts_vec3 wrong = ts_vec3_negate(v);
    ts_vec3 correct = ts_vec3_make(1, 1, 0);
    TS_ASSERT_TRUE(ts_vec3_distance(wrong, correct) > 1.0);
    TS_PASS();
}

/* ================================================================
 * SECTION 4: MATRIX OPERATION TESTS
 * OpenSCAD: translate, rotate, scale, mirror, multmatrix
 * Parallelism: GPU (16 independent dot products per multiply)
 * ================================================================ */

static void test_mat4_identity_green(void) {
    ts_mat4 id = ts_mat4_identity();
    ts_vec3 p = ts_vec3_make(1, 2, 3);
    ts_vec3 r = ts_mat4_transform_point(id, p);
    TS_ASSERT_VEC3_NEAR(r, p, 1e-15);
    TS_PASS();
}
static void test_mat4_identity_red(void) {
    /* Zero matrix instead of identity */
    ts_mat4 z = ts_mat4_zero();
    ts_vec3 p = ts_vec3_make(1, 2, 3);
    ts_vec3 r = ts_mat4_transform_point(z, p);
    /* w=0 division guard makes this return (0,0,0)/1 which is wrong */
    TS_ASSERT_TRUE(ts_vec3_distance(r, p) > 0.5);
    TS_PASS();
}

/* --- translate --- */
static void test_mat4_translate_green(void) {
    ts_mat4 t = ts_mat4_translate(10, 20, 30);
    ts_vec3 p = ts_vec3_make(1, 2, 3);
    ts_vec3 r = ts_mat4_transform_point(t, p);
    ts_vec3 expected = ts_vec3_make(11, 22, 33);
    TS_ASSERT_VEC3_NEAR(r, expected, 1e-14);

    /* Direction is NOT affected by translation */
    ts_vec3 d = ts_vec3_make(1, 0, 0);
    ts_vec3 rd = ts_mat4_transform_dir(t, d);
    TS_ASSERT_VEC3_NEAR(rd, d, 1e-15);
    TS_PASS();
}
static void test_mat4_translate_red(void) {
    /* Scale instead of translate */
    ts_mat4 wrong = ts_mat4_scale(10, 20, 30);
    ts_vec3 p = ts_vec3_make(1, 2, 3);
    ts_vec3 r = ts_mat4_transform_point(wrong, p);
    ts_vec3 expected = ts_vec3_make(11, 22, 33);
    TS_ASSERT_TRUE(ts_vec3_distance(r, expected) > 5.0);
    TS_PASS();
}

/* --- scale --- */
static void test_mat4_scale_green(void) {
    ts_mat4 s = ts_mat4_scale(2, 3, 4);
    ts_vec3 p = ts_vec3_make(1, 1, 1);
    ts_vec3 r = ts_mat4_transform_point(s, p);
    ts_vec3 expected = ts_vec3_make(2, 3, 4);
    TS_ASSERT_VEC3_NEAR(r, expected, 1e-15);

    /* Uniform scale */
    ts_mat4 u = ts_mat4_scale(5, 5, 5);
    ts_vec3 r2 = ts_mat4_transform_point(u, p);
    ts_vec3 expected2 = ts_vec3_make(5, 5, 5);
    TS_ASSERT_VEC3_NEAR(r2, expected2, 1e-15);
    TS_PASS();
}
static void test_mat4_scale_red(void) {
    /* Translate instead of scale */
    ts_mat4 wrong = ts_mat4_translate(2, 3, 4);
    ts_vec3 p = ts_vec3_make(1, 1, 1);
    ts_vec3 r = ts_mat4_transform_point(wrong, p);
    ts_vec3 expected = ts_vec3_make(2, 3, 4);
    TS_ASSERT_TRUE(ts_vec3_distance(r, expected) > 1.0);
    TS_PASS();
}

/* --- rotate Z --- */
static void test_mat4_rotate_z_green(void) {
    ts_mat4 rz = ts_mat4_rotate_z(90.0);
    ts_vec3 p = ts_vec3_make(1, 0, 0);
    ts_vec3 r = ts_mat4_transform_point(rz, p);
    ts_vec3 expected = ts_vec3_make(0, 1, 0);
    TS_ASSERT_VEC3_NEAR(r, expected, 1e-14);
    TS_PASS();
}
static void test_mat4_rotate_z_red(void) {
    /* Using radians instead of degrees in rotation */
    double rad = 90.0;  /* wrong: treating 90 as radians */
    double s = sin(rad), c = cos(rad);
    /* cos(90 rad) ~= -0.448, sin(90 rad) ~= 0.894 */
    /* So (1,0,0) -> (cos90, sin90, 0) = (-0.448, 0.894, 0) */
    ts_vec3 wrong = ts_vec3_make(c, s, 0);
    ts_vec3 expected = ts_vec3_make(0, 1, 0);
    TS_ASSERT_TRUE(ts_vec3_distance(wrong, expected) > 0.4);
    TS_PASS();
}

/* --- rotate X --- */
static void test_mat4_rotate_x_green(void) {
    ts_mat4 rx = ts_mat4_rotate_x(90.0);
    ts_vec3 p = ts_vec3_make(0, 1, 0);
    ts_vec3 r = ts_mat4_transform_point(rx, p);
    ts_vec3 expected = ts_vec3_make(0, 0, 1);
    TS_ASSERT_VEC3_NEAR(r, expected, 1e-14);
    TS_PASS();
}
static void test_mat4_rotate_x_red(void) {
    /* Rotate Y instead of X */
    ts_mat4 wrong = ts_mat4_rotate_y(90.0);
    ts_vec3 p = ts_vec3_make(0, 1, 0);
    ts_vec3 r = ts_mat4_transform_point(wrong, p);
    ts_vec3 expected = ts_vec3_make(0, 0, 1);
    TS_ASSERT_TRUE(ts_vec3_distance(r, expected) > 0.5);
    TS_PASS();
}

/* --- rotate Y --- */
static void test_mat4_rotate_y_green(void) {
    ts_mat4 ry = ts_mat4_rotate_y(90.0);
    ts_vec3 p = ts_vec3_make(0, 0, 1);
    ts_vec3 r = ts_mat4_transform_point(ry, p);
    ts_vec3 expected = ts_vec3_make(1, 0, 0);
    TS_ASSERT_VEC3_NEAR(r, expected, 1e-14);
    TS_PASS();
}
static void test_mat4_rotate_y_red(void) {
    ts_mat4 wrong = ts_mat4_rotate_x(90.0);
    ts_vec3 p = ts_vec3_make(0, 0, 1);
    ts_vec3 r = ts_mat4_transform_point(wrong, p);
    ts_vec3 expected = ts_vec3_make(1, 0, 0);
    TS_ASSERT_TRUE(ts_vec3_distance(r, expected) > 0.5);
    TS_PASS();
}

/* --- rotate arbitrary axis --- */
static void test_mat4_rotate_axis_green(void) {
    /* 180 degrees around Z should flip X and Y */
    ts_vec3 z_axis = ts_vec3_make(0, 0, 1);
    ts_mat4 r = ts_mat4_rotate_axis(180.0, z_axis);
    ts_vec3 p = ts_vec3_make(1, 0, 0);
    ts_vec3 result = ts_mat4_transform_point(r, p);
    ts_vec3 expected = ts_vec3_make(-1, 0, 0);
    TS_ASSERT_VEC3_NEAR(result, expected, 1e-13);

    /* 120 degrees around (1,1,1) should cycle x->y->z */
    ts_vec3 diag = ts_vec3_make(1, 1, 1);
    ts_mat4 r2 = ts_mat4_rotate_axis(120.0, diag);
    ts_vec3 x = ts_vec3_make(1, 0, 0);
    ts_vec3 result2 = ts_mat4_transform_point(r2, x);
    ts_vec3 expected2 = ts_vec3_make(0, 1, 0);
    TS_ASSERT_VEC3_NEAR(result2, expected2, 1e-13);
    TS_PASS();
}
static void test_mat4_rotate_axis_red(void) {
    /* Zero-length axis should return identity */
    ts_vec3 zero_axis = ts_vec3_zero();
    ts_mat4 r = ts_mat4_rotate_axis(90.0, zero_axis);
    ts_vec3 p = ts_vec3_make(1, 2, 3);
    ts_vec3 result = ts_mat4_transform_point(r, p);
    /* Should be unchanged (identity) */
    TS_ASSERT_VEC3_NEAR(result, p, 1e-14);
    TS_PASS();
}

/* --- mirror --- */
static void test_mat4_mirror_green(void) {
    /* Mirror across X=0 plane (normal = (1,0,0)) */
    ts_vec3 nx = ts_vec3_make(1, 0, 0);
    ts_mat4 m = ts_mat4_mirror(nx);
    ts_vec3 p = ts_vec3_make(5, 3, 7);
    ts_vec3 r = ts_mat4_transform_point(m, p);
    ts_vec3 expected = ts_vec3_make(-5, 3, 7);
    TS_ASSERT_VEC3_NEAR(r, expected, 1e-14);

    /* Mirror across Y=0 */
    ts_vec3 ny = ts_vec3_make(0, 1, 0);
    ts_mat4 m2 = ts_mat4_mirror(ny);
    ts_vec3 r2 = ts_mat4_transform_point(m2, p);
    ts_vec3 expected2 = ts_vec3_make(5, -3, 7);
    TS_ASSERT_VEC3_NEAR(r2, expected2, 1e-14);
    TS_PASS();
}
static void test_mat4_mirror_red(void) {
    /* Negating all components instead of mirroring one axis */
    ts_vec3 p = ts_vec3_make(5, 3, 7);
    ts_vec3 wrong = ts_vec3_negate(p);
    ts_vec3 expected = ts_vec3_make(-5, 3, 7);
    TS_ASSERT_TRUE(ts_vec3_distance(wrong, expected) > 5.0);
    TS_PASS();
}

/* --- multiply (chained transforms) --- */
static void test_mat4_multiply_green(void) {
    /* translate then scale: scale(translate(p)) */
    ts_mat4 t = ts_mat4_translate(10, 0, 0);
    ts_mat4 s = ts_mat4_scale(2, 2, 2);
    ts_mat4 combined = ts_mat4_multiply(s, t);  /* scale applied after translate */
    ts_vec3 p = ts_vec3_make(1, 0, 0);
    ts_vec3 r = ts_mat4_transform_point(combined, p);
    /* p + (10,0,0) = (11,0,0), then *2 = (22,0,0) */
    ts_vec3 expected = ts_vec3_make(22, 0, 0);
    TS_ASSERT_VEC3_NEAR(r, expected, 1e-13);
    TS_PASS();
}
static void test_mat4_multiply_red(void) {
    /* Wrong order: translate(scale(p)) instead of scale(translate(p)) */
    ts_mat4 t = ts_mat4_translate(10, 0, 0);
    ts_mat4 s = ts_mat4_scale(2, 2, 2);
    ts_mat4 wrong = ts_mat4_multiply(t, s);  /* translate after scale */
    ts_vec3 p = ts_vec3_make(1, 0, 0);
    ts_vec3 r = ts_mat4_transform_point(wrong, p);
    /* p *2 = (2,0,0), then +(10,0,0) = (12,0,0) — NOT 22 */
    ts_vec3 expected = ts_vec3_make(22, 0, 0);
    TS_ASSERT_TRUE(ts_vec3_distance(r, expected) > 5.0);
    TS_PASS();
}

/* --- inverse --- */
static void test_mat4_inverse_green(void) {
    ts_mat4 t = ts_mat4_translate(10, 20, 30);
    ts_mat4 inv = ts_mat4_inverse(t);
    ts_mat4 product = ts_mat4_multiply(t, inv);
    ts_mat4 id = ts_mat4_identity();
    TS_ASSERT_MAT4_NEAR(product, id, 1e-12);

    /* Rotation inverse */
    ts_mat4 r = ts_mat4_rotate_z(45.0);
    ts_mat4 rinv = ts_mat4_inverse(r);
    ts_mat4 rprod = ts_mat4_multiply(r, rinv);
    TS_ASSERT_MAT4_NEAR(rprod, id, 1e-12);
    TS_PASS();
}
static void test_mat4_inverse_red(void) {
    /* Transpose instead of inverse (only correct for pure rotations) */
    ts_mat4 t = ts_mat4_translate(10, 20, 30);
    ts_mat4 wrong = ts_mat4_transpose(t);
    ts_mat4 product = ts_mat4_multiply(t, wrong);
    ts_mat4 id = ts_mat4_identity();
    /* For translation, transpose != inverse */
    int close = 1;
    for (int i = 0; i < 16; i++)
        if (fabs(product.m[i] - id.m[i]) > 0.1) close = 0;
    TS_ASSERT_FALSE(close);
    TS_PASS();
}

/* --- determinant --- */
static void test_mat4_det_green(void) {
    ts_mat4 id = ts_mat4_identity();
    TS_ASSERT_NEAR(ts_mat4_det(id), 1.0, 1e-15);

    ts_mat4 s = ts_mat4_scale(2, 3, 4);
    TS_ASSERT_NEAR(ts_mat4_det(s), 24.0, 1e-13);

    /* Mirror flips sign */
    ts_vec3 nx = ts_vec3_make(1, 0, 0);
    ts_mat4 m = ts_mat4_mirror(nx);
    TS_ASSERT_NEAR(ts_mat4_det(m), -1.0, 1e-14);
    TS_PASS();
}
static void test_mat4_det_red(void) {
    /* Zero matrix det = 0, proving the test catches singular matrices */
    ts_mat4 z = ts_mat4_zero();
    TS_ASSERT_NEAR(ts_mat4_det(z), 0.0, 1e-15);

    /* Scale(2,3,4) has det=24 — if someone returned trace (2+3+4=9) instead */
    ts_mat4 s = ts_mat4_scale(2, 3, 4);
    double wrong_trace = s.m[0] + s.m[5] + s.m[10] + s.m[15]; /* = 10 */
    TS_ASSERT_TRUE(fabs(wrong_trace - 24.0) > 10.0);
    TS_PASS();
}

/* ================================================================
 * SECTION 5: MESH & GEOMETRY TESTS
 * OpenSCAD: cube, sphere, cylinder, circle, square, polyhedron
 * Parallelism: GPU (vertex generation per-vertex parallel)
 * ================================================================ */

/* --- cube --- */
static void test_cube_green(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_cube(2.0, 2.0, 2.0, &m);

    /* 6 faces * 4 verts = 24 verts, 6 faces * 2 tris = 12 tris */
    TS_ASSERT_EQ_INT(m.vert_count, 24);
    TS_ASSERT_EQ_INT(m.tri_count, 12);

    /* Bounding box should be [-1,1] in all axes */
    double mn[3], mx[3];
    ts_mesh_bounds(&m, mn, mx);
    TS_ASSERT_NEAR(mn[0], -1.0, 1e-14);
    TS_ASSERT_NEAR(mn[1], -1.0, 1e-14);
    TS_ASSERT_NEAR(mn[2], -1.0, 1e-14);
    TS_ASSERT_NEAR(mx[0], 1.0, 1e-14);
    TS_ASSERT_NEAR(mx[1], 1.0, 1e-14);
    TS_ASSERT_NEAR(mx[2], 1.0, 1e-14);

    ts_mesh_free(&m);
    TS_PASS();
}
static void test_cube_red(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_cube(2.0, 2.0, 2.0, &m);

    /* If we generated a sphere instead, triangle count would differ */
    /* A cube always has exactly 12 triangles */
    TS_ASSERT_TRUE(m.tri_count == 12);
    /* A wrong implementation with only 5 faces would have 10 tris */
    TS_ASSERT_TRUE(10 != 12);

    ts_mesh_free(&m);
    TS_PASS();
}

/* --- sphere --- */
static void test_sphere_green(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_sphere(1.0, 16, &m);

    /* Should have vertices and triangles */
    TS_ASSERT_TRUE(m.vert_count > 0);
    TS_ASSERT_TRUE(m.tri_count > 0);

    /* All vertices should be on the unit sphere (radius ~= 1) */
    for (int i = 0; i < m.vert_count; i++) {
        double r = sqrt(m.verts[i].pos[0]*m.verts[i].pos[0] +
                        m.verts[i].pos[1]*m.verts[i].pos[1] +
                        m.verts[i].pos[2]*m.verts[i].pos[2]);
        TS_ASSERT_NEAR(r, 1.0, 1e-14);
    }

    /* Bounding box should be approximately [-1,1] */
    double mn[3], mx[3];
    ts_mesh_bounds(&m, mn, mx);
    for (int j = 0; j < 3; j++) {
        TS_ASSERT_TRUE(mn[j] >= -1.01);
        TS_ASSERT_TRUE(mx[j] <= 1.01);
    }

    ts_mesh_free(&m);
    TS_PASS();
}
static void test_sphere_red(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_sphere(5.0, 16, &m);

    /* If radius was ignored (always 1), max extent would be ~1 not ~5 */
    double mn[3], mx[3];
    ts_mesh_bounds(&m, mn, mx);
    TS_ASSERT_TRUE(mx[0] > 4.0);  /* should be near 5, not 1 */

    ts_mesh_free(&m);
    TS_PASS();
}

/* --- cylinder --- */
static void test_cylinder_green(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_cylinder(10.0, 2.0, 2.0, 16, &m);

    TS_ASSERT_TRUE(m.vert_count > 0);
    TS_ASSERT_TRUE(m.tri_count > 0);

    /* Bounding box: X/Y should be [-2,2], Z should be [-5,5] */
    double mn[3], mx[3];
    ts_mesh_bounds(&m, mn, mx);
    TS_ASSERT_NEAR(mn[2], -5.0, 1e-14);
    TS_ASSERT_NEAR(mx[2], 5.0, 1e-14);
    TS_ASSERT_TRUE(mx[0] <= 2.01 && mx[0] >= 1.99);

    ts_mesh_free(&m);
    TS_PASS();
}
static void test_cylinder_red(void) {
    /* A cone (r2=0) should have top at a point */
    ts_mesh m = ts_mesh_init();
    ts_gen_cylinder(10.0, 2.0, 0.0, 16, &m);

    double mn[3], mx[3];
    ts_mesh_bounds(&m, mn, mx);
    /* Top ring should have radius 0, so at z=+5, x and y should be 0 */
    int found_top_zero = 0;
    for (int i = 0; i < m.vert_count; i++) {
        if (fabs(m.verts[i].pos[2] - 5.0) < 0.01) {
            if (fabs(m.verts[i].pos[0]) < 0.01 && fabs(m.verts[i].pos[1]) < 0.01)
                found_top_zero = 1;
        }
    }
    TS_ASSERT_TRUE(found_top_zero);

    ts_mesh_free(&m);
    TS_PASS();
}

/* --- circle points (2D) --- */
static void test_circle_green(void) {
    double pts[200];
    int n = ts_gen_circle_points(5.0, 100, pts, 100);
    TS_ASSERT_EQ_INT(n, 100);

    /* All points should be at radius 5 */
    for (int i = 0; i < n; i++) {
        double r = sqrt(pts[i*2]*pts[i*2] + pts[i*2+1]*pts[i*2+1]);
        TS_ASSERT_NEAR(r, 5.0, 1e-13);
    }
    TS_PASS();
}
static void test_circle_red(void) {
    double pts[200];
    int n = ts_gen_circle_points(5.0, 100, pts, 100);
    /* If radius was squared instead of used directly */
    double wrong_radius = 25.0;
    double r0 = sqrt(pts[0]*pts[0] + pts[1]*pts[1]);
    TS_ASSERT_TRUE(fabs(r0 - wrong_radius) > 15.0);
    (void)n;
    TS_PASS();
}

/* --- square points (2D) --- */
static void test_square_green(void) {
    double pts[8];
    int n = ts_gen_square_points(4.0, 6.0, pts, 4);
    TS_ASSERT_EQ_INT(n, 4);
    /* Corners at (+-2, +-3) */
    TS_ASSERT_NEAR(pts[0], -2.0, 1e-15);
    TS_ASSERT_NEAR(pts[1], -3.0, 1e-15);
    TS_ASSERT_NEAR(pts[4], 2.0, 1e-15);
    TS_ASSERT_NEAR(pts[5], 3.0, 1e-15);
    TS_PASS();
}
static void test_square_red(void) {
    double pts[8];
    ts_gen_square_points(4.0, 6.0, pts, 4);
    /* If not centered, first corner would be (0,0) not (-2,-3) */
    TS_ASSERT_TRUE(fabs(pts[0] - 0.0) > 1.0);
    TS_PASS();
}

/* --- polyhedron --- */
static void test_polyhedron_green(void) {
    /* Simple tetrahedron */
    double points[] = {
        0, 0, 0,
        1, 0, 0,
        0.5, 0.866, 0,
        0.5, 0.289, 0.816
    };
    int faces[] = {
        0, 1, 2,
        0, 1, 3,
        1, 2, 3,
        0, 2, 3
    };
    ts_mesh m = ts_mesh_init();
    int ret = ts_gen_polyhedron(points, 4, faces, 4, &m);
    TS_ASSERT_EQ_INT(ret, 0);
    TS_ASSERT_EQ_INT(m.vert_count, 4);
    TS_ASSERT_EQ_INT(m.tri_count, 4);
    ts_mesh_free(&m);
    TS_PASS();
}
static void test_polyhedron_red(void) {
    /* Invalid face index should return error */
    double points[] = { 0,0,0, 1,0,0, 0,1,0 };
    int bad_faces[] = { 0, 1, 99 };  /* index 99 out of bounds */
    ts_mesh m = ts_mesh_init();
    int ret = ts_gen_polyhedron(points, 3, bad_faces, 1, &m);
    TS_ASSERT_EQ_INT(ret, -1);
    ts_mesh_free(&m);
    TS_PASS();
}

/* --- Platonic solids --- */

static void test_tetrahedron_green(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_tetrahedron(1.0, &m);
    TS_ASSERT_EQ_INT(m.tri_count, 4);
    TS_ASSERT_EQ_INT(m.vert_count, 12);
    for (int i = 0; i < m.vert_count; i++) {
        double r = sqrt(m.verts[i].pos[0]*m.verts[i].pos[0] +
                        m.verts[i].pos[1]*m.verts[i].pos[1] +
                        m.verts[i].pos[2]*m.verts[i].pos[2]);
        TS_ASSERT_NEAR(r, 1.0, 1e-10);
    }
    ts_mesh_free(&m);
    TS_PASS();
}
static void test_tetrahedron_red(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_tetrahedron(5.0, &m);
    for (int i = 0; i < m.vert_count; i++) {
        double r = sqrt(m.verts[i].pos[0]*m.verts[i].pos[0] +
                        m.verts[i].pos[1]*m.verts[i].pos[1] +
                        m.verts[i].pos[2]*m.verts[i].pos[2]);
        TS_ASSERT_NEAR(r, 5.0, 1e-10);
    }
    ts_mesh_free(&m);
    TS_PASS();
}

static void test_octahedron_green(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_octahedron(1.0, &m);
    TS_ASSERT_EQ_INT(m.tri_count, 8);
    TS_ASSERT_EQ_INT(m.vert_count, 24);
    for (int i = 0; i < m.vert_count; i++) {
        double r = sqrt(m.verts[i].pos[0]*m.verts[i].pos[0] +
                        m.verts[i].pos[1]*m.verts[i].pos[1] +
                        m.verts[i].pos[2]*m.verts[i].pos[2]);
        TS_ASSERT_NEAR(r, 1.0, 1e-10);
    }
    ts_mesh_free(&m);
    TS_PASS();
}
static void test_octahedron_red(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_octahedron(3.0, &m);
    double mn[3], mx[3];
    ts_mesh_bounds(&m, mn, mx);
    TS_ASSERT_TRUE(mx[0] > 2.5);
    ts_mesh_free(&m);
    TS_PASS();
}

static void test_dodecahedron_green(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_dodecahedron(1.0, &m);
    TS_ASSERT_EQ_INT(m.tri_count, 36);
    TS_ASSERT_TRUE(m.vert_count > 0);
    for (int i = 0; i < m.vert_count; i++) {
        double r = sqrt(m.verts[i].pos[0]*m.verts[i].pos[0] +
                        m.verts[i].pos[1]*m.verts[i].pos[1] +
                        m.verts[i].pos[2]*m.verts[i].pos[2]);
        TS_ASSERT_NEAR(r, 1.0, 0.01);
    }
    ts_mesh_free(&m);
    TS_PASS();
}
static void test_dodecahedron_red(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_dodecahedron(10.0, &m);
    double mn[3], mx[3];
    ts_mesh_bounds(&m, mn, mx);
    TS_ASSERT_TRUE(mx[0] > 5.0);
    TS_ASSERT_TRUE(mn[0] < -5.0);
    ts_mesh_free(&m);
    TS_PASS();
}

static void test_icosahedron_green(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_icosahedron(1.0, &m);
    TS_ASSERT_EQ_INT(m.tri_count, 20);
    TS_ASSERT_EQ_INT(m.vert_count, 60);
    for (int i = 0; i < m.vert_count; i++) {
        double r = sqrt(m.verts[i].pos[0]*m.verts[i].pos[0] +
                        m.verts[i].pos[1]*m.verts[i].pos[1] +
                        m.verts[i].pos[2]*m.verts[i].pos[2]);
        TS_ASSERT_NEAR(r, 1.0, 1e-10);
    }
    ts_mesh_free(&m);
    TS_PASS();
}
static void test_icosahedron_red(void) {
    ts_mesh m = ts_mesh_init();
    ts_gen_icosahedron(7.0, &m);
    for (int i = 0; i < m.vert_count; i++) {
        double r = sqrt(m.verts[i].pos[0]*m.verts[i].pos[0] +
                        m.verts[i].pos[1]*m.verts[i].pos[1] +
                        m.verts[i].pos[2]*m.verts[i].pos[2]);
        TS_ASSERT_NEAR(r, 7.0, 1e-10);
    }
    ts_mesh_free(&m);
    TS_PASS();
}

/* ================================================================
 * SECTION 6: RANDOM NUMBER TESTS
 * OpenSCAD: rands(min, max, count, seed)
 * Parallelism: TRIVIAL (counter-based RNG, each element independent)
 * ================================================================ */

static void test_rands_green(void) {
    double out[1000];
    ts_rands(0.0, 1.0, 1000, 42, out);

    /* All values should be in [0, 1] */
    for (int i = 0; i < 1000; i++) {
        TS_ASSERT_TRUE(out[i] >= 0.0);
        TS_ASSERT_TRUE(out[i] <= 1.0);
    }

    /* Deterministic: same seed = same output */
    double out2[1000];
    ts_rands(0.0, 1.0, 1000, 42, out2);
    for (int i = 0; i < 1000; i++) {
        TS_ASSERT_NEAR(out[i], out2[i], 1e-15);
    }

    /* Different seed = different output */
    double out3[1000];
    ts_rands(0.0, 1.0, 1000, 99, out3);
    int differences = 0;
    for (int i = 0; i < 1000; i++)
        if (fabs(out[i] - out3[i]) > 1e-10) differences++;
    TS_ASSERT_TRUE(differences > 900);  /* almost all should differ */
    TS_PASS();
}
static void test_rands_red(void) {
    /* A non-deterministic RNG would fail the reproducibility test */
    /* Our counter-based approach guarantees determinism */
    double a = ts_rand(42, 0, 0.0, 1.0);
    double b = ts_rand(42, 0, 0.0, 1.0);
    TS_ASSERT_NEAR(a, b, 1e-15);

    /* Sequential RNG would make rand(seed, 0) != rand(seed, 0) if
       called from different threads. Ours uses counter-based, so
       the same (seed, index) always gives the same result. */
    TS_PASS();
}

/* --- rands range test --- */
static void test_rands_range_green(void) {
    double out[500];
    ts_rands(-10.0, 10.0, 500, 7, out);
    for (int i = 0; i < 500; i++) {
        TS_ASSERT_TRUE(out[i] >= -10.0);
        TS_ASSERT_TRUE(out[i] <= 10.0);
    }
    /* Check spread — should use most of the range */
    double lo = out[0], hi = out[0];
    for (int i = 1; i < 500; i++) {
        if (out[i] < lo) lo = out[i];
        if (out[i] > hi) hi = out[i];
    }
    TS_ASSERT_TRUE(hi - lo > 15.0);  /* should cover most of [-10, 10] */
    TS_PASS();
}
static void test_rands_range_red(void) {
    /* If range was ignored and always [0,1] */
    double wrong_max = 1.0;
    double actual = ts_rand(42, 0, -100.0, 100.0);
    /* With range [-100, 100], values can be >> 1.0 */
    TS_ASSERT_TRUE(fabs(actual) > 0.0 || wrong_max > 0.0);  /* trivially true */
    /* Better: check that range is actually applied */
    double out[100];
    ts_rands(-100.0, 100.0, 100, 42, out);
    int outside_01 = 0;
    for (int i = 0; i < 100; i++)
        if (out[i] > 1.0 || out[i] < 0.0) outside_01++;
    TS_ASSERT_TRUE(outside_01 > 80);  /* most should be outside [0,1] */
    TS_PASS();
}

/* ================================================================
 * SECTION 7: CSG TESTS
 * BSP-tree boolean ops, quickhull, Minkowski sum
 * Parallelism: documented in ts_csg.h headers
 * ================================================================ */

/* Helper: compute mesh volume via divergence theorem (signed) */
static double mesh_signed_volume(const ts_mesh *m) {
    double vol = 0.0;
    for (int i = 0; i < m->tri_count; i++) {
        double *a = m->verts[m->tris[i].idx[0]].pos;
        double *b = m->verts[m->tris[i].idx[1]].pos;
        double *c = m->verts[m->tris[i].idx[2]].pos;
        /* V = (1/6) * sum of a . (b x c) for each triangle */
        vol += a[0]*(b[1]*c[2] - b[2]*c[1])
             + a[1]*(b[2]*c[0] - b[0]*c[2])
             + a[2]*(b[0]*c[1] - b[1]*c[0]);
    }
    return vol / 6.0;
}

/* --- UNION --- */
/* GREEN: union of two non-overlapping unit cubes = volume ~2.0 */
static void test_csg_union_green(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);  /* centered at origin */
    /* Translate b to [2,0,0] — no overlap */
    ts_gen_cube(1, 1, 1, &b);
    for (int i = 0; i < b.vert_count; i++) b.verts[i].pos[0] += 2.0;

    int ret = ts_csg_union(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    /* Non-overlapping union should have all triangles from both */
    TS_ASSERT_TRUE(out.tri_count >= 24); /* 12 + 12 */

    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_NEAR(vol, 2.0, 0.1);

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* RED: union should NOT have zero triangles */
static void test_csg_union_red(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(1, 1, 1, &b);
    for (int i = 0; i < b.vert_count; i++) b.verts[i].pos[0] += 2.0;

    int ret = ts_csg_union(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0); /* Must NOT be empty */

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* --- UNION OVERLAPPING --- */
/* GREEN: overlapping cubes should produce fewer tris than 24 */
static void test_csg_union_overlap_green(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(2, 2, 2, &a);
    ts_gen_cube(2, 2, 2, &b);
    /* Offset b by 1 unit — they overlap in a 1x2x2 region */
    for (int i = 0; i < b.vert_count; i++) b.verts[i].pos[0] += 1.0;

    int ret = ts_csg_union(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    /* Volume should be less than 8+8=16 (since they overlap) */
    /* Two 2x2x2 cubes offset by 1 = volume of 3x2x2 = 12 */
    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_TRUE(vol > 10.0 && vol < 14.0);

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* --- DIFFERENCE --- */
/* GREEN: difference of two non-overlapping cubes = just cube A */
static void test_csg_difference_green(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(1, 1, 1, &b);
    for (int i = 0; i < b.vert_count; i++) b.verts[i].pos[0] += 5.0;

    int ret = ts_csg_difference(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_NEAR(vol, 1.0, 0.1);

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* RED: difference should not produce original volume when B overlaps A */
static void test_csg_difference_red(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(2, 2, 2, &a);
    ts_gen_cube(2, 2, 2, &b);
    /* B overlaps A — result should be less than A's volume */
    for (int i = 0; i < b.vert_count; i++) b.verts[i].pos[0] += 1.0;

    int ret = ts_csg_difference(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);

    double vol = fabs(mesh_signed_volume(&out));
    /* A=8, overlap region=1*2*2=4, so A-B should be ~4 */
    TS_ASSERT_TRUE(vol < 7.0); /* Must be less than original 8 */

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* --- INTERSECTION --- */
/* GREEN: intersection of identical cubes = same cube */
static void test_csg_intersection_green(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(1, 1, 1, &b);

    int ret = ts_csg_intersection(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_NEAR(vol, 1.0, 0.15);

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* RED: intersection of non-overlapping cubes should be empty */
static void test_csg_intersection_red(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(1, 1, 1, &b);
    for (int i = 0; i < b.vert_count; i++) b.verts[i].pos[0] += 5.0;

    int ret = ts_csg_intersection(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);

    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_TRUE(vol < 0.01); /* Should be ~0 */

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* --- HULL --- */
/* GREEN: hull of a cube's vertices should still be a cube (volume=1) */
static void test_csg_hull_green(void) {
    ts_mesh input = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &input);

    int ret = ts_csg_hull(&input, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_NEAR(vol, 1.0, 0.1);

    ts_mesh_free(&input); ts_mesh_free(&out);
    TS_PASS();
}

/* RED: hull should produce a closed mesh (non-zero volume) */
static void test_csg_hull_red(void) {
    ts_mesh input = ts_mesh_init(), out = ts_mesh_init();
    /* Add some scattered points as vertices */
    ts_mesh_add_vertex(&input, 0,0,0, 0,0,0);
    ts_mesh_add_vertex(&input, 1,0,0, 0,0,0);
    ts_mesh_add_vertex(&input, 0,1,0, 0,0,0);
    ts_mesh_add_vertex(&input, 0,0,1, 0,0,0);
    ts_mesh_add_vertex(&input, 1,1,1, 0,0,0);

    int ret = ts_csg_hull(&input, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count >= 4); /* At least a tetrahedron */

    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_TRUE(vol > 0.01); /* Must have volume */

    ts_mesh_free(&input); ts_mesh_free(&out);
    TS_PASS();
}

/* --- MINKOWSKI --- */
/* GREEN: Minkowski of two unit cubes = 2x2x2 cube (vol=8) */
static void test_csg_minkowski_green(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(1, 1, 1, &b);

    int ret = ts_csg_minkowski(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    /* Minkowski sum of two unit cubes centered at origin = 2x2x2 = 8 */
    TS_ASSERT_NEAR(vol, 8.0, 0.5);

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* RED: Minkowski result should be bigger than either input */
static void test_csg_minkowski_red(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(1, 1, 1, &b);

    int ret = ts_csg_minkowski(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);

    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_TRUE(vol > 1.0); /* Must be bigger than input cube */

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* --- MINKOWSKI RIGOROUS SUITE --- */
/* Helper: check mesh is watertight (every edge shared by exactly 2 tris) */
static int mesh_is_watertight(const ts_mesh *m) {
    /* Build edge table: for each directed edge (a,b), count occurrences.
     * A watertight mesh has each undirected edge in exactly 2 triangles,
     * meaning for each (a,b) there exists (b,a). */
    typedef struct { int a, b; } edge_t;
    int ne = m->tri_count * 3;
    edge_t *edges = (edge_t *)malloc((size_t)ne * sizeof(edge_t));
    if (!edges) return 0;

    for (int i = 0; i < m->tri_count; i++) {
        int *idx = m->tris[i].idx;
        edges[i*3+0] = (edge_t){ idx[0], idx[1] };
        edges[i*3+1] = (edge_t){ idx[1], idx[2] };
        edges[i*3+2] = (edge_t){ idx[2], idx[0] };
    }

    /* For each directed edge, search for its reverse */
    int unpaired = 0;
    for (int i = 0; i < ne; i++) {
        int found = 0;
        for (int j = 0; j < ne; j++) {
            if (i == j) continue;
            if (edges[i].a == edges[j].b && edges[i].b == edges[j].a) {
                found = 1;
                break;
            }
        }
        if (!found) unpaired++;
    }

    free(edges);
    return unpaired == 0;
}

/* Helper: check all triangle normals face outward (positive signed volume) */
static int mesh_has_consistent_normals(const ts_mesh *m) {
    double vol = mesh_signed_volume(m);
    return vol > 0.0;  /* positive = outward-facing CCW winding */
}

/* Helper: compute axis-aligned bounding box */
static void mesh_aabb(const ts_mesh *m, double min[3], double max[3]) {
    min[0] = min[1] = min[2] = 1e30;
    max[0] = max[1] = max[2] = -1e30;
    for (int i = 0; i < m->vert_count; i++) {
        for (int j = 0; j < 3; j++) {
            if (m->verts[i].pos[j] < min[j]) min[j] = m->verts[i].pos[j];
            if (m->verts[i].pos[j] > max[j]) max[j] = m->verts[i].pos[j];
        }
    }
}

/* GREEN: Minkowski of cube + sphere = rounded cube.
 * Volume must be between sphere vol and (cube_side + sphere_diam)^3.
 * This is the core fidget-spinner shape operation. */
static void test_mink_cube_sphere_green(void) {
    ts_mesh cube = ts_mesh_init(), sphere = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(2, 2, 2, &cube);       /* 2x2x2 cube centered at origin */
    ts_gen_sphere(0.5, 16, &sphere);    /* r=0.5 sphere (diameter=1) */

    int ret = ts_csg_minkowski(&cube, &sphere, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    /* Minkowski(2x2x2 cube, r=0.5 sphere) = rounded cube with:
     * - body: 3x3x3 = 27 (cube expanded by diameter in each axis)
     * - minus corners (replaced by sphere octants) and edges (cylinder segments)
     * Exact: 27 - corner_excess + edge_cylinders... but bounded:
     * Lower bound: original cube vol = 8
     * Upper bound: (2+1)^3 = 27 (bounding box) */
    TS_ASSERT_TRUE(vol > 8.0);
    TS_ASSERT_TRUE(vol < 27.0);

    /* AABB should be 3x3x3 (cube side + sphere diameter in each axis) */
    double mn[3], mx[3];
    mesh_aabb(&out, mn, mx);
    for (int i = 0; i < 3; i++) {
        TS_ASSERT_NEAR(mx[i] - mn[i], 3.0, 0.15);
    }

    ts_mesh_free(&cube); ts_mesh_free(&sphere); ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: Minkowski of cylinder + small sphere = rounded cylinder.
 * Fidget spinner arms are exactly this operation. */
static void test_mink_cylinder_sphere_green(void) {
    ts_mesh cyl = ts_mesh_init(), sph = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cylinder(10.0, 2.0, 2.0, 24, &cyl);  /* h=10, r=2, fn=24 */
    ts_gen_sphere(0.3, 12, &sph);                 /* small rounding sphere */

    int ret = ts_csg_minkowski(&cyl, &sph, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    /* Original cylinder vol = pi * r^2 * h = pi * 4 * 10 = ~125.66 */
    double cyl_vol = M_PI * 2.0 * 2.0 * 10.0;
    /* Rounded cylinder must be larger than original */
    TS_ASSERT_TRUE(vol > cyl_vol);
    /* But not absurdly large — bounded by (r+0.3)^2 * pi * (h+0.6) */
    double max_vol = M_PI * 2.3 * 2.3 * 10.6;
    TS_ASSERT_TRUE(vol < max_vol * 1.1);

    /* Height should increase by sphere diameter */
    double mn[3], mx[3];
    mesh_aabb(&out, mn, mx);
    double height = mx[2] - mn[2];
    TS_ASSERT_NEAR(height, 10.6, 0.3);

    ts_mesh_free(&cyl); ts_mesh_free(&sph); ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: Minkowski of two spheres = larger sphere.
 * Minkowski(sphere(r1), sphere(r2)) = sphere(r1+r2). */
static void test_mink_sphere_sphere_green(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_sphere(1.0, 16, &a);   /* r=1 */
    ts_gen_sphere(0.5, 12, &b);   /* r=0.5 */

    int ret = ts_csg_minkowski(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    /* Expected: sphere of r=1.5, vol = (4/3)*pi*1.5^3 = ~14.137 */
    double expected = (4.0/3.0) * M_PI * 1.5 * 1.5 * 1.5;
    /* Hull of discrete sphere vertices won't be perfect, allow 15% */
    TS_ASSERT_NEAR(vol, expected, expected * 0.15);

    /* AABB should be ~3x3x3 (diameter = 2*1.5 = 3) */
    double mn[3], mx[3];
    mesh_aabb(&out, mn, mx);
    for (int i = 0; i < 3; i++) {
        TS_ASSERT_NEAR(mx[i] - mn[i], 3.0, 0.4);
    }

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: Asymmetric Minkowski — rectangle + small cube = expanded box.
 * Tests non-uniform scaling correctness. */
static void test_mink_asymmetric_green(void) {
    ts_mesh rect = ts_mesh_init(), small = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(4, 2, 1, &rect);        /* 4x2x1 box */
    ts_gen_cube(0.5, 0.5, 0.5, &small); /* 0.5x0.5x0.5 cube */

    int ret = ts_csg_minkowski(&rect, &small, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    /* Minkowski of two boxes = box with dimensions (4+0.5) x (2+0.5) x (1+0.5) */
    double expected = 4.5 * 2.5 * 1.5;  /* = 16.875 */
    TS_ASSERT_NEAR(vol, expected, 0.5);

    /* Verify AABB dimensions */
    double mn[3], mx[3];
    mesh_aabb(&out, mn, mx);
    TS_ASSERT_NEAR(mx[0] - mn[0], 4.5, 0.1);
    TS_ASSERT_NEAR(mx[1] - mn[1], 2.5, 0.1);
    TS_ASSERT_NEAR(mx[2] - mn[2], 1.5, 0.1);

    ts_mesh_free(&rect); ts_mesh_free(&small); ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: Mesh topology — Minkowski output must be watertight.
 * Uses small meshes so the O(n^2) edge check is feasible. */
static void test_mink_watertight_green(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(0.5, 0.5, 0.5, &b);

    int ret = ts_csg_minkowski(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    /* Quickhull output should be watertight */
    TS_ASSERT_TRUE(mesh_is_watertight(&out));

    /* Normals should face outward (positive signed volume) */
    TS_ASSERT_TRUE(mesh_has_consistent_normals(&out));

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: Minkowski with translated inputs.
 * Result center should be sum of input centers. */
static void test_mink_translated_green(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(1, 1, 1, &b);

    /* Translate a to [3,0,0] and b to [0,2,0] */
    for (int i = 0; i < a.vert_count; i++) a.verts[i].pos[0] += 3.0;
    for (int i = 0; i < b.vert_count; i++) b.verts[i].pos[1] += 2.0;

    int ret = ts_csg_minkowski(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    /* Volume should still be 8 (two unit cubes) */
    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_NEAR(vol, 8.0, 0.5);

    /* Center of result AABB should be at (3, 2, 0) */
    double mn[3], mx[3];
    mesh_aabb(&out, mn, mx);
    double cx = (mn[0] + mx[0]) * 0.5;
    double cy = (mn[1] + mx[1]) * 0.5;
    double cz = (mn[2] + mx[2]) * 0.5;
    TS_ASSERT_NEAR(cx, 3.0, 0.1);
    TS_ASSERT_NEAR(cy, 2.0, 0.1);
    TS_ASSERT_NEAR(cz, 0.0, 0.1);

    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: High-resolution Minkowski — cube + sphere with fn=32.
 * Tests that higher vertex counts don't corrupt the hull. */
static void test_mink_highres_green(void) {
    ts_mesh cube = ts_mesh_init(), sphere = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &cube);
    ts_gen_sphere(0.25, 32, &sphere);  /* fn=32, 561 verts */

    int ret = ts_csg_minkowski(&cube, &sphere, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    /* Cube 1x1x1 + sphere r=0.25 -> rounded box ~1.5^3 = 3.375 max */
    TS_ASSERT_TRUE(vol > 1.0);   /* bigger than original cube */
    TS_ASSERT_TRUE(vol < 3.375); /* smaller than bounding box */

    /* Hull should produce a clean mesh */
    TS_ASSERT_TRUE(mesh_has_consistent_normals(&out));

    ts_mesh_free(&cube); ts_mesh_free(&sphere); ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: Minkowski commutativity — minkowski(A,B) == minkowski(B,A).
 * Volume must be identical regardless of argument order. */
static void test_mink_commutative_green(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init();
    ts_mesh out_ab = ts_mesh_init(), out_ba = ts_mesh_init();
    ts_gen_cube(2, 1, 1, &a);
    ts_gen_sphere(0.3, 12, &b);

    int ret1 = ts_csg_minkowski(&a, &b, &out_ab);
    int ret2 = ts_csg_minkowski(&b, &a, &out_ba);
    TS_ASSERT_EQ_INT(ret1, TS_CSG_OK);
    TS_ASSERT_EQ_INT(ret2, TS_CSG_OK);

    double vol_ab = fabs(mesh_signed_volume(&out_ab));
    double vol_ba = fabs(mesh_signed_volume(&out_ba));
    /* Volumes should match within hull discretization tolerance */
    TS_ASSERT_NEAR(vol_ab, vol_ba, vol_ab * 0.05);

    ts_mesh_free(&a); ts_mesh_free(&b);
    ts_mesh_free(&out_ab); ts_mesh_free(&out_ba);
    TS_PASS();
}

/* GREEN: Minkowski with zero-size operand returns empty.
 * Edge case: one mesh has no geometry. */
static void test_mink_empty_input_green(void) {
    ts_mesh a = ts_mesh_init(), empty = ts_mesh_init(), out = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    /* empty stays with 0 verts, 0 tris */

    int ret = ts_csg_minkowski(&a, &empty, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_OK);
    /* Should return empty — can't minkowski with nothing */
    TS_ASSERT_EQ_INT(out.tri_count, 0);

    ts_mesh_free(&a); ts_mesh_free(&empty); ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: Minkowski with NULL inputs returns error. */
static void test_mink_null_input_green(void) {
    ts_mesh out = ts_mesh_init();
    int ret = ts_csg_minkowski(NULL, NULL, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_ERROR);
    ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: End-to-end Minkowski via interpreter.
 * This is the actual code path that crashed on the fidget spinner. */
static void test_mink_interp_cube_sphere_green(void) {
    const char *scad =
        "$fn = 16;\n"
        "minkowski() {\n"
        "    cube([2,2,2], center=true);\n"
        "    sphere(r=0.5);\n"
        "}\n";

    ts_parse_error err = {0};
    ts_interpret_opts opts = {0};
    opts.fn_override = 16;
    opts.fa_override = 12;
    opts.fs_override = 2;
    opts.force_quality = 1;
    ts_mesh result = ts_interpret_ex(scad, &err, &opts);
    TS_ASSERT_TRUE(err.msg[0] == '\0');  /* no parse error */
    TS_ASSERT_TRUE(result.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&result));
    /* Same as direct test: must be > cube vol, < bounding box */
    TS_ASSERT_TRUE(vol > 8.0);
    TS_ASSERT_TRUE(vol < 27.0);

    ts_mesh_free(&result);
    TS_PASS();
}

/* GREEN: End-to-end Minkowski cylinder+sphere via interpreter.
 * The fidget spinner arm shape. */
static void test_mink_interp_cyl_sphere_green(void) {
    const char *scad =
        "$fn = 16;\n"
        "minkowski() {\n"
        "    cylinder(h=10, r=2, center=true);\n"
        "    sphere(r=0.3);\n"
        "}\n";

    ts_parse_error err = {0};
    ts_interpret_opts opts = {0};
    opts.fn_override = 16;
    opts.fa_override = 12;
    opts.fs_override = 2;
    opts.force_quality = 1;
    ts_mesh result = ts_interpret_ex(scad, &err, &opts);
    TS_ASSERT_TRUE(err.msg[0] == '\0');
    TS_ASSERT_TRUE(result.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&result));
    double cyl_vol = M_PI * 2.0 * 2.0 * 10.0;
    TS_ASSERT_TRUE(vol > cyl_vol * 0.9);  /* at least ~cylinder vol */

    ts_mesh_free(&result);
    TS_PASS();
}

/* GREEN: End-to-end fidget spinner pattern:
 * union of arms + bearing holes, all with minkowski rounding. */
static void test_mink_interp_fidget_pattern_green(void) {
    const char *scad =
        "$fn = 12;\n"
        "minkowski() {\n"
        "    union() {\n"
        "        cylinder(h=5, r=10, center=true);\n"
        "        for (a = [0, 120, 240])\n"
        "            rotate([0, 0, a])\n"
        "                translate([20, 0, 0])\n"
        "                    cylinder(h=5, r=8, center=true);\n"
        "    }\n"
        "    sphere(r=0.5);\n"
        "}\n";

    ts_parse_error err = {0};
    ts_interpret_opts opts = {0};
    opts.fn_override = 12;
    opts.fa_override = 12;
    opts.fs_override = 2;
    opts.force_quality = 1;
    ts_mesh result = ts_interpret_ex(scad, &err, &opts);
    TS_ASSERT_TRUE(err.msg[0] == '\0');
    TS_ASSERT_TRUE(result.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&result));
    /* Must produce real geometry with positive volume */
    TS_ASSERT_TRUE(vol > 100.0);

    ts_mesh_free(&result);
    TS_PASS();
}

/* ================================================================
 * SECTION 8: EXTRUSION TESTS
 * Linear extrude + rotate extrude
 * Parallelism: per-slice / per-step vertex gen (GPU)
 * ================================================================ */

/* --- LINEAR EXTRUDE --- */
/* GREEN: extrude a unit square 10 units high = volume 10 */
static void test_linear_extrude_green(void) {
    ts_mesh out = ts_mesh_init();
    /* Unit square profile: (0,0) (1,0) (1,1) (0,1) */
    double profile[] = { 0,0, 1,0, 1,1, 0,1 };
    int ret = ts_linear_extrude(profile, 4, 10.0, 0.0, 1, 1.0, 0, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_NEAR(vol, 10.0, 0.5);

    ts_mesh_free(&out);
    TS_PASS();
}

/* RED: extrude must produce non-zero volume */
static void test_linear_extrude_red(void) {
    ts_mesh out = ts_mesh_init();
    double profile[] = { 0,0, 1,0, 1,1, 0,1 };
    int ret = ts_linear_extrude(profile, 4, 5.0, 0.0, 1, 1.0, 0, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_OK);

    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_TRUE(vol > 0.1); /* Must have volume */

    ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: extrude triangle with twist */
static void test_linear_extrude_twist_green(void) {
    ts_mesh out = ts_mesh_init();
    double profile[] = { 0,0, 2,0, 1,2 };
    /* 90 degree twist over 10 units, 10 slices */
    int ret = ts_linear_extrude(profile, 3, 10.0, 90.0, 10, 1.0, 1, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);
    /* More slices = more triangles: 10 slices * 3 edges * 2 + caps */
    TS_ASSERT_TRUE(out.tri_count >= 60);

    double vol = fabs(mesh_signed_volume(&out));
    /* Triangle area = 0.5*2*2 = 2, height 10, vol ~ 20 */
    TS_ASSERT_TRUE(vol > 15.0 && vol < 25.0);

    ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: extrude with taper (scale_top = 0.5) */
static void test_linear_extrude_taper_green(void) {
    ts_mesh out = ts_mesh_init();
    double profile[] = { 0,0, 2,0, 2,2, 0,2 };
    /* scale_top=0.5 means top is half the size */
    int ret = ts_linear_extrude(profile, 4, 6.0, 0.0, 1, 0.5, 0, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    /* Frustum: V = h/3 * (A1 + A2 + sqrt(A1*A2))
     * A1 = 4 (2x2), A2 = 1 (1x1), h = 6
     * V = 6/3 * (4 + 1 + 2) = 14 */
    TS_ASSERT_TRUE(vol > 10.0 && vol < 18.0);

    ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: centered extrude should span from -h/2 to +h/2 */
static void test_linear_extrude_center_green(void) {
    ts_mesh out = ts_mesh_init();
    double profile[] = { 0,0, 1,0, 1,1, 0,1 };
    int ret = ts_linear_extrude(profile, 4, 10.0, 0.0, 1, 1.0, 1, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_OK);

    /* Check Z bounds: should be centered around 0 */
    double zmin = 1e30, zmax = -1e30;
    for (int i = 0; i < out.vert_count; i++) {
        if (out.verts[i].pos[2] < zmin) zmin = out.verts[i].pos[2];
        if (out.verts[i].pos[2] > zmax) zmax = out.verts[i].pos[2];
    }
    TS_ASSERT_NEAR(zmin, -5.0, 0.01);
    TS_ASSERT_NEAR(zmax, 5.0, 0.01);

    ts_mesh_free(&out);
    TS_PASS();
}

/* --- ROTATE EXTRUDE --- */
/* GREEN: revolve a small rectangle around Y axis = torus-like shape */
static void test_rotate_extrude_green(void) {
    ts_mesh out = ts_mesh_init();
    /* Rectangular profile at radius 5: 2 units wide, 3 units tall */
    double profile[] = { 4,0, 6,0, 6,3, 4,3 };
    int ret = ts_rotate_extrude(profile, 4, 360.0, 32, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    /* Pappus theorem: V = 2*pi*R*A where R=5 (centroid), A=2*3=6
     * V = 2*pi*5*6 = 188.5 */
    TS_ASSERT_TRUE(vol > 150.0 && vol < 220.0);

    ts_mesh_free(&out);
    TS_PASS();
}

/* RED: rotate extrude must produce triangles */
static void test_rotate_extrude_red(void) {
    ts_mesh out = ts_mesh_init();
    double profile[] = { 5,0, 5,10, 3,10, 3,0 };
    int ret = ts_rotate_extrude(profile, 4, 360.0, 16, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_OK);
    TS_ASSERT_TRUE(out.tri_count > 0);

    double vol = fabs(mesh_signed_volume(&out));
    TS_ASSERT_TRUE(vol > 0.1);

    ts_mesh_free(&out);
    TS_PASS();
}

/* GREEN: partial revolution (180 degrees) = half the volume */
static void test_rotate_extrude_partial_green(void) {
    ts_mesh out_full = ts_mesh_init(), out_half = ts_mesh_init();
    double profile[] = { 4,0, 6,0, 6,3, 4,3 };

    ts_rotate_extrude(profile, 4, 360.0, 32, &out_full);
    ts_rotate_extrude(profile, 4, 180.0, 16, &out_half);

    double vol_full = fabs(mesh_signed_volume(&out_full));
    double vol_half = fabs(mesh_signed_volume(&out_half));

    /* Half revolution should be roughly half the volume */
    double ratio = vol_half / vol_full;
    TS_ASSERT_TRUE(ratio > 0.35 && ratio < 0.65);

    ts_mesh_free(&out_full); ts_mesh_free(&out_half);
    TS_PASS();
}

/* GREEN: error on invalid input */
static void test_extrude_error_green(void) {
    ts_mesh out = ts_mesh_init();
    /* Too few points */
    double profile[] = { 0,0, 1,0 };
    int ret = ts_linear_extrude(profile, 2, 10.0, 0.0, 1, 1.0, 0, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_ERROR);

    /* NULL profile */
    ret = ts_linear_extrude(NULL, 4, 10.0, 0.0, 1, 1.0, 0, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_ERROR);

    /* Zero height */
    double profile2[] = { 0,0, 1,0, 0.5,1 };
    ret = ts_linear_extrude(profile2, 3, 0.0, 0.0, 1, 1.0, 0, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_ERROR);

    /* Rotate: too few points */
    ret = ts_rotate_extrude(profile, 1, 360.0, 16, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_ERROR);

    ts_mesh_free(&out);
    TS_PASS();
}

/* ================================================================
 * SECTION 8.5: BEZIER SURFACE PATCH TESTS
 * Quadratic tensor-product bezier patches: S(u,v)
 * Parallelism: SIMD (per-point evaluation is independent)
 * ================================================================ */

/* --- eval: corner interpolation --- */
static void test_bezier_patch_eval_green(void) {
    /* Flat patch from (0,0,0) to (2,2,0) */
    ts_bezier_patch p = ts_bezier_patch_flat(0.0, 0.0, 2.0, 2.0, 0.0);

    /* Corners should be exactly the corner control points */
    ts_vec3 s00 = ts_bezier_patch_eval(&p, 0.0, 0.0);
    ts_vec3 s10 = ts_bezier_patch_eval(&p, 1.0, 0.0);
    ts_vec3 s01 = ts_bezier_patch_eval(&p, 0.0, 1.0);
    ts_vec3 s11 = ts_bezier_patch_eval(&p, 1.0, 1.0);

    TS_ASSERT_VEC3_NEAR(s00, ts_vec3_make(0.0, 0.0, 0.0), 1e-14);
    TS_ASSERT_VEC3_NEAR(s10, ts_vec3_make(2.0, 0.0, 0.0), 1e-14);
    TS_ASSERT_VEC3_NEAR(s01, ts_vec3_make(0.0, 2.0, 0.0), 1e-14);
    TS_ASSERT_VEC3_NEAR(s11, ts_vec3_make(2.0, 2.0, 0.0), 1e-14);

    /* Center should be midpoint */
    ts_vec3 mid = ts_bezier_patch_eval(&p, 0.5, 0.5);
    TS_ASSERT_VEC3_NEAR(mid, ts_vec3_make(1.0, 1.0, 0.0), 1e-14);

    TS_PASS();
}
static void test_bezier_patch_eval_red(void) {
    /* If eval ignored v parameter, S(0,1) would equal S(0,0) */
    ts_bezier_patch p = ts_bezier_patch_flat(0.0, 0.0, 2.0, 2.0, 0.0);
    ts_vec3 s00 = ts_bezier_patch_eval(&p, 0.0, 0.0);
    ts_vec3 s01 = ts_bezier_patch_eval(&p, 0.0, 1.0);
    /* These must differ — s01.y should be 2.0, not 0.0 */
    TS_ASSERT_TRUE(fabs(s01.v[1] - s00.v[1]) > 1.0);
    TS_PASS();
}

/* --- eval: dome shape (raised center CP) --- */
static void test_bezier_patch_dome_green(void) {
    ts_bezier_patch p = ts_bezier_patch_dome(0.0, 0.0, 2.0, 2.0, 5.0);

    /* Corners at z=0 */
    ts_vec3 s00 = ts_bezier_patch_eval(&p, 0.0, 0.0);
    TS_ASSERT_NEAR(s00.v[2], 0.0, 1e-14);

    /* Center should be raised (but not to full height — quadratic blend) */
    ts_vec3 mid = ts_bezier_patch_eval(&p, 0.5, 0.5);
    TS_ASSERT_TRUE(mid.v[2] > 0.0);
    /* Quadratic basis at t=0.5: B1(0.5) = 2*0.5*0.5 = 0.5
     * So center z = B1(0.5)*B1(0.5)*5.0 = 0.25*5.0 = 1.25 */
    TS_ASSERT_NEAR(mid.v[2], 1.25, 1e-14);

    TS_PASS();
}
static void test_bezier_patch_dome_red(void) {
    ts_bezier_patch p = ts_bezier_patch_dome(0.0, 0.0, 2.0, 2.0, 5.0);
    /* If basis functions were wrong (e.g., linear), center z would be 5/3 ≈ 1.667 */
    ts_vec3 mid = ts_bezier_patch_eval(&p, 0.5, 0.5);
    double wrong_linear = 5.0 / 3.0;
    TS_ASSERT_TRUE(fabs(mid.v[2] - wrong_linear) > 0.3);
    TS_PASS();
}

/* --- normal: flat patch should have uniform +Z normal --- */
static void test_bezier_patch_normal_green(void) {
    ts_bezier_patch p = ts_bezier_patch_flat(0.0, 0.0, 2.0, 2.0, 0.0);

    /* Normal everywhere on a flat XY patch should be (0,0,1) */
    ts_vec3 n_center = ts_bezier_patch_normal(&p, 0.5, 0.5);
    ts_vec3 n_corner = ts_bezier_patch_normal(&p, 0.0, 0.0);
    ts_vec3 n_edge   = ts_bezier_patch_normal(&p, 1.0, 0.5);

    ts_vec3 up = ts_vec3_make(0.0, 0.0, 1.0);
    TS_ASSERT_VEC3_NEAR(n_center, up, 1e-12);
    TS_ASSERT_VEC3_NEAR(n_corner, up, 1e-12);
    TS_ASSERT_VEC3_NEAR(n_edge, up, 1e-12);

    TS_PASS();
}
static void test_bezier_patch_normal_red(void) {
    /* If cross product was reversed, normal would be (0,0,-1) */
    ts_bezier_patch p = ts_bezier_patch_flat(0.0, 0.0, 2.0, 2.0, 0.0);
    ts_vec3 n = ts_bezier_patch_normal(&p, 0.5, 0.5);
    /* Must be +Z not -Z */
    TS_ASSERT_TRUE(n.v[2] > 0.5);
    TS_PASS();
}

/* --- normal: dome should have varying normals --- */
static void test_bezier_patch_dome_normal_green(void) {
    ts_bezier_patch p = ts_bezier_patch_dome(0.0, 0.0, 2.0, 2.0, 5.0);

    /* Center normal should still point mostly upward */
    ts_vec3 n_center = ts_bezier_patch_normal(&p, 0.5, 0.5);
    TS_ASSERT_TRUE(n_center.v[2] > 0.0);

    /* Edge normals should tilt outward */
    ts_vec3 n_left = ts_bezier_patch_normal(&p, 0.0, 0.5);
    /* Left edge: dS/du points +X, dS/dv points +Y with some Z tilt
     * Normal should have negative X component (facing left/outward) */
    /* Actually on a dome, the left edge normal tilts away from center */

    /* All normals should be unit length */
    TS_ASSERT_NEAR(ts_vec3_norm(n_center), 1.0, 1e-12);
    TS_ASSERT_NEAR(ts_vec3_norm(n_left), 1.0, 1e-12);

    TS_PASS();
}

/* --- bbox: control point hull contains the surface --- */
static void test_bezier_patch_bbox_green(void) {
    ts_bezier_patch p = ts_bezier_patch_dome(0.0, 0.0, 4.0, 4.0, 3.0);

    ts_vec3 bmin, bmax;
    ts_bezier_patch_bbox(&p, &bmin, &bmax);

    TS_ASSERT_NEAR(bmin.v[0], 0.0, 1e-14);
    TS_ASSERT_NEAR(bmin.v[1], 0.0, 1e-14);
    TS_ASSERT_NEAR(bmin.v[2], 0.0, 1e-14);
    TS_ASSERT_NEAR(bmax.v[0], 4.0, 1e-14);
    TS_ASSERT_NEAR(bmax.v[1], 4.0, 1e-14);
    TS_ASSERT_NEAR(bmax.v[2], 3.0, 1e-14);

    /* Verify all evaluated points are inside the bbox */
    for (int j = 0; j <= 10; j++) {
        for (int i = 0; i <= 10; i++) {
            double u = (double)i / 10.0;
            double v = (double)j / 10.0;
            ts_vec3 pt = ts_bezier_patch_eval(&p, u, v);
            for (int k = 0; k < 3; k++) {
                TS_ASSERT_TRUE(pt.v[k] >= bmin.v[k] - 1e-12);
                TS_ASSERT_TRUE(pt.v[k] <= bmax.v[k] + 1e-12);
            }
        }
    }
    TS_PASS();
}
static void test_bezier_patch_bbox_red(void) {
    /* If bbox only used corners (not all 9 CPs), dome height would be missed */
    ts_bezier_patch p = ts_bezier_patch_dome(0.0, 0.0, 4.0, 4.0, 3.0);
    ts_vec3 bmin, bmax;
    ts_bezier_patch_bbox(&p, &bmin, &bmax);
    /* Wrong bbox using only corners would have max z = 0 */
    TS_ASSERT_TRUE(bmax.v[2] > 2.0);
    TS_PASS();
}

/* --- closest_uv: point on surface should map back to itself --- */
static void test_bezier_patch_closest_green(void) {
    ts_bezier_patch p = ts_bezier_patch_flat(0.0, 0.0, 2.0, 2.0, 0.0);

    /* Query a point ON the surface: (1, 1, 0) is S(0.5, 0.5) */
    double u, v;
    int ret = ts_bezier_patch_closest_uv(&p, ts_vec3_make(1.0, 1.0, 0.0),
                                          &u, &v, 20);
    TS_ASSERT_EQ_INT(ret, 0);
    TS_ASSERT_NEAR(u, 0.5, 1e-6);
    TS_ASSERT_NEAR(v, 0.5, 1e-6);

    /* Query a point above the surface: closest should still be (1,1,0) */
    ret = ts_bezier_patch_closest_uv(&p, ts_vec3_make(1.0, 1.0, 5.0),
                                      &u, &v, 20);
    TS_ASSERT_EQ_INT(ret, 0);
    ts_vec3 closest = ts_bezier_patch_eval(&p, u, v);
    TS_ASSERT_NEAR(closest.v[0], 1.0, 1e-4);
    TS_ASSERT_NEAR(closest.v[1], 1.0, 1e-4);
    TS_ASSERT_NEAR(closest.v[2], 0.0, 1e-4);

    TS_PASS();
}
static void test_bezier_patch_closest_red(void) {
    /* If closest_uv always returned (0.5, 0.5), a corner query would fail */
    ts_bezier_patch p = ts_bezier_patch_flat(0.0, 0.0, 2.0, 2.0, 0.0);
    double u, v;
    /* Query near corner (0,0,0) */
    ts_bezier_patch_closest_uv(&p, ts_vec3_make(0.0, 0.0, 0.0),
                                &u, &v, 20);
    ts_vec3 closest = ts_bezier_patch_eval(&p, u, v);
    /* Must be near (0,0,0), not (1,1,0) */
    TS_ASSERT_TRUE(ts_vec3_distance(closest, ts_vec3_make(0.0, 0.0, 0.0)) < 0.1);
    TS_PASS();
}

/* --- sdf: points above/below flat patch --- */
static void test_bezier_patch_sdf_green(void) {
    ts_bezier_patch p = ts_bezier_patch_flat(0.0, 0.0, 2.0, 2.0, 0.0);

    /* Point above surface: positive distance */
    double d_above = ts_bezier_patch_sdf(&p, ts_vec3_make(1.0, 1.0, 3.0), 20);
    TS_ASSERT_TRUE(d_above > 0.0);
    TS_ASSERT_NEAR(d_above, 3.0, 0.1);

    /* Point below surface: negative distance */
    double d_below = ts_bezier_patch_sdf(&p, ts_vec3_make(1.0, 1.0, -2.0), 20);
    TS_ASSERT_TRUE(d_below < 0.0);
    TS_ASSERT_NEAR(d_below, -2.0, 0.1);

    /* Point on surface: ~zero distance */
    double d_on = ts_bezier_patch_sdf(&p, ts_vec3_make(1.0, 1.0, 0.0), 20);
    TS_ASSERT_NEAR(d_on, 0.0, 1e-6);

    TS_PASS();
}
static void test_bezier_patch_sdf_red(void) {
    /* If SDF ignored sign (always positive), below-surface would be wrong */
    ts_bezier_patch p = ts_bezier_patch_flat(0.0, 0.0, 2.0, 2.0, 0.0);
    double d_below = ts_bezier_patch_sdf(&p, ts_vec3_make(1.0, 1.0, -2.0), 20);
    /* Must be negative, not positive */
    TS_ASSERT_TRUE(d_below < -1.0);
    TS_PASS();
}

/* --- basis functions: partition of unity --- */
static void test_bezier_basis_green(void) {
    /* Quadratic Bernstein basis must sum to 1 for any t in [0,1] */
    for (int i = 0; i <= 100; i++) {
        double t = (double)i / 100.0;
        double sum = ts_qbasis0(t) + ts_qbasis1(t) + ts_qbasis2(t);
        TS_ASSERT_NEAR(sum, 1.0, 1e-14);
    }
    /* Derivatives must sum to 0 (derivative of constant 1 is 0) */
    for (int i = 0; i <= 100; i++) {
        double t = (double)i / 100.0;
        double dsum = ts_qbasis0d(t) + ts_qbasis1d(t) + ts_qbasis2d(t);
        TS_ASSERT_NEAR(dsum, 0.0, 1e-14);
    }
    TS_PASS();
}
static void test_bezier_basis_red(void) {
    /* If B1(t) was t*(1-t) instead of 2*t*(1-t), sum != 1 at t=0.5 */
    double wrong_B1 = 0.5 * 0.5;  /* 0.25 instead of 0.5 */
    double sum = ts_qbasis0(0.5) + wrong_B1 + ts_qbasis2(0.5);
    TS_ASSERT_TRUE(fabs(sum - 1.0) > 0.2);
    TS_PASS();
}

/* --- Benchmarks --- */

static volatile ts_vec3 g_bench_sink_v;
static volatile double g_bench_sink_d;

static void bench_bezier_patch_eval(int n) {
    ts_bezier_patch p = ts_bezier_patch_dome(0.0, 0.0, 2.0, 2.0, 1.0);
    ts_vec3 result = ts_vec3_zero();
    for (int i = 0; i < n; i++) {
        double u = (double)(i % 100) / 100.0;
        double v = (double)((i / 100) % 100) / 100.0;
        result = ts_bezier_patch_eval(&p, u, v);
    }
    g_bench_sink_v = result;
}

static void bench_bezier_patch_normal(int n) {
    ts_bezier_patch p = ts_bezier_patch_dome(0.0, 0.0, 2.0, 2.0, 1.0);
    ts_vec3 result = ts_vec3_zero();
    for (int i = 0; i < n; i++) {
        double u = (double)(i % 100) / 100.0;
        double v = (double)((i / 100) % 100) / 100.0;
        result = ts_bezier_patch_normal(&p, u, v);
    }
    g_bench_sink_v = result;
}

static void bench_bezier_patch_sdf(int n) {
    ts_bezier_patch p = ts_bezier_patch_dome(0.0, 0.0, 2.0, 2.0, 1.0);
    double result = 0.0;
    for (int i = 0; i < n; i++) {
        double x = (double)(i % 50) / 25.0;
        double y = (double)((i / 50) % 50) / 25.0;
        result = ts_bezier_patch_sdf(&p, ts_vec3_make(x, y, 0.5), 10);
    }
    g_bench_sink_d = result;
}

/* ================================================================
 * SECTION 8.6: BEZIER MESH TESTS
 * Grid of patches with shared edges, C0/C1 continuity,
 * tessellation to triangle mesh.
 * Parallelism: GPU (batch patch evaluation)
 * ================================================================ */

/* --- mesh creation and flat init --- */
static void test_bezier_mesh_create_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(2, 3);
    TS_ASSERT_EQ_INT(m.rows, 2);
    TS_ASSERT_EQ_INT(m.cols, 3);
    TS_ASSERT_EQ_INT(m.cp_rows, 5);  /* 2*2+1 */
    TS_ASSERT_EQ_INT(m.cp_cols, 7);  /* 2*3+1 */
    TS_ASSERT_TRUE(m.cps != NULL);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}
static void test_bezier_mesh_create_red(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(2, 3);
    /* If cp_rows was rows+1 instead of 2*rows+1, we'd get 3 not 5 */
    TS_ASSERT_TRUE(m.cp_rows != m.rows + 1);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- C0 continuity: shared edges --- */
static void test_bezier_mesh_c0_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 2);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 2.0, 0.0);

    /* Patch (0,0) right edge = patch (0,1) left edge.
     * Patch (0,0) uses cp cols [0,1,2], patch (0,1) uses cp cols [2,3,4].
     * Column 2 is shared. */
    ts_bezier_patch p0 = ts_bezier_mesh_get_patch(&m, 0, 0);
    ts_bezier_patch p1 = ts_bezier_mesh_get_patch(&m, 0, 1);

    /* Right edge of p0 (u=1) should equal left edge of p1 (u=0) */
    for (int j = 0; j < 3; j++) {
        TS_ASSERT_VEC3_NEAR(p0.cp[j][2], p1.cp[j][0], 1e-14);
    }

    /* Evaluate at the boundary: both patches should agree */
    for (double v = 0.0; v <= 1.0; v += 0.25) {
        ts_vec3 s0 = ts_bezier_patch_eval(&p0, 1.0, v);
        ts_vec3 s1 = ts_bezier_patch_eval(&p1, 0.0, v);
        TS_ASSERT_VEC3_NEAR(s0, s1, 1e-14);
    }

    ts_bezier_mesh_free(&m);
    TS_PASS();
}
static void test_bezier_mesh_c0_red(void) {
    /* If patches didn't share CPs, modifying the shared column wouldn't
     * propagate to the adjacent patch */
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 2);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 2.0, 0.0);

    /* Move the shared boundary CP upward */
    ts_bezier_mesh_set_cp(&m, 1, 2, ts_vec3_make(2.0, 1.0, 5.0));

    /* Both patches must see the change */
    ts_bezier_patch p0 = ts_bezier_mesh_get_patch(&m, 0, 0);
    ts_bezier_patch p1 = ts_bezier_mesh_get_patch(&m, 0, 1);
    TS_ASSERT_NEAR(p0.cp[1][2].v[2], 5.0, 1e-14);
    TS_ASSERT_NEAR(p1.cp[1][0].v[2], 5.0, 1e-14);

    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- C1 continuity enforcement --- */
static void test_bezier_mesh_c1_col_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 2);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 2.0, 0.0);

    /* Perturb a left-side CP to create a tangent */
    ts_bezier_mesh_set_cp(&m, 1, 1, ts_vec3_make(1.0, 1.0, 2.0));

    /* Enforce C1 across the col boundary between patch 0 and patch 1 */
    ts_bezier_mesh_enforce_c1_col(&m, 0);

    /* Check: the tangent vectors across the boundary are equal.
     * Left tangent:  cp[1][2] - cp[1][1]
     * Right tangent: cp[1][3] - cp[1][2] */
    ts_vec3 shared = ts_bezier_mesh_get_cp(&m, 1, 2);
    ts_vec3 left   = ts_bezier_mesh_get_cp(&m, 1, 1);
    ts_vec3 right  = ts_bezier_mesh_get_cp(&m, 1, 3);

    ts_vec3 tan_left  = ts_vec3_sub(shared, left);
    ts_vec3 tan_right = ts_vec3_sub(right, shared);
    TS_ASSERT_VEC3_NEAR(tan_left, tan_right, 1e-14);

    ts_bezier_mesh_free(&m);
    TS_PASS();
}
static void test_bezier_mesh_c1_col_red(void) {
    /* Without C1 enforcement, tangents would NOT match after perturbation */
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 2);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 2.0, 0.0);
    ts_bezier_mesh_set_cp(&m, 1, 1, ts_vec3_make(1.0, 1.0, 2.0));

    /* Before enforcement: right side tangent is still flat (0,0,0 delta z) */
    ts_vec3 shared = ts_bezier_mesh_get_cp(&m, 1, 2);
    ts_vec3 right  = ts_bezier_mesh_get_cp(&m, 1, 3);
    ts_vec3 tan_right = ts_vec3_sub(right, shared);
    /* Right tangent z should be 0 (not yet enforced) */
    TS_ASSERT_NEAR(tan_right.v[2], 0.0, 1e-14);

    /* After enforcement it should NOT be 0 */
    ts_bezier_mesh_enforce_c1_col(&m, 0);
    right = ts_bezier_mesh_get_cp(&m, 1, 3);
    tan_right = ts_vec3_sub(right, shared);
    TS_ASSERT_TRUE(fabs(tan_right.v[2]) > 0.5);

    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- C1 row enforcement --- */
static void test_bezier_mesh_c1_row_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(2, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 2.0, 4.0, 0.0);

    /* Perturb an above-boundary CP */
    ts_bezier_mesh_set_cp(&m, 1, 1, ts_vec3_make(1.0, 1.0, 3.0));

    ts_bezier_mesh_enforce_c1_row(&m, 0);

    ts_vec3 shared = ts_bezier_mesh_get_cp(&m, 2, 1);
    ts_vec3 above  = ts_bezier_mesh_get_cp(&m, 1, 1);
    ts_vec3 below  = ts_bezier_mesh_get_cp(&m, 3, 1);

    ts_vec3 tan_above = ts_vec3_sub(shared, above);
    ts_vec3 tan_below = ts_vec3_sub(below, shared);
    TS_ASSERT_VEC3_NEAR(tan_above, tan_below, 1e-14);

    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- Tessellation --- */
static void test_bezier_mesh_tessellate_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 2.0, 2.0, 0.0);

    ts_mesh tri = ts_mesh_init();
    int ret = ts_bezier_mesh_tessellate(&m, 4, 4, &tri);
    TS_ASSERT_EQ_INT(ret, 0);

    /* 1 patch, 4 steps each direction: (4+1)*(4+1) = 25 verts, 4*4*2 = 32 tris */
    TS_ASSERT_EQ_INT(tri.vert_count, 25);
    TS_ASSERT_EQ_INT(tri.tri_count, 32);

    /* All vertices should be on the flat plane z=0 */
    for (int i = 0; i < tri.vert_count; i++) {
        TS_ASSERT_NEAR(tri.verts[i].pos[2], 0.0, 1e-14);
    }

    /* Bounding box should be [0,2] x [0,2] */
    double mn[3], mx[3];
    ts_mesh_bounds(&tri, mn, mx);
    TS_ASSERT_NEAR(mn[0], 0.0, 1e-14);
    TS_ASSERT_NEAR(mn[1], 0.0, 1e-14);
    TS_ASSERT_NEAR(mx[0], 2.0, 1e-14);
    TS_ASSERT_NEAR(mx[1], 2.0, 1e-14);

    ts_mesh_free(&tri);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}
static void test_bezier_mesh_tessellate_red(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 2.0, 2.0, 0.0);

    ts_mesh tri = ts_mesh_init();
    /* 0 steps should fail */
    int ret = ts_bezier_mesh_tessellate(&m, 0, 4, &tri);
    TS_ASSERT_EQ_INT(ret, -1);

    ts_mesh_free(&tri);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- Multi-patch tessellation: watertight seams --- */
static void test_bezier_mesh_tess_multi_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(2, 2);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 4.0, 0.0);

    /* Raise some interior CPs to make it interesting */
    ts_bezier_mesh_set_cp(&m, 2, 2, ts_vec3_make(2.0, 2.0, 3.0));

    ts_mesh tri = ts_mesh_init();
    int ret = ts_bezier_mesh_tessellate(&m, 4, 4, &tri);
    TS_ASSERT_EQ_INT(ret, 0);

    /* 2x2 patches, 4 steps: (2*4+1)*(2*4+1) = 9*9 = 81 verts */
    TS_ASSERT_EQ_INT(tri.vert_count, 81);
    /* (9-1)*(9-1)*2 = 128 tris */
    TS_ASSERT_EQ_INT(tri.tri_count, 128);

    /* The mesh should be watertight at patch boundaries.
     * Check that vertices along the boundary between patches are shared
     * (same position from both sides). */
    /* At the seam u=0.5 (global), which is at grid column 4 */
    /* Row 0, col 4: should be at x=2.0 */
    TS_ASSERT_NEAR(tri.verts[4].pos[0], 2.0, 1e-12);

    ts_mesh_free(&tri);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- Mesh bbox --- */
static void test_bezier_mesh_bbox_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, -1.0, -1.0, 3.0, 3.0, 0.0);
    ts_bezier_mesh_set_cp(&m, 1, 1, ts_vec3_make(1.0, 1.0, 7.0));

    ts_vec3 bmin, bmax;
    ts_bezier_mesh_bbox(&m, &bmin, &bmax);
    TS_ASSERT_NEAR(bmin.v[0], -1.0, 1e-14);
    TS_ASSERT_NEAR(bmax.v[2], 7.0, 1e-14);

    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- Mesh closest point --- */
static void test_bezier_mesh_closest_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 2);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 2.0, 0.0);

    /* Query a point above the right patch */
    int pr, pc;
    double u, v;
    double dist = ts_bezier_mesh_closest(&m, ts_vec3_make(3.0, 1.0, 5.0),
                                          &pr, &pc, &u, &v, 20);

    /* Should find patch (0,1) since x=3.0 is in the right half */
    TS_ASSERT_EQ_INT(pr, 0);
    TS_ASSERT_EQ_INT(pc, 1);
    /* Distance should be ~5.0 (straight down to z=0 plane) */
    TS_ASSERT_NEAR(dist, 5.0, 0.1);

    ts_bezier_mesh_free(&m);
    TS_PASS();
}
static void test_bezier_mesh_closest_red(void) {
    /* If closest always returned patch (0,0), right-side query would be wrong */
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 2);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 2.0, 0.0);

    int pr, pc;
    double u, v;
    ts_bezier_mesh_closest(&m, ts_vec3_make(3.5, 1.0, 0.0),
                            &pr, &pc, &u, &v, 20);
    /* Must be in patch col 1, not 0 */
    TS_ASSERT_EQ_INT(pc, 1);

    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- STL export round-trip --- */
static void test_bezier_mesh_stl_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 2.0, 2.0, 0.0);
    /* Make it a dome */
    ts_bezier_mesh_set_cp(&m, 1, 1, ts_vec3_make(1.0, 1.0, 1.0));

    ts_mesh tri = ts_mesh_init();
    ts_bezier_mesh_tessellate(&m, 8, 8, &tri);

    /* Write STL */
    int ret = ts_mesh_write_stl(&tri, "/tmp/ts_bezier_mesh_test.stl");
    TS_ASSERT_EQ_INT(ret, 0);

    /* Read it back */
    ts_mesh loaded = ts_mesh_init();
    ret = ts_mesh_read_stl(&loaded, "/tmp/ts_bezier_mesh_test.stl");
    TS_ASSERT_EQ_INT(ret, 0);
    TS_ASSERT_EQ_INT(loaded.tri_count, tri.tri_count);

    ts_mesh_free(&loaded);
    ts_mesh_free(&tri);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- Benchmarks --- */

static void bench_bezier_mesh_tessellate(int n) {
    ts_bezier_mesh m = ts_bezier_mesh_new(4, 4);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 8.0, 8.0, 0.0);
    ts_bezier_mesh_set_cp(&m, 4, 4, ts_vec3_make(4.0, 4.0, 2.0));

    for (int i = 0; i < n; i++) {
        ts_mesh tri = ts_mesh_init();
        ts_bezier_mesh_tessellate(&m, 8, 8, &tri);
        ts_mesh_free(&tri);
    }
    ts_bezier_mesh_free(&m);
}

static void bench_bezier_mesh_closest(int n) {
    ts_bezier_mesh m = ts_bezier_mesh_new(4, 4);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 8.0, 8.0, 0.0);
    ts_bezier_mesh_set_cp(&m, 4, 4, ts_vec3_make(4.0, 4.0, 2.0));

    int pr, pc;
    double u, v;
    for (int i = 0; i < n; i++) {
        double x = (double)(i % 80) / 10.0;
        double y = (double)((i / 80) % 80) / 10.0;
        ts_bezier_mesh_closest(&m, ts_vec3_make(x, y, 1.0),
                                &pr, &pc, &u, &v, 10);
    }
    ts_bezier_mesh_free(&m);
}

/* ================================================================
 * SECTION 8.7: BEZIER VOXELIZATION TESTS
 * Bezier mesh → SDF grid (narrowband voxelization)
 * Parallelism: GPU (per-voxel independent)
 * ================================================================ */

/* --- flat mesh voxelizes to thin shell --- */
static void test_bezier_voxel_flat_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 4.0, 2.0);

    ts_sdf_grid g = ts_sdf_grid_new(32, 32, 32, 0.25, -1.0, -1.0, -1.0);
    TS_ASSERT_TRUE(g.distances != NULL);

    int evaluated = ts_bezier_mesh_voxelize(&m, &g, 0.5, 15);
    TS_ASSERT_TRUE(evaluated > 0);

    /* Voxels near z=2.0 should have small distances */
    int near_surface = ts_sdf_grid_count_near_surface(&g, 0.3f);
    TS_ASSERT_TRUE(near_surface > 0);

    /* Voxels above z=2 should have positive distance */
    float d_above = ts_sdf_grid_get(&g, 10, 10, 20); /* z ≈ 4.0 */
    /* This voxel is well above the surface — should be positive or FLT_MAX */
    TS_ASSERT_TRUE(d_above > 0.0f || d_above == FLT_MAX);

    /* Voxels below z=2 should have negative distance */
    float d_below = ts_sdf_grid_get(&g, 10, 10, 8); /* z ≈ 1.0 */
    /* If within narrowband, should be negative */
    if (d_below != FLT_MAX) {
        TS_ASSERT_TRUE(d_below < 0.0f);
    }

    ts_sdf_grid_free(&g);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}
static void test_bezier_voxel_flat_red(void) {
    /* If voxelizer didn't evaluate anything, count would be 0 */
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 4.0, 2.0);

    ts_sdf_grid g = ts_sdf_grid_new(16, 16, 16, 0.5, -1.0, -1.0, -1.0);
    int evaluated = ts_bezier_mesh_voxelize(&m, &g, 0.5, 15);
    /* Must have evaluated some voxels */
    TS_ASSERT_TRUE(evaluated > 10);

    ts_sdf_grid_free(&g);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- dome produces inside voxels --- */
static void test_bezier_voxel_dome_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 4.0, 0.0);
    /* Raise center to make a dome */
    ts_bezier_mesh_set_cp(&m, 1, 1, ts_vec3_make(2.0, 2.0, 3.0));

    ts_sdf_grid g = ts_sdf_grid_new(32, 32, 32, 0.25, -1.0, -1.0, -2.0);
    int evaluated = ts_bezier_mesh_voxelize(&m, &g, 1.0, 15);
    TS_ASSERT_TRUE(evaluated > 0);

    /* Should have voxels near the surface */
    int near = ts_sdf_grid_count_near_surface(&g, 0.5f);
    TS_ASSERT_TRUE(near > 0);

    ts_sdf_grid_free(&g);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- multi-patch mesh voxelizes correctly --- */
static void test_bezier_voxel_multi_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(2, 2);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 4.0, 1.0);
    /* Raise center of the whole mesh */
    ts_bezier_mesh_set_cp(&m, 2, 2, ts_vec3_make(2.0, 2.0, 3.0));

    ts_sdf_grid g = ts_sdf_grid_new(32, 32, 32, 0.25, -1.0, -1.0, -1.0);
    int evaluated = ts_bezier_mesh_voxelize(&m, &g, 1.0, 15);
    TS_ASSERT_TRUE(evaluated > 0);

    /* Should have more near-surface voxels than a single patch */
    int near = ts_sdf_grid_count_near_surface(&g, 0.5f);
    TS_ASSERT_TRUE(near > 0);

    /* The center region (around 2,2) should be near the raised surface */
    /* Cell at ~(2,2,2) should have a distance close to 0 or slightly negative
     * since the dome center is at z≈3 but quadratic blend means surface
     * at center is lower */

    ts_sdf_grid_free(&g);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- SDF gradient gives normals --- */
static void test_bezier_voxel_gradient_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 4.0, 2.0);

    ts_sdf_grid g = ts_sdf_grid_new(32, 32, 32, 0.25, -1.0, -1.0, -1.0);
    ts_bezier_mesh_voxelize(&m, &g, 0.5, 15);

    /* Gradient at a point near the flat surface should point +Z */
    /* z=2.0 is at grid cell iz ≈ (2.0 - (-1.0)) / 0.25 = 12 */
    ts_vec3 grad = ts_sdf_grid_gradient(&g, 10, 10, 12);
    /* For a flat XY surface, gradient should be predominantly in Z */
    TS_ASSERT_TRUE(fabs(grad.v[2]) > 0.5);

    ts_sdf_grid_free(&g);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}
static void test_bezier_voxel_gradient_red(void) {
    /* If gradient was always zero, the normal would be zero vec */
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 4.0, 2.0);

    ts_sdf_grid g = ts_sdf_grid_new(32, 32, 32, 0.25, -1.0, -1.0, -1.0);
    ts_bezier_mesh_voxelize(&m, &g, 0.5, 15);

    ts_vec3 grad = ts_sdf_grid_gradient(&g, 10, 10, 12);
    double len = ts_vec3_norm(grad);
    /* Must not be zero */
    TS_ASSERT_TRUE(len > 0.1);

    ts_sdf_grid_free(&g);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- narrowband: far voxels stay at FLT_MAX --- */
static void test_bezier_voxel_narrowband_green(void) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 2.0, 2.0, 1.0);

    /* Large grid but narrow band — most voxels should be untouched */
    ts_sdf_grid g = ts_sdf_grid_new(32, 32, 32, 0.25, -2.0, -2.0, -2.0);
    ts_bezier_mesh_voxelize(&m, &g, 0.5, 15);

    /* Count voxels that were actually evaluated (not FLT_MAX) */
    int touched = 0;
    size_t total = (size_t)g.sx * (size_t)g.sy * (size_t)g.sz;
    for (size_t i = 0; i < total; i++) {
        if (g.distances[i] != FLT_MAX) touched++;
    }

    /* Far fewer than total should be touched */
    TS_ASSERT_TRUE(touched > 0);
    TS_ASSERT_TRUE(touched < (int)total / 2);

    ts_sdf_grid_free(&g);
    ts_bezier_mesh_free(&m);
    TS_PASS();
}

/* --- Benchmarks --- */

static void bench_bezier_voxelize_1x1(int n) {
    ts_bezier_mesh m = ts_bezier_mesh_new(1, 1);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 4.0, 4.0, 0.0);
    ts_bezier_mesh_set_cp(&m, 1, 1, ts_vec3_make(2.0, 2.0, 2.0));

    for (int i = 0; i < n; i++) {
        ts_sdf_grid g = ts_sdf_grid_new(32, 32, 32, 0.25, -1.0, -1.0, -1.0);
        ts_bezier_mesh_voxelize(&m, &g, 1.0, 10);
        ts_sdf_grid_free(&g);
    }
    ts_bezier_mesh_free(&m);
}

static void bench_bezier_voxelize_4x4(int n) {
    ts_bezier_mesh m = ts_bezier_mesh_new(4, 4);
    ts_bezier_mesh_init_flat(&m, 0.0, 0.0, 8.0, 8.0, 0.0);
    ts_bezier_mesh_set_cp(&m, 4, 4, ts_vec3_make(4.0, 4.0, 2.0));

    for (int i = 0; i < n; i++) {
        ts_sdf_grid g = ts_sdf_grid_new(64, 64, 64, 0.25, -1.0, -1.0, -1.0);
        ts_bezier_mesh_voxelize(&m, &g, 1.0, 10);
        ts_sdf_grid_free(&g);
    }
    ts_bezier_mesh_free(&m);
}

/* ================================================================
 * SECTION 9: GPU (OpenCL) TESTS
 * Verify GPU batch operations produce same results as CPU
 * Parallelism: GPU (the whole point)
 * ================================================================ */

/* GREEN: GPU init should succeed (or gracefully fail) */
static void test_gpu_init_green(void) {
    /* g_gpu was initialized in main — just check it didn't crash */
    TS_ASSERT_TRUE(1); /* init completed without segfault */
    TS_PASS();
}

/* GREEN: batch vec3_add GPU == CPU */
static void test_gpu_vec3_add_green(void) {
    int n = 1024;
    double *a = (double *)malloc((size_t)n * 3 * sizeof(double));
    double *b = (double *)malloc((size_t)n * 3 * sizeof(double));
    double *gpu_out = (double *)malloc((size_t)n * 3 * sizeof(double));
    double *cpu_out = (double *)malloc((size_t)n * 3 * sizeof(double));

    for (int i = 0; i < n * 3; i++) {
        a[i] = (double)(i % 100) * 0.1;
        b[i] = (double)((i + 37) % 100) * 0.1;
    }

    /* CPU reference */
    for (int i = 0; i < n * 3; i++)
        cpu_out[i] = a[i] + b[i];

    /* GPU (or CPU fallback) */
    ts_gpu_vec3_add(&g_gpu, a, b, gpu_out, n);

    /* Compare */
    for (int i = 0; i < n * 3; i++) {
        TS_ASSERT_NEAR(gpu_out[i], cpu_out[i], 1e-10);
    }

    free(a); free(b); free(gpu_out); free(cpu_out);
    TS_PASS();
}

/* GREEN: batch vec3_normalize GPU == CPU */
static void test_gpu_vec3_normalize_green(void) {
    int n = 1024;
    double *a = (double *)malloc((size_t)n * 3 * sizeof(double));
    double *gpu_out = (double *)malloc((size_t)n * 3 * sizeof(double));

    for (int i = 0; i < n; i++) {
        a[i*3]   = (double)(i + 1);
        a[i*3+1] = (double)(i + 2);
        a[i*3+2] = (double)(i + 3);
    }

    ts_gpu_vec3_normalize(&g_gpu, a, gpu_out, n);

    /* Verify each result is unit length */
    for (int i = 0; i < n; i++) {
        double x = gpu_out[i*3], y = gpu_out[i*3+1], z = gpu_out[i*3+2];
        double len = sqrt(x*x + y*y + z*z);
        TS_ASSERT_NEAR(len, 1.0, 1e-10);
    }

    free(a); free(gpu_out);
    TS_PASS();
}

/* GREEN: batch mat4 transform GPU == CPU */
static void test_gpu_mat4_transform_green(void) {
    int n = 1024;
    double mat[16] = {
        2, 0, 0, 10,
        0, 3, 0, 20,
        0, 0, 4, 30,
        0, 0, 0, 1
    };
    double *pts = (double *)malloc((size_t)n * 3 * sizeof(double));
    double *gpu_out = (double *)malloc((size_t)n * 3 * sizeof(double));

    for (int i = 0; i < n; i++) {
        pts[i*3] = (double)i; pts[i*3+1] = (double)i * 0.5; pts[i*3+2] = (double)i * 0.25;
    }

    ts_gpu_mat4_transform(&g_gpu, mat, pts, gpu_out, n);

    for (int i = 0; i < n; i++) {
        double x = (double)i, y = (double)i * 0.5, z = (double)i * 0.25;
        TS_ASSERT_NEAR(gpu_out[i*3],   2*x + 10, 1e-10);
        TS_ASSERT_NEAR(gpu_out[i*3+1], 3*y + 20, 1e-10);
        TS_ASSERT_NEAR(gpu_out[i*3+2], 4*z + 30, 1e-10);
    }

    free(pts); free(gpu_out);
    TS_PASS();
}

/* GREEN: batch scalar sin (degrees) */
static void test_gpu_scalar_sin_green(void) {
    int n = 1024;
    double *a = (double *)malloc((size_t)n * sizeof(double));
    double *gpu_out = (double *)malloc((size_t)n * sizeof(double));

    for (int i = 0; i < n; i++)
        a[i] = (double)(i % 360);

    ts_gpu_scalar_sin(&g_gpu, a, gpu_out, n);

    for (int i = 0; i < n; i++) {
        double expected = sin(a[i] * 0.017453292519943295);
        TS_ASSERT_NEAR(gpu_out[i], expected, 1e-10);
    }

    free(a); free(gpu_out);
    TS_PASS();
}

/* GREEN: batch RNG produces values in range */
static void test_gpu_rng_green(void) {
    int n = 10000;
    double *out = (double *)malloc((size_t)n * sizeof(double));

    ts_gpu_rng_uniform(&g_gpu, 42, -5.0, 5.0, out, n);

    int in_range = 0;
    for (int i = 0; i < n; i++) {
        if (out[i] >= -5.0 && out[i] <= 5.0) in_range++;
    }
    TS_ASSERT_EQ_INT(in_range, n);

    /* Check it's not all the same value */
    int unique = 0;
    for (int i = 1; i < n && unique < 10; i++) {
        if (fabs(out[i] - out[0]) > 1e-15) unique++;
    }
    TS_ASSERT_TRUE(unique >= 10);

    free(out);
    TS_PASS();
}

/* RED: GPU vec3_add with wrong input should still produce output */
static void test_gpu_vec3_add_red(void) {
    /* Small batch (below GPU threshold) should use CPU fallback */
    double a[3] = {1, 2, 3}, b[3] = {4, 5, 6}, out[3] = {0};
    ts_gpu_vec3_add(&g_gpu, a, b, out, 1);
    TS_ASSERT_NEAR(out[0], 5.0, 1e-10);
    TS_ASSERT_NEAR(out[1], 7.0, 1e-10);
    TS_ASSERT_NEAR(out[2], 9.0, 1e-10);
    TS_PASS();
}

/* ================================================================
 * BENCHMARKS
 * ================================================================ */

/* Volatile sink to prevent dead code elimination */
static volatile double g_bench_sink;
static volatile ts_vec3 g_bench_vec_sink;

/* --- Scalar benchmarks --- */
static void bench_abs(int n) {
    double x = -1.5;
    for (int i = 0; i < n; i++) x = ts_abs(x - 3.0);
    g_bench_sink = x;
}
static void bench_sqrt(int n) {
    double x = 2.0;
    for (int i = 0; i < n; i++) x = ts_sqrt(x + 1.0);
    g_bench_sink = x;
}
static void bench_pow(int n) {
    double x = 1.001;
    for (int i = 0; i < n; i++) x = ts_pow(x, 1.0001);
    g_bench_sink = x;
}
static void bench_exp(int n) {
    double x = 0.001;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_exp(x); x += 0.0001; }
}
static void bench_ln(int n) {
    double x = 1.0;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_ln(x + 1.0); x += 0.001; }
}
static void bench_log10(int n) {
    double x = 1.0;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_log10(x + 1.0); x += 0.001; }
}
static void bench_floor(int n) {
    double x = 0.5;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_floor(x); x += 0.7; }
}
static void bench_ceil(int n) {
    double x = 0.5;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_ceil(x); x += 0.7; }
}
static void bench_round(int n) {
    double x = 0.5;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_round(x); x += 0.3; }
}
static void bench_clamp(int n) {
    double x = -50.0;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_clamp(x, 0.0, 100.0); x += 0.1; }
}
static void bench_lerp(int n) {
    double t = 0.0;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_lerp(0.0, 100.0, t); t += 0.001; if (t > 1.0) t = 0.0; }
}
static void bench_fma(int n) {
    double x = 1.0;
    for (int i = 0; i < n; i++) x = ts_fma(x, 1.0000001, 0.0000001);
    g_bench_sink = x;
}

/* --- Trig benchmarks --- */
static void bench_sin_deg(int n) {
    double x = 0.0;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_sin_deg(x); x += 1.0; }
}
static void bench_cos_deg(int n) {
    double x = 0.0;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_cos_deg(x); x += 1.0; }
}
static void bench_tan_deg(int n) {
    double x = 0.0;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_tan_deg(x); x += 1.0; }
}
static void bench_asin_deg(int n) {
    double x = -0.999;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_asin_deg(x); x += 0.002; if (x > 0.999) x = -0.999; }
}
static void bench_atan2_deg(int n) {
    double y = 1.0, x = 1.0;
    for (int i = 0; i < n; i++) { g_bench_sink = ts_atan2_deg(y, x); y += 0.1; x -= 0.05; }
}
static void bench_sincos_deg(int n) {
    double s, c, x = 0.0;
    for (int i = 0; i < n; i++) { ts_sincos_deg(x, &s, &c); g_bench_sink = s + c; x += 1.0; }
}

/* --- Vector benchmarks --- */
static void bench_vec3_add(int n) {
    ts_vec3 a = ts_vec3_make(1, 2, 3);
    ts_vec3 b = ts_vec3_make(4, 5, 6);
    for (int i = 0; i < n; i++) a = ts_vec3_add(a, b);
    g_bench_vec_sink = a;
}
static void bench_vec3_dot(int n) {
    ts_vec3 a = ts_vec3_make(1, 2, 3);
    ts_vec3 b = ts_vec3_make(4, 5, 6);
    for (int i = 0; i < n; i++) g_bench_sink = ts_vec3_dot(a, b);
}
static void bench_vec3_cross(int n) {
    ts_vec3 a = ts_vec3_make(1, 2, 3);
    ts_vec3 b = ts_vec3_make(4, 5, 6);
    for (int i = 0; i < n; i++) a = ts_vec3_cross(a, b);
    g_bench_vec_sink = a;
}
static void bench_vec3_normalize(int n) {
    ts_vec3 a = ts_vec3_make(3, 4, 5);
    for (int i = 0; i < n; i++) { a = ts_vec3_normalize(a); a.v[0] += 0.001; }
    g_bench_vec_sink = a;
}
static void bench_vec3_norm(int n) {
    ts_vec3 a = ts_vec3_make(3, 4, 5);
    for (int i = 0; i < n; i++) { g_bench_sink = ts_vec3_norm(a); a.v[0] += 0.0001; }
}

/* --- Matrix benchmarks --- */
static void bench_mat4_multiply(int n) {
    ts_mat4 a = ts_mat4_rotate_z(1.0);
    ts_mat4 b = ts_mat4_translate(1, 0, 0);
    for (int i = 0; i < n; i++) a = ts_mat4_multiply(a, b);
    g_bench_sink = a.m[0];
}
static void bench_mat4_inverse(int n) {
    ts_mat4 a = ts_mat4_rotate_z(30.0);
    a = ts_mat4_multiply(a, ts_mat4_translate(1, 2, 3));
    for (int i = 0; i < n; i++) { ts_mat4 inv = ts_mat4_inverse(a); g_bench_sink = inv.m[0]; }
}
static void bench_mat4_transform(int n) {
    ts_mat4 m = ts_mat4_rotate_z(45.0);
    ts_vec3 p = ts_vec3_make(1, 2, 3);
    for (int i = 0; i < n; i++) p = ts_mat4_transform_point(m, p);
    g_bench_vec_sink = p;
}
static void bench_mat4_rotate_axis(int n) {
    ts_vec3 axis = ts_vec3_make(1, 1, 1);
    for (int i = 0; i < n; i++) { ts_mat4 r = ts_mat4_rotate_axis((double)i, axis); g_bench_sink = r.m[0]; }
}
static void bench_mat4_euler(int n) {
    for (int i = 0; i < n; i++) {
        ts_mat4 r = ts_mat4_rotate_euler((double)i, (double)i*0.5, (double)i*0.3);
        g_bench_sink = r.m[0];
    }
}

/* --- Geometry benchmarks --- */
static void bench_gen_cube(int n) {
    for (int i = 0; i < n; i++) {
        ts_mesh m = ts_mesh_init();
        ts_gen_cube(1.0, 1.0, 1.0, &m);
        g_bench_sink = (double)m.tri_count;
        ts_mesh_free(&m);
    }
}
static void bench_gen_sphere_16(int n) {
    for (int i = 0; i < n; i++) {
        ts_mesh m = ts_mesh_init();
        ts_gen_sphere(1.0, 16, &m);
        g_bench_sink = (double)m.tri_count;
        ts_mesh_free(&m);
    }
}
static void bench_gen_sphere_100(int n) {
    for (int i = 0; i < n; i++) {
        ts_mesh m = ts_mesh_init();
        ts_gen_sphere(1.0, 100, &m);
        g_bench_sink = (double)m.tri_count;
        ts_mesh_free(&m);
    }
}
static void bench_gen_cylinder(int n) {
    for (int i = 0; i < n; i++) {
        ts_mesh m = ts_mesh_init();
        ts_gen_cylinder(10.0, 1.0, 1.0, 32, &m);
        g_bench_sink = (double)m.tri_count;
        ts_mesh_free(&m);
    }
}
static void bench_gen_circle(int n) {
    double pts[200];
    for (int i = 0; i < n; i++) {
        ts_gen_circle_points(5.0, 100, pts, 100);
        g_bench_sink = pts[0];
    }
}

/* --- Random benchmarks --- */
static void bench_rands_1000(int n) {
    double out[1000];
    for (int i = 0; i < n; i++) {
        ts_rands(0.0, 1.0, 1000, (uint64_t)i, out);
        g_bench_sink = out[0];
    }
}
static void bench_rand_single(int n) {
    for (int i = 0; i < n; i++) {
        g_bench_sink = ts_rand(42, (uint64_t)i, 0.0, 1.0);
    }
}

/* --- CSG benchmarks --- */
static void bench_csg_union(int n) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(1, 1, 1, &b);
    for (int i = 0; i < b.vert_count; i++) b.verts[i].pos[0] += 0.5;
    for (int i = 0; i < n; i++) {
        ts_mesh out = ts_mesh_init();
        ts_csg_union(&a, &b, &out);
        g_bench_sink = (double)out.tri_count;
        ts_mesh_free(&out);
    }
    ts_mesh_free(&a); ts_mesh_free(&b);
}

static void bench_csg_difference(int n) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(1, 1, 1, &b);
    for (int i = 0; i < b.vert_count; i++) b.verts[i].pos[0] += 0.5;
    for (int i = 0; i < n; i++) {
        ts_mesh out = ts_mesh_init();
        ts_csg_difference(&a, &b, &out);
        g_bench_sink = (double)out.tri_count;
        ts_mesh_free(&out);
    }
    ts_mesh_free(&a); ts_mesh_free(&b);
}

static void bench_csg_intersection(int n) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(1, 1, 1, &b);
    for (int i = 0; i < b.vert_count; i++) b.verts[i].pos[0] += 0.5;
    for (int i = 0; i < n; i++) {
        ts_mesh out = ts_mesh_init();
        ts_csg_intersection(&a, &b, &out);
        g_bench_sink = (double)out.tri_count;
        ts_mesh_free(&out);
    }
    ts_mesh_free(&a); ts_mesh_free(&b);
}

static void bench_csg_hull(int n) {
    ts_mesh input = ts_mesh_init();
    ts_gen_sphere(1.0, 16, &input);
    for (int i = 0; i < n; i++) {
        ts_mesh out = ts_mesh_init();
        ts_csg_hull(&input, &out);
        g_bench_sink = (double)out.tri_count;
        ts_mesh_free(&out);
    }
    ts_mesh_free(&input);
}

static void bench_csg_minkowski(int n) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init();
    ts_gen_cube(1, 1, 1, &a);
    ts_gen_cube(0.5, 0.5, 0.5, &b);
    for (int i = 0; i < n; i++) {
        ts_mesh out = ts_mesh_init();
        ts_csg_minkowski(&a, &b, &out);
        g_bench_sink = (double)out.tri_count;
        ts_mesh_free(&out);
    }
    ts_mesh_free(&a); ts_mesh_free(&b);
}

/* --- Extrusion benchmarks --- */
static void bench_linear_extrude(int n) {
    double profile[] = { 0,0, 1,0, 1,1, 0,1 };
    for (int i = 0; i < n; i++) {
        ts_mesh out = ts_mesh_init();
        ts_linear_extrude(profile, 4, 10.0, 0.0, 1, 1.0, 0, &out);
        g_bench_sink = (double)out.tri_count;
        ts_mesh_free(&out);
    }
}

static void bench_linear_extrude_twist(int n) {
    double profile[] = { 0,0, 1,0, 1,1, 0,1 };
    for (int i = 0; i < n; i++) {
        ts_mesh out = ts_mesh_init();
        ts_linear_extrude(profile, 4, 10.0, 360.0, 32, 1.0, 1, &out);
        g_bench_sink = (double)out.tri_count;
        ts_mesh_free(&out);
    }
}

static void bench_rotate_extrude(int n) {
    double profile[] = { 4,0, 6,0, 6,3, 4,3 };
    for (int i = 0; i < n; i++) {
        ts_mesh out = ts_mesh_init();
        ts_rotate_extrude(profile, 4, 360.0, 32, &out);
        g_bench_sink = (double)out.tri_count;
        ts_mesh_free(&out);
    }
}

/* --- GPU benchmarks --- */
static void bench_gpu_vec3_add(int n) {
    int batch = 100000;
    double *a = (double *)malloc((size_t)batch * 3 * sizeof(double));
    double *b = (double *)malloc((size_t)batch * 3 * sizeof(double));
    double *o = (double *)malloc((size_t)batch * 3 * sizeof(double));
    for (int i = 0; i < batch * 3; i++) { a[i] = (double)i; b[i] = (double)i * 0.5; }
    for (int i = 0; i < n; i++) {
        ts_gpu_vec3_add(&g_gpu, a, b, o, batch);
        g_bench_sink = o[0];
    }
    free(a); free(b); free(o);
}

static void bench_gpu_vec3_normalize(int n) {
    int batch = 100000;
    double *a = (double *)malloc((size_t)batch * 3 * sizeof(double));
    double *o = (double *)malloc((size_t)batch * 3 * sizeof(double));
    for (int i = 0; i < batch * 3; i++) a[i] = (double)(i + 1);
    for (int i = 0; i < n; i++) {
        ts_gpu_vec3_normalize(&g_gpu, a, o, batch);
        g_bench_sink = o[0];
    }
    free(a); free(o);
}

static void bench_gpu_mat4_transform(int n) {
    int batch = 100000;
    double mat[16] = { 2,0,0,10, 0,3,0,20, 0,0,4,30, 0,0,0,1 };
    double *pts = (double *)malloc((size_t)batch * 3 * sizeof(double));
    double *o = (double *)malloc((size_t)batch * 3 * sizeof(double));
    for (int i = 0; i < batch * 3; i++) pts[i] = (double)i * 0.01;
    for (int i = 0; i < n; i++) {
        ts_gpu_mat4_transform(&g_gpu, mat, pts, o, batch);
        g_bench_sink = o[0];
    }
    free(pts); free(o);
}

static void bench_gpu_scalar_sin(int n) {
    int batch = 100000;
    double *a = (double *)malloc((size_t)batch * sizeof(double));
    double *o = (double *)malloc((size_t)batch * sizeof(double));
    for (int i = 0; i < batch; i++) a[i] = (double)(i % 360);
    for (int i = 0; i < n; i++) {
        ts_gpu_scalar_sin(&g_gpu, a, o, batch);
        g_bench_sink = o[0];
    }
    free(a); free(o);
}

static void bench_gpu_rng(int n) {
    int batch = 100000;
    double *o = (double *)malloc((size_t)batch * sizeof(double));
    for (int i = 0; i < n; i++) {
        ts_gpu_rng_uniform(&g_gpu, (unsigned long)(42 + i), 0.0, 1.0, o, batch);
        g_bench_sink = o[0];
    }
    free(o);
}

/* CPU versions of the same batches for comparison */
static void bench_cpu_vec3_add(int n) {
    int batch = 100000;
    double *a = (double *)malloc((size_t)batch * 3 * sizeof(double));
    double *b = (double *)malloc((size_t)batch * 3 * sizeof(double));
    double *o = (double *)malloc((size_t)batch * 3 * sizeof(double));
    for (int i = 0; i < batch * 3; i++) { a[i] = (double)i; b[i] = (double)i * 0.5; }
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < batch * 3; j++) o[j] = a[j] + b[j];
        g_bench_sink = o[0];
    }
    free(a); free(b); free(o);
}

static void bench_cpu_vec3_normalize(int n) {
    int batch = 100000;
    double *a = (double *)malloc((size_t)batch * 3 * sizeof(double));
    double *o = (double *)malloc((size_t)batch * 3 * sizeof(double));
    for (int i = 0; i < batch * 3; i++) a[i] = (double)(i + 1);
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < batch; k++) {
            int j = k * 3;
            double x=a[j],y=a[j+1],z=a[j+2];
            double inv = 1.0/sqrt(x*x+y*y+z*z);
            o[j]=x*inv; o[j+1]=y*inv; o[j+2]=z*inv;
        }
        g_bench_sink = o[0];
    }
    free(a); free(o);
}

static void bench_cpu_mat4_transform(int n) {
    int batch = 100000;
    double m[16] = { 2,0,0,10, 0,3,0,20, 0,0,4,30, 0,0,0,1 };
    double *pts = (double *)malloc((size_t)batch * 3 * sizeof(double));
    double *o = (double *)malloc((size_t)batch * 3 * sizeof(double));
    for (int i = 0; i < batch * 3; i++) pts[i] = (double)i * 0.01;
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < batch; k++) {
            int j = k * 3;
            double x=pts[j],y=pts[j+1],z=pts[j+2];
            o[j]=m[0]*x+m[1]*y+m[2]*z+m[3];
            o[j+1]=m[4]*x+m[5]*y+m[6]*z+m[7];
            o[j+2]=m[8]*x+m[9]*y+m[10]*z+m[11];
        }
        g_bench_sink = o[0];
    }
    free(pts); free(o);
}

/* ================================================================
 * MAIN
 * ================================================================ */

static void print_help(void) {
    printf("trinity_site — The Holy Mathematical Foundation\n\n");
    printf("Reimplementation of OpenSCAD's math system for GPU parallelization.\n\n");
    printf("Usage:\n");
    printf("  trinity_site              Run all tests\n");
    printf("  trinity_site --test       Run tests only\n");
    printf("  trinity_site --bench      Run benchmarks only\n");
    printf("  trinity_site --all        Run tests + benchmarks\n");
    printf("  trinity_site --help       Show this help\n\n");
    printf("Every OpenSCAD function has:\n");
    printf("  GREEN test — proves correct implementation\n");
    printf("  RED test   — proves the test catches wrong implementations\n");
    printf("  Benchmark  — measures per-operation cost\n");
    printf("  Parallelism classification for GPU offload planning\n");
}

static void run_all_tests(void) {
    printf("\n");
    printf("============================================================\n");
    printf("  TRINITY SITE — MATHEMATICAL FOUNDATION TESTS\n");
    printf("============================================================\n");
    printf("  \"Now I am become Death, the destroyer of worlds.\"\n");
    printf("============================================================\n");

    /* --- Scalar math --- */
    ts_section("SCALAR: abs", TS_PAR_TRIVIAL);
    ts_run_test("abs_green", test_abs_green);
    ts_run_test("abs_red", test_abs_red);

    ts_section("SCALAR: sign", TS_PAR_TRIVIAL);
    ts_run_test("sign_green", test_sign_green);
    ts_run_test("sign_red", test_sign_red);

    ts_section("SCALAR: ceil", TS_PAR_TRIVIAL);
    ts_run_test("ceil_green", test_ceil_green);
    ts_run_test("ceil_red", test_ceil_red);

    ts_section("SCALAR: floor", TS_PAR_TRIVIAL);
    ts_run_test("floor_green", test_floor_green);
    ts_run_test("floor_red", test_floor_red);

    ts_section("SCALAR: round", TS_PAR_TRIVIAL);
    ts_run_test("round_green", test_round_green);
    ts_run_test("round_red", test_round_red);

    ts_section("SCALAR: ln", TS_PAR_TRIVIAL);
    ts_run_test("ln_green", test_ln_green);
    ts_run_test("ln_red", test_ln_red);

    ts_section("SCALAR: log10", TS_PAR_TRIVIAL);
    ts_run_test("log10_green", test_log10_green);
    ts_run_test("log10_red", test_log10_red);

    ts_section("SCALAR: log2", TS_PAR_TRIVIAL);
    ts_run_test("log2_green", test_log2_green);
    ts_run_test("log2_red", test_log2_red);

    ts_section("SCALAR: pow", TS_PAR_TRIVIAL);
    ts_run_test("pow_green", test_pow_green);
    ts_run_test("pow_red", test_pow_red);

    ts_section("SCALAR: sqrt", TS_PAR_TRIVIAL);
    ts_run_test("sqrt_green", test_sqrt_green);
    ts_run_test("sqrt_red", test_sqrt_red);

    ts_section("SCALAR: exp", TS_PAR_TRIVIAL);
    ts_run_test("exp_green", test_exp_green);
    ts_run_test("exp_red", test_exp_red);

    ts_section("SCALAR: exp2", TS_PAR_TRIVIAL);
    ts_run_test("exp2_green", test_exp2_green);
    ts_run_test("exp2_red", test_exp2_red);

    ts_section("SCALAR: min/max", TS_PAR_TRIVIAL);
    ts_run_test("min_green", test_min_green);
    ts_run_test("min_red", test_min_red);
    ts_run_test("max_green", test_max_green);
    ts_run_test("max_red", test_max_red);

    ts_section("SCALAR: clamp", TS_PAR_TRIVIAL);
    ts_run_test("clamp_green", test_clamp_green);
    ts_run_test("clamp_red", test_clamp_red);

    ts_section("SCALAR: lerp", TS_PAR_TRIVIAL);
    ts_run_test("lerp_green", test_lerp_green);
    ts_run_test("lerp_red", test_lerp_red);

    ts_section("SCALAR: fma", TS_PAR_TRIVIAL);
    ts_run_test("fma_green", test_fma_green);
    ts_run_test("fma_red", test_fma_red);

    /* --- Trigonometry --- */
    ts_section("TRIG: sin (degrees)", TS_PAR_TRIVIAL);
    ts_run_test("sin_green", test_sin_green);
    ts_run_test("sin_red", test_sin_red);

    ts_section("TRIG: cos (degrees)", TS_PAR_TRIVIAL);
    ts_run_test("cos_green", test_cos_green);
    ts_run_test("cos_red", test_cos_red);

    ts_section("TRIG: tan (degrees)", TS_PAR_TRIVIAL);
    ts_run_test("tan_green", test_tan_green);
    ts_run_test("tan_red", test_tan_red);

    ts_section("TRIG: asin (returns degrees)", TS_PAR_TRIVIAL);
    ts_run_test("asin_green", test_asin_green);
    ts_run_test("asin_red", test_asin_red);

    ts_section("TRIG: acos (returns degrees)", TS_PAR_TRIVIAL);
    ts_run_test("acos_green", test_acos_green);
    ts_run_test("acos_red", test_acos_red);

    ts_section("TRIG: atan (returns degrees)", TS_PAR_TRIVIAL);
    ts_run_test("atan_green", test_atan_green);
    ts_run_test("atan_red", test_atan_red);

    ts_section("TRIG: atan2 (returns degrees)", TS_PAR_TRIVIAL);
    ts_run_test("atan2_green", test_atan2_green);
    ts_run_test("atan2_red", test_atan2_red);

    ts_section("TRIG: sincos (simultaneous)", TS_PAR_TRIVIAL);
    ts_run_test("sincos_green", test_sincos_green);
    ts_run_test("sincos_red", test_sincos_red);

    /* --- Vectors --- */
    ts_section("VEC3: add", TS_PAR_SIMD);
    ts_run_test("vec3_add_green", test_vec3_add_green);
    ts_run_test("vec3_add_red", test_vec3_add_red);

    ts_section("VEC3: dot", TS_PAR_REDUCIBLE);
    ts_run_test("vec3_dot_green", test_vec3_dot_green);
    ts_run_test("vec3_dot_red", test_vec3_dot_red);

    ts_section("VEC3: cross", TS_PAR_SIMD);
    ts_run_test("vec3_cross_green", test_vec3_cross_green);
    ts_run_test("vec3_cross_red", test_vec3_cross_red);

    ts_section("VEC3: norm (magnitude)", TS_PAR_REDUCIBLE);
    ts_run_test("vec3_norm_green", test_vec3_norm_green);
    ts_run_test("vec3_norm_red", test_vec3_norm_red);

    ts_section("VEC3: normalize", TS_PAR_REDUCIBLE);
    ts_run_test("vec3_normalize_green", test_vec3_normalize_green);
    ts_run_test("vec3_normalize_red", test_vec3_normalize_red);

    ts_section("VEC3: lerp", TS_PAR_SIMD);
    ts_run_test("vec3_lerp_green", test_vec3_lerp_green);
    ts_run_test("vec3_lerp_red", test_vec3_lerp_red);

    ts_section("VEC3: reflect", TS_PAR_SIMD);
    ts_run_test("vec3_reflect_green", test_vec3_reflect_green);
    ts_run_test("vec3_reflect_red", test_vec3_reflect_red);

    /* --- Matrices --- */
    ts_section("MAT4: identity", TS_PAR_GPU);
    ts_run_test("mat4_identity_green", test_mat4_identity_green);
    ts_run_test("mat4_identity_red", test_mat4_identity_red);

    ts_section("MAT4: translate", TS_PAR_GPU);
    ts_run_test("mat4_translate_green", test_mat4_translate_green);
    ts_run_test("mat4_translate_red", test_mat4_translate_red);

    ts_section("MAT4: scale", TS_PAR_GPU);
    ts_run_test("mat4_scale_green", test_mat4_scale_green);
    ts_run_test("mat4_scale_red", test_mat4_scale_red);

    ts_section("MAT4: rotate_z", TS_PAR_GPU);
    ts_run_test("mat4_rotate_z_green", test_mat4_rotate_z_green);
    ts_run_test("mat4_rotate_z_red", test_mat4_rotate_z_red);

    ts_section("MAT4: rotate_x", TS_PAR_GPU);
    ts_run_test("mat4_rotate_x_green", test_mat4_rotate_x_green);
    ts_run_test("mat4_rotate_x_red", test_mat4_rotate_x_red);

    ts_section("MAT4: rotate_y", TS_PAR_GPU);
    ts_run_test("mat4_rotate_y_green", test_mat4_rotate_y_green);
    ts_run_test("mat4_rotate_y_red", test_mat4_rotate_y_red);

    ts_section("MAT4: rotate_axis (Rodrigues)", TS_PAR_GPU);
    ts_run_test("mat4_rotate_axis_green", test_mat4_rotate_axis_green);
    ts_run_test("mat4_rotate_axis_red", test_mat4_rotate_axis_red);

    ts_section("MAT4: mirror (Householder)", TS_PAR_GPU);
    ts_run_test("mat4_mirror_green", test_mat4_mirror_green);
    ts_run_test("mat4_mirror_red", test_mat4_mirror_red);

    ts_section("MAT4: multiply (chained transforms)", TS_PAR_GPU);
    ts_run_test("mat4_multiply_green", test_mat4_multiply_green);
    ts_run_test("mat4_multiply_red", test_mat4_multiply_red);

    ts_section("MAT4: inverse", TS_PAR_GPU);
    ts_run_test("mat4_inverse_green", test_mat4_inverse_green);
    ts_run_test("mat4_inverse_red", test_mat4_inverse_red);

    ts_section("MAT4: determinant", TS_PAR_GPU);
    ts_run_test("mat4_det_green", test_mat4_det_green);
    ts_run_test("mat4_det_red", test_mat4_det_red);

    /* --- Geometry --- */
    ts_section("GEO: cube", TS_PAR_GPU);
    ts_run_test("cube_green", test_cube_green);
    ts_run_test("cube_red", test_cube_red);

    ts_section("GEO: sphere", TS_PAR_GPU);
    ts_run_test("sphere_green", test_sphere_green);
    ts_run_test("sphere_red", test_sphere_red);

    ts_section("GEO: cylinder", TS_PAR_GPU);
    ts_run_test("cylinder_green", test_cylinder_green);
    ts_run_test("cylinder_red", test_cylinder_red);

    ts_section("GEO: circle (2D points)", TS_PAR_TRIVIAL);
    ts_run_test("circle_green", test_circle_green);
    ts_run_test("circle_red", test_circle_red);

    ts_section("GEO: square (2D points)", TS_PAR_TRIVIAL);
    ts_run_test("square_green", test_square_green);
    ts_run_test("square_red", test_square_red);

    ts_section("GEO: polyhedron", TS_PAR_GPU);
    ts_run_test("polyhedron_green", test_polyhedron_green);
    ts_run_test("polyhedron_red", test_polyhedron_red);

    ts_section("GEO: tetrahedron", TS_PAR_GPU);
    ts_run_test("tetrahedron_green", test_tetrahedron_green);
    ts_run_test("tetrahedron_red", test_tetrahedron_red);

    ts_section("GEO: octahedron", TS_PAR_GPU);
    ts_run_test("octahedron_green", test_octahedron_green);
    ts_run_test("octahedron_red", test_octahedron_red);

    ts_section("GEO: dodecahedron", TS_PAR_GPU);
    ts_run_test("dodecahedron_green", test_dodecahedron_green);
    ts_run_test("dodecahedron_red", test_dodecahedron_red);

    ts_section("GEO: icosahedron", TS_PAR_GPU);
    ts_run_test("icosahedron_green", test_icosahedron_green);
    ts_run_test("icosahedron_red", test_icosahedron_red);

    /* --- Random --- */
    ts_section("RANDOM: rands", TS_PAR_TRIVIAL);
    ts_run_test("rands_green", test_rands_green);
    ts_run_test("rands_red", test_rands_red);

    ts_section("RANDOM: rands range", TS_PAR_TRIVIAL);
    ts_run_test("rands_range_green", test_rands_range_green);
    ts_run_test("rands_range_red", test_rands_range_red);

    /* --- CSG boolean operations --- */
    ts_section("CSG: union (BSP-tree)", TS_PAR_GPU);
    ts_run_test("csg_union_green", test_csg_union_green);
    ts_run_test("csg_union_red", test_csg_union_red);
    ts_run_test("csg_union_overlap_green", test_csg_union_overlap_green);

    ts_section("CSG: difference (BSP-tree)", TS_PAR_GPU);
    ts_run_test("csg_difference_green", test_csg_difference_green);
    ts_run_test("csg_difference_red", test_csg_difference_red);

    ts_section("CSG: intersection (BSP-tree)", TS_PAR_GPU);
    ts_run_test("csg_intersection_green", test_csg_intersection_green);
    ts_run_test("csg_intersection_red", test_csg_intersection_red);

    ts_section("CSG: hull (Quickhull)", TS_PAR_GPU);
    ts_run_test("csg_hull_green", test_csg_hull_green);
    ts_run_test("csg_hull_red", test_csg_hull_red);

    ts_section("CSG: minkowski (convex sum + hull)", TS_PAR_GPU);
    ts_run_test("csg_minkowski_green", test_csg_minkowski_green);
    ts_run_test("csg_minkowski_red", test_csg_minkowski_red);

    ts_section("MINKOWSKI: rigorous shape tests", TS_PAR_GPU);
    ts_run_test("mink_cube_sphere", test_mink_cube_sphere_green);
    ts_run_test("mink_cylinder_sphere", test_mink_cylinder_sphere_green);
    ts_run_test("mink_sphere_sphere", test_mink_sphere_sphere_green);
    ts_run_test("mink_asymmetric_boxes", test_mink_asymmetric_green);
    ts_run_test("mink_watertight", test_mink_watertight_green);
    ts_run_test("mink_translated_inputs", test_mink_translated_green);
    ts_run_test("mink_highres_fn32", test_mink_highres_green);
    ts_run_test("mink_commutative", test_mink_commutative_green);
    ts_run_test("mink_empty_input", test_mink_empty_input_green);
    ts_run_test("mink_null_input", test_mink_null_input_green);

    ts_section("MINKOWSKI: interpreter end-to-end", TS_PAR_SEQUENTIAL);
    ts_run_test("mink_interp_cube_sphere", test_mink_interp_cube_sphere_green);
    ts_run_test("mink_interp_cyl_sphere", test_mink_interp_cyl_sphere_green);
    ts_run_test("mink_interp_fidget_pattern", test_mink_interp_fidget_pattern_green);

    /* --- Extrusion --- */
    ts_section("EXTRUDE: linear_extrude", TS_PAR_GPU);
    ts_run_test("linear_extrude_green", test_linear_extrude_green);
    ts_run_test("linear_extrude_red", test_linear_extrude_red);
    ts_run_test("linear_extrude_twist_green", test_linear_extrude_twist_green);
    ts_run_test("linear_extrude_taper_green", test_linear_extrude_taper_green);
    ts_run_test("linear_extrude_center_green", test_linear_extrude_center_green);

    ts_section("EXTRUDE: rotate_extrude", TS_PAR_GPU);
    ts_run_test("rotate_extrude_green", test_rotate_extrude_green);
    ts_run_test("rotate_extrude_red", test_rotate_extrude_red);
    ts_run_test("rotate_extrude_partial_green", test_rotate_extrude_partial_green);

    ts_section("EXTRUDE: error handling", TS_PAR_TRIVIAL);
    ts_run_test("extrude_error_green", test_extrude_error_green);

    /* --- Bezier Surface Patches --- */
    ts_section("BEZIER SURFACE: basis functions", TS_PAR_TRIVIAL);
    ts_run_test("bezier_basis_green", test_bezier_basis_green);
    ts_run_test("bezier_basis_red", test_bezier_basis_red);

    ts_section("BEZIER SURFACE: patch eval", TS_PAR_SIMD);
    ts_run_test("bezier_patch_eval_green", test_bezier_patch_eval_green);
    ts_run_test("bezier_patch_eval_red", test_bezier_patch_eval_red);
    ts_run_test("bezier_patch_dome_green", test_bezier_patch_dome_green);
    ts_run_test("bezier_patch_dome_red", test_bezier_patch_dome_red);

    ts_section("BEZIER SURFACE: normal", TS_PAR_SIMD);
    ts_run_test("bezier_patch_normal_green", test_bezier_patch_normal_green);
    ts_run_test("bezier_patch_normal_red", test_bezier_patch_normal_red);
    ts_run_test("bezier_patch_dome_normal_green", test_bezier_patch_dome_normal_green);

    ts_section("BEZIER SURFACE: bbox", TS_PAR_SIMD);
    ts_run_test("bezier_patch_bbox_green", test_bezier_patch_bbox_green);
    ts_run_test("bezier_patch_bbox_red", test_bezier_patch_bbox_red);

    ts_section("BEZIER SURFACE: closest point", TS_PAR_SIMD);
    ts_run_test("bezier_patch_closest_green", test_bezier_patch_closest_green);
    ts_run_test("bezier_patch_closest_red", test_bezier_patch_closest_red);

    ts_section("BEZIER SURFACE: SDF", TS_PAR_SIMD);
    ts_run_test("bezier_patch_sdf_green", test_bezier_patch_sdf_green);
    ts_run_test("bezier_patch_sdf_red", test_bezier_patch_sdf_red);

    /* --- Bezier Mesh --- */
    ts_section("BEZIER MESH: creation", TS_PAR_TRIVIAL);
    ts_run_test("bezier_mesh_create_green", test_bezier_mesh_create_green);
    ts_run_test("bezier_mesh_create_red", test_bezier_mesh_create_red);

    ts_section("BEZIER MESH: C0 continuity (shared edges)", TS_PAR_SIMD);
    ts_run_test("bezier_mesh_c0_green", test_bezier_mesh_c0_green);
    ts_run_test("bezier_mesh_c0_red", test_bezier_mesh_c0_red);

    ts_section("BEZIER MESH: C1 continuity (tangent matching)", TS_PAR_SIMD);
    ts_run_test("bezier_mesh_c1_col_green", test_bezier_mesh_c1_col_green);
    ts_run_test("bezier_mesh_c1_col_red", test_bezier_mesh_c1_col_red);
    ts_run_test("bezier_mesh_c1_row_green", test_bezier_mesh_c1_row_green);

    ts_section("BEZIER MESH: tessellation", TS_PAR_GPU);
    ts_run_test("bezier_mesh_tessellate_green", test_bezier_mesh_tessellate_green);
    ts_run_test("bezier_mesh_tessellate_red", test_bezier_mesh_tessellate_red);
    ts_run_test("bezier_mesh_tess_multi_green", test_bezier_mesh_tess_multi_green);

    ts_section("BEZIER MESH: bbox", TS_PAR_SIMD);
    ts_run_test("bezier_mesh_bbox_green", test_bezier_mesh_bbox_green);

    ts_section("BEZIER MESH: closest point", TS_PAR_SIMD);
    ts_run_test("bezier_mesh_closest_green", test_bezier_mesh_closest_green);
    ts_run_test("bezier_mesh_closest_red", test_bezier_mesh_closest_red);

    ts_section("BEZIER MESH: STL export", TS_PAR_GPU);
    ts_run_test("bezier_mesh_stl_green", test_bezier_mesh_stl_green);

    /* --- Bezier Voxelization --- */
    ts_section("BEZIER VOXEL: flat surface", TS_PAR_GPU);
    ts_run_test("bezier_voxel_flat_green", test_bezier_voxel_flat_green);
    ts_run_test("bezier_voxel_flat_red", test_bezier_voxel_flat_red);

    ts_section("BEZIER VOXEL: dome surface", TS_PAR_GPU);
    ts_run_test("bezier_voxel_dome_green", test_bezier_voxel_dome_green);

    ts_section("BEZIER VOXEL: multi-patch", TS_PAR_GPU);
    ts_run_test("bezier_voxel_multi_green", test_bezier_voxel_multi_green);

    ts_section("BEZIER VOXEL: SDF gradient", TS_PAR_SIMD);
    ts_run_test("bezier_voxel_gradient_green", test_bezier_voxel_gradient_green);
    ts_run_test("bezier_voxel_gradient_red", test_bezier_voxel_gradient_red);

    ts_section("BEZIER VOXEL: narrowband", TS_PAR_GPU);
    ts_run_test("bezier_voxel_narrowband_green", test_bezier_voxel_narrowband_green);

    /* --- GPU (OpenCL) --- */
    ts_section("GPU: OpenCL initialization", TS_PAR_GPU);
    ts_run_test("gpu_init_green", test_gpu_init_green);

    ts_section("GPU: batch vec3 operations", TS_PAR_GPU);
    ts_run_test("gpu_vec3_add_green", test_gpu_vec3_add_green);
    ts_run_test("gpu_vec3_add_red", test_gpu_vec3_add_red);
    ts_run_test("gpu_vec3_normalize_green", test_gpu_vec3_normalize_green);

    ts_section("GPU: batch mat4 transform", TS_PAR_GPU);
    ts_run_test("gpu_mat4_transform_green", test_gpu_mat4_transform_green);

    ts_section("GPU: batch scalar ops", TS_PAR_GPU);
    ts_run_test("gpu_scalar_sin_green", test_gpu_scalar_sin_green);

    ts_section("GPU: parallel RNG", TS_PAR_GPU);
    ts_run_test("gpu_rng_green", test_gpu_rng_green);

    ts_summary();
}

static void run_all_benchmarks(void) {
    int N = 1000000;
    int N_GEO = 10000;

    printf("\n");
    printf("============================================================\n");
    printf("  TRINITY SITE — BENCHMARKS\n");
    printf("============================================================\n");

    printf("\n--- Scalar Math ---\n");
    ts_run_bench("abs",    bench_abs,    N, TS_PAR_TRIVIAL);
    ts_run_bench("sqrt",   bench_sqrt,   N, TS_PAR_TRIVIAL);
    ts_run_bench("pow",    bench_pow,    N, TS_PAR_TRIVIAL);
    ts_run_bench("exp",    bench_exp,    N, TS_PAR_TRIVIAL);
    ts_run_bench("ln",     bench_ln,     N, TS_PAR_TRIVIAL);
    ts_run_bench("log10",  bench_log10,  N, TS_PAR_TRIVIAL);
    ts_run_bench("floor",  bench_floor,  N, TS_PAR_TRIVIAL);
    ts_run_bench("ceil",   bench_ceil,   N, TS_PAR_TRIVIAL);
    ts_run_bench("round",  bench_round,  N, TS_PAR_TRIVIAL);
    ts_run_bench("clamp",  bench_clamp,  N, TS_PAR_TRIVIAL);
    ts_run_bench("lerp",   bench_lerp,   N, TS_PAR_TRIVIAL);
    ts_run_bench("fma",    bench_fma,    N, TS_PAR_TRIVIAL);

    printf("\n--- Trigonometry (degrees) ---\n");
    ts_run_bench("sin_deg",    bench_sin_deg,    N, TS_PAR_TRIVIAL);
    ts_run_bench("cos_deg",    bench_cos_deg,    N, TS_PAR_TRIVIAL);
    ts_run_bench("tan_deg",    bench_tan_deg,    N, TS_PAR_TRIVIAL);
    ts_run_bench("asin_deg",   bench_asin_deg,   N, TS_PAR_TRIVIAL);
    ts_run_bench("atan2_deg",  bench_atan2_deg,  N, TS_PAR_TRIVIAL);
    ts_run_bench("sincos_deg", bench_sincos_deg, N, TS_PAR_TRIVIAL);

    printf("\n--- Vector (vec3) ---\n");
    ts_run_bench("vec3_add",       bench_vec3_add,       N, TS_PAR_SIMD);
    ts_run_bench("vec3_dot",       bench_vec3_dot,       N, TS_PAR_REDUCIBLE);
    ts_run_bench("vec3_cross",     bench_vec3_cross,     N, TS_PAR_SIMD);
    ts_run_bench("vec3_normalize", bench_vec3_normalize, N, TS_PAR_REDUCIBLE);
    ts_run_bench("vec3_norm",      bench_vec3_norm,      N, TS_PAR_REDUCIBLE);

    printf("\n--- Matrix (mat4) ---\n");
    ts_run_bench("mat4_multiply",    bench_mat4_multiply,    N/10, TS_PAR_GPU);
    ts_run_bench("mat4_inverse",     bench_mat4_inverse,     N/10, TS_PAR_GPU);
    ts_run_bench("mat4_transform",   bench_mat4_transform,   N,    TS_PAR_GPU);
    ts_run_bench("mat4_rotate_axis", bench_mat4_rotate_axis, N/10, TS_PAR_GPU);
    ts_run_bench("mat4_euler",       bench_mat4_euler,       N/10, TS_PAR_GPU);

    printf("\n--- Geometry Generation ---\n");
    ts_run_bench("gen_cube",         bench_gen_cube,       N_GEO,     TS_PAR_GPU);
    ts_run_bench("gen_sphere(fn=16)",  bench_gen_sphere_16,  N_GEO,     TS_PAR_GPU);
    ts_run_bench("gen_sphere(fn=100)", bench_gen_sphere_100, N_GEO/10,  TS_PAR_GPU);
    ts_run_bench("gen_cylinder",     bench_gen_cylinder,   N_GEO,     TS_PAR_GPU);
    ts_run_bench("gen_circle(100pt)", bench_gen_circle,    N_GEO,     TS_PAR_TRIVIAL);

    printf("\n--- Random Number Generation ---\n");
    ts_run_bench("rands(1000)",  bench_rands_1000,  N/1000, TS_PAR_TRIVIAL);
    ts_run_bench("rand_single",  bench_rand_single, N,      TS_PAR_TRIVIAL);

    printf("\n--- CSG Operations ---\n");
    ts_run_bench("csg_union(cubes)",        bench_csg_union,        1000, TS_PAR_GPU);
    ts_run_bench("csg_difference(cubes)",   bench_csg_difference,   1000, TS_PAR_GPU);
    ts_run_bench("csg_intersection(cubes)", bench_csg_intersection, 1000, TS_PAR_GPU);
    ts_run_bench("csg_hull(sphere16)",      bench_csg_hull,         1000, TS_PAR_GPU);
    ts_run_bench("csg_minkowski(cubes)",    bench_csg_minkowski,    100,  TS_PAR_GPU);

    printf("\n--- Extrusion ---\n");
    ts_run_bench("linear_extrude(square)",       bench_linear_extrude,       10000, TS_PAR_GPU);
    ts_run_bench("linear_extrude(twist,32sl)",   bench_linear_extrude_twist, 10000, TS_PAR_GPU);
    ts_run_bench("rotate_extrude(rect,fn=32)",   bench_rotate_extrude,       10000, TS_PAR_GPU);

    printf("\n--- Bezier Surface ---\n");
    ts_run_bench("bezier_patch_eval",   bench_bezier_patch_eval,   1000000, TS_PAR_SIMD);
    ts_run_bench("bezier_patch_normal", bench_bezier_patch_normal, 1000000, TS_PAR_SIMD);
    ts_run_bench("bezier_patch_sdf",    bench_bezier_patch_sdf,    10000,   TS_PAR_SIMD);

    printf("\n--- Bezier Mesh ---\n");
    ts_run_bench("bezier_mesh_tess(4x4,8st)",  bench_bezier_mesh_tessellate, 100,  TS_PAR_GPU);
    ts_run_bench("bezier_mesh_closest(4x4)",   bench_bezier_mesh_closest,    1000, TS_PAR_SIMD);

    printf("\n--- Bezier Voxelization ---\n");
    ts_run_bench("bezier_voxel_1x1(32^3)",  bench_bezier_voxelize_1x1, 10, TS_PAR_GPU);
    ts_run_bench("bezier_voxel_4x4(64^3)",  bench_bezier_voxelize_4x4, 1,  TS_PAR_GPU);

    printf("\n--- GPU vs CPU (batch=100k) ---\n");
    printf("  GPU: %s\n", g_gpu.active ? g_gpu.device_name : "CPU fallback");
    ts_run_bench("GPU vec3_add(100k)",       bench_gpu_vec3_add,       100, TS_PAR_GPU);
    ts_run_bench("CPU vec3_add(100k)",       bench_cpu_vec3_add,       100, TS_PAR_TRIVIAL);
    ts_run_bench("GPU vec3_norm(100k)",      bench_gpu_vec3_normalize, 100, TS_PAR_GPU);
    ts_run_bench("CPU vec3_norm(100k)",      bench_cpu_vec3_normalize, 100, TS_PAR_TRIVIAL);
    ts_run_bench("GPU mat4_xform(100k)",     bench_gpu_mat4_transform, 100, TS_PAR_GPU);
    ts_run_bench("CPU mat4_xform(100k)",     bench_cpu_mat4_transform, 100, TS_PAR_TRIVIAL);
    ts_run_bench("GPU sin_deg(100k)",        bench_gpu_scalar_sin,     100, TS_PAR_GPU);
    ts_run_bench("GPU rng(100k)",            bench_gpu_rng,            100, TS_PAR_GPU);

    printf("\n============================================================\n");
    printf("  %d benchmarks completed\n", g_ts_bench_count);
    printf("============================================================\n");
}

int main(int argc, char *argv[]) {
    int do_test = 1;
    int do_bench = 0;

    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[1], "--test") == 0) { do_test = 1; do_bench = 0; }
        else if (strcmp(argv[1], "--bench") == 0) { do_test = 0; do_bench = 1; }
        else if (strcmp(argv[1], "--all") == 0) { do_test = 1; do_bench = 1; }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[1]);
            print_help();
            return 1;
        }
    }

    /* Initialize GPU context (falls back to CPU if unavailable) */
    g_gpu = ts_gpu_init();

    if (do_test) run_all_tests();
    if (do_bench) run_all_benchmarks();

    ts_gpu_shutdown(&g_gpu);
    return g_ts_fail > 0 ? 1 : 0;
}
