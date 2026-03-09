/*
 * ts_random.h — Parallel-friendly random number generation
 *
 * OpenSCAD: rands(min, max, count, seed)
 *
 * Standard PRNGs are sequential (each output depends on previous state).
 * For GPU parallelization, we use a COUNTER-BASED RNG:
 *   - Philox or similar: output = hash(seed, counter)
 *   - Each element computed independently — embarrassingly parallel
 *   - Deterministic: same (seed, counter) = same output, always
 *   - No shared state between threads
 *
 * We implement a simplified Philox-2x32-10 variant using 64-bit
 * integer mixing. This gives excellent statistical properties
 * with zero inter-thread dependencies.
 *
 * GPU: each work item computes hash(seed, global_id) — zero coordination.
 */
#ifndef TS_RANDOM_H
#define TS_RANDOM_H

#include <stdint.h>

/*
 * SplitMix64-based hash function.
 * Takes a 64-bit state, returns a uniform 64-bit value.
 * Each call is independent — no sequential dependency.
 */
static inline uint64_t ts_hash64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

/*
 * Convert hash to uniform double in [0, 1).
 * Uses 53 bits of mantissa for full double precision.
 */
static inline double ts_hash_to_double(uint64_t h) {
    return (double)(h >> 11) * (1.0 / 9007199254740992.0);  /* 1/2^53 */
}

/*
 * Generate a single random double in [min, max].
 * seed + index uniquely determines the output.
 * GPU: each work item calls this with its own index.
 */
static inline double ts_rand(uint64_t seed, uint64_t index,
                             double min_val, double max_val) {
    uint64_t h = ts_hash64(seed ^ (index * 0x9E3779B97F4A7C15ULL));
    double t = ts_hash_to_double(h);
    return min_val + t * (max_val - min_val);
}

/*
 * Fill an array with random doubles in [min, max].
 * OpenSCAD: rands(min, max, count, seed)
 *
 * GPU: each element is independent — launch count work items.
 * CPU: simple loop, still benefits from auto-vectorization.
 */
static inline void ts_rands(double min_val, double max_val,
                            int count, uint64_t seed,
                            double *out) {
    for (int i = 0; i < count; i++) {
        out[i] = ts_rand(seed, (uint64_t)i, min_val, max_val);
    }
}

/*
 * Generate a random integer in [min, max] inclusive.
 */
static inline int ts_rand_int(uint64_t seed, uint64_t index,
                              int min_val, int max_val) {
    uint64_t h = ts_hash64(seed ^ (index * 0x9E3779B97F4A7C15ULL));
    int range = max_val - min_val + 1;
    return min_val + (int)(h % (uint64_t)range);
}

#endif /* TS_RANDOM_H */
