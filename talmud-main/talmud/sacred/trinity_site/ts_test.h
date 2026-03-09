/*
 * ts_test.h — Trinity Site test & benchmark framework
 *
 * Lightweight test runner with:
 *   - Pass/fail tracking with source location
 *   - Float comparison with epsilon
 *   - Vector/matrix comparison
 *   - Benchmark timing (clock_gettime MONOTONIC)
 *   - Parallelism classification for each function group
 *
 * All functions static — include directly in trinity_site.c.
 */
#ifndef TS_TEST_H
#define TS_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* --- Parallelism classification --- */

typedef enum {
    TS_PAR_TRIVIAL,     /* Each element independent, embarrassingly parallel */
    TS_PAR_SIMD,        /* Benefits from SIMD (vec3/vec4 component ops) */
    TS_PAR_GPU,         /* Benefits from GPU offload (matrix, mesh gen) */
    TS_PAR_REDUCIBLE,   /* Parallel with reduction (min, max, sum, norm) */
    TS_PAR_SEQUENTIAL,  /* Inherently sequential (document why) */
} ts_parallelism_t;

static const char *ts_par_name(ts_parallelism_t p) {
    switch (p) {
    case TS_PAR_TRIVIAL:    return "TRIVIAL (embarrassingly parallel)";
    case TS_PAR_SIMD:       return "SIMD (vector lanes)";
    case TS_PAR_GPU:        return "GPU (matrix/mesh offload)";
    case TS_PAR_REDUCIBLE:  return "REDUCIBLE (parallel with reduction)";
    case TS_PAR_SEQUENTIAL: return "SEQUENTIAL (inherently serial)";
    }
    return "UNKNOWN";
}

/* --- Test state --- */

static int g_ts_pass = 0;
static int g_ts_fail = 0;
static int g_ts_bench_count = 0;
static const char *g_ts_current_test = NULL;

/* --- Assertion macros --- */

#define TS_ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL [%s:%d] %s: expected true: %s\n", \
                __FILE__, __LINE__, g_ts_current_test, #expr); \
        g_ts_fail++; return; \
    } \
} while (0)

#define TS_ASSERT_FALSE(expr) do { \
    if (expr) { \
        fprintf(stderr, "  FAIL [%s:%d] %s: expected false: %s\n", \
                __FILE__, __LINE__, g_ts_current_test, #expr); \
        g_ts_fail++; return; \
    } \
} while (0)

#define TS_ASSERT_NEAR(actual, expected, eps) do { \
    double _a = (actual), _e = (expected), _eps = (eps); \
    if (fabs(_a - _e) > _eps) { \
        fprintf(stderr, "  FAIL [%s:%d] %s: %.15g != %.15g (eps=%.15g)\n", \
                __FILE__, __LINE__, g_ts_current_test, _a, _e, _eps); \
        g_ts_fail++; return; \
    } \
} while (0)

#define TS_ASSERT_EQ_INT(actual, expected) do { \
    int _a = (actual), _e = (expected); \
    if (_a != _e) { \
        fprintf(stderr, "  FAIL [%s:%d] %s: %d != %d\n", \
                __FILE__, __LINE__, g_ts_current_test, _a, _e); \
        g_ts_fail++; return; \
    } \
} while (0)

#define TS_ASSERT_VEC3_NEAR(a, b, eps) do { \
    for (int _i = 0; _i < 3; _i++) { \
        if (fabs((a).v[_i] - (b).v[_i]) > (eps)) { \
            fprintf(stderr, "  FAIL [%s:%d] %s: vec3[%d] %.15g != %.15g\n", \
                    __FILE__, __LINE__, g_ts_current_test, \
                    _i, (a).v[_i], (b).v[_i]); \
            g_ts_fail++; return; \
        } \
    } \
} while (0)

#define TS_ASSERT_MAT4_NEAR(a, b, eps) do { \
    for (int _i = 0; _i < 16; _i++) { \
        if (fabs((a).m[_i] - (b).m[_i]) > (eps)) { \
            fprintf(stderr, "  FAIL [%s:%d] %s: mat4[%d] %.15g != %.15g\n", \
                    __FILE__, __LINE__, g_ts_current_test, \
                    _i, (a).m[_i], (b).m[_i]); \
            g_ts_fail++; return; \
        } \
    } \
} while (0)

/* Mark a test as passing (call at end of test function if no asserts fired) */
#define TS_PASS() do { g_ts_pass++; } while (0)

/* --- Test runner --- */

typedef void (*ts_test_fn)(void);

static void ts_run_test(const char *name, ts_test_fn fn) {
    int before = g_ts_fail;
    g_ts_current_test = name;
    fn();
    if (g_ts_fail == before) {
        printf("  PASS  %s\n", name);
    }
}

static void ts_section(const char *name, ts_parallelism_t par) {
    printf("\n=== %s ===\n", name);
    printf("  Parallelism: %s\n\n", ts_par_name(par));
}

/* --- Benchmark --- */

static double ts_clock_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/*
 * Benchmark a function. The function receives an iteration count
 * and should loop internally. We measure total time and report
 * per-operation nanoseconds.
 */
typedef void (*ts_bench_fn)(int n);

static void ts_run_bench(const char *name, ts_bench_fn fn, int iterations,
                         ts_parallelism_t par) {
    /* Warmup */
    fn(iterations / 10 > 0 ? iterations / 10 : 1);

    double t0 = ts_clock_ns();
    fn(iterations);
    double t1 = ts_clock_ns();

    double total_ms = (t1 - t0) / 1e6;
    double per_op_ns = (t1 - t0) / (double)iterations;

    printf("  %-30s %10d ops  %8.2f ms  %8.1f ns/op  [%s]\n",
           name, iterations, total_ms, per_op_ns,
           par == TS_PAR_TRIVIAL    ? "TRIV" :
           par == TS_PAR_SIMD       ? "SIMD" :
           par == TS_PAR_GPU        ? "GPU " :
           par == TS_PAR_REDUCIBLE  ? "REDC" :
                                      "SEQ ");
    g_ts_bench_count++;
}

/* --- Summary --- */

static void ts_summary(void) {
    printf("\n========================================\n");
    printf("TRINITY SITE RESULTS\n");
    printf("========================================\n");
    printf("  Tests:      %d passed, %d failed, %d total\n",
           g_ts_pass, g_ts_fail, g_ts_pass + g_ts_fail);
    if (g_ts_bench_count > 0)
        printf("  Benchmarks: %d completed\n", g_ts_bench_count);
    printf("========================================\n");

    if (g_ts_fail > 0) {
        printf("\n  *** %d TEST(S) FAILED ***\n", g_ts_fail);
    } else {
        printf("\n  ALL TESTS PASSED\n");
    }
}

#endif /* TS_TEST_H */
