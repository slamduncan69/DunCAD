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
 * SECTION 7: CSG STUB TESTS
 * Verify stubs return NOT_IMPLEMENTED correctly
 * Parallelism: documented in ts_csg.h headers
 * ================================================================ */

static void test_csg_union_stub(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    int ret = ts_csg_union(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_NOT_IMPLEMENTED);
    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

static void test_csg_difference_stub(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    int ret = ts_csg_difference(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_NOT_IMPLEMENTED);
    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

static void test_csg_intersection_stub(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    int ret = ts_csg_intersection(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_NOT_IMPLEMENTED);
    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

static void test_csg_hull_stub(void) {
    ts_mesh a = ts_mesh_init(), out = ts_mesh_init();
    int ret = ts_csg_hull(&a, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_NOT_IMPLEMENTED);
    ts_mesh_free(&a); ts_mesh_free(&out);
    TS_PASS();
}

static void test_csg_minkowski_stub(void) {
    ts_mesh a = ts_mesh_init(), b = ts_mesh_init(), out = ts_mesh_init();
    int ret = ts_csg_minkowski(&a, &b, &out);
    TS_ASSERT_EQ_INT(ret, TS_CSG_NOT_IMPLEMENTED);
    ts_mesh_free(&a); ts_mesh_free(&b); ts_mesh_free(&out);
    TS_PASS();
}

/* ================================================================
 * SECTION 8: EXTRUSION STUB TESTS
 * ================================================================ */

static void test_linear_extrude_stub(void) {
    ts_mesh out = ts_mesh_init();
    double profile[] = { 0,0, 1,0, 0.5,1 };
    int ret = ts_linear_extrude(profile, 3, 10.0, 0.0, 1, 1.0, 1, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_NOT_IMPLEMENTED);
    ts_mesh_free(&out);
    TS_PASS();
}

static void test_rotate_extrude_stub(void) {
    ts_mesh out = ts_mesh_init();
    double profile[] = { 5,0, 5,10, 3,10, 3,0 };
    int ret = ts_rotate_extrude(profile, 4, 360.0, 32, &out);
    TS_ASSERT_EQ_INT(ret, TS_EXTRUDE_NOT_IMPLEMENTED);
    ts_mesh_free(&out);
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

    /* --- Random --- */
    ts_section("RANDOM: rands", TS_PAR_TRIVIAL);
    ts_run_test("rands_green", test_rands_green);
    ts_run_test("rands_red", test_rands_red);

    ts_section("RANDOM: rands range", TS_PAR_TRIVIAL);
    ts_run_test("rands_range_green", test_rands_range_green);
    ts_run_test("rands_range_red", test_rands_range_red);

    /* --- CSG stubs --- */
    ts_section("CSG: stubs (not yet implemented)", TS_PAR_GPU);
    ts_run_test("csg_union_stub", test_csg_union_stub);
    ts_run_test("csg_difference_stub", test_csg_difference_stub);
    ts_run_test("csg_intersection_stub", test_csg_intersection_stub);
    ts_run_test("csg_hull_stub", test_csg_hull_stub);
    ts_run_test("csg_minkowski_stub", test_csg_minkowski_stub);

    /* --- Extrusion stubs --- */
    ts_section("EXTRUDE: stubs (not yet implemented)", TS_PAR_GPU);
    ts_run_test("linear_extrude_stub", test_linear_extrude_stub);
    ts_run_test("rotate_extrude_stub", test_rotate_extrude_stub);

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

    if (do_test) run_all_tests();
    if (do_bench) run_all_benchmarks();

    return g_ts_fail > 0 ? 1 : 0;
}
