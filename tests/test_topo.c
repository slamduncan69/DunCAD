/*
 * test_topo.c — Tests and benchmarks for dc_topo.h mesh topology analysis.
 *
 * Trinity Site style: green/red TDD pairs + benchmarks.
 * Pure C — no GL, no GTK. Tests against synthetic mesh data.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "gl/dc_topo.h"

/* =========================================================================
 * Test framework (minimal, matches Trinity Site pattern)
 * ========================================================================= */

static int g_pass = 0;
static int g_fail = 0;
static int g_bench_count = 0;
static const char *g_current = NULL;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL [%s:%d] %s: %s\n", \
                __FILE__, __LINE__, g_current, #expr); \
        g_fail++; return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    float _a = (a), _b = (b); \
    if (fabsf(_a - _b) > (eps)) { \
        fprintf(stderr, "  FAIL [%s:%d] %s: %.6f != %.6f (eps=%.6f)\n", \
                __FILE__, __LINE__, g_current, _a, _b, (float)(eps)); \
        g_fail++; return; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    g_current = #fn; \
    int _prev_fail = g_fail; \
    fn(); \
    if (g_fail == _prev_fail) { g_pass++; printf("  PASS %s\n", #fn); } \
    else { printf("  FAIL %s\n", #fn); } \
} while (0)

/* Benchmark helper */
static double
bench_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

#define BENCH_START() double _t0 = bench_time_ns()
#define BENCH_END(label, iters) do { \
    double _dt = bench_time_ns() - _t0; \
    printf("  BENCH %s: %.1f us (%.0f ns/iter, %d iters)\n", \
           label, _dt / 1000.0, _dt / (iters), (int)(iters)); \
    g_bench_count++; \
} while (0)

/* =========================================================================
 * Mesh builders — synthetic test data
 * ========================================================================= */

/* Write one triangle to interleaved data at offset.
 * data must have room for 18 floats starting at data[offset*18]. */
static void
set_tri(float *data, int tri_idx,
        float nx, float ny, float nz,
        float v0x, float v0y, float v0z,
        float v1x, float v1y, float v1z,
        float v2x, float v2y, float v2z)
{
    float *p = data + tri_idx * 18;
    /* Vertex 0 */
    p[0] = nx; p[1] = ny; p[2] = nz;
    p[3] = v0x; p[4] = v0y; p[5] = v0z;
    /* Vertex 1 */
    p[6] = nx; p[7] = ny; p[8] = nz;
    p[9] = v1x; p[10] = v1y; p[11] = v1z;
    /* Vertex 2 */
    p[12] = nx; p[13] = ny; p[14] = nz;
    p[15] = v2x; p[16] = v2y; p[17] = v2z;
}

/* Build a unit cube (axis-aligned, 12 triangles, 6 faces).
 * Each face has 2 triangles with the same normal.
 * Returns malloc'd data. Caller frees. */
static float *
build_cube(void)
{
    /* 12 triangles * 18 floats = 216 floats */
    float *data = (float *)calloc(216, sizeof(float));

    /* +Z face (top): normal (0,0,1), z=1 */
    set_tri(data, 0, 0,0,1,  0,0,1, 1,0,1, 1,1,1);
    set_tri(data, 1, 0,0,1,  0,0,1, 1,1,1, 0,1,1);

    /* -Z face (bottom): normal (0,0,-1), z=0 */
    set_tri(data, 2, 0,0,-1,  0,0,0, 0,1,0, 1,1,0);
    set_tri(data, 3, 0,0,-1,  0,0,0, 1,1,0, 1,0,0);

    /* +X face: normal (1,0,0), x=1 */
    set_tri(data, 4, 1,0,0,  1,0,0, 1,1,0, 1,1,1);
    set_tri(data, 5, 1,0,0,  1,0,0, 1,1,1, 1,0,1);

    /* -X face: normal (-1,0,0), x=0 */
    set_tri(data, 6, -1,0,0,  0,0,0, 0,0,1, 0,1,1);
    set_tri(data, 7, -1,0,0,  0,0,0, 0,1,1, 0,1,0);

    /* +Y face: normal (0,1,0), y=1 */
    set_tri(data, 8, 0,1,0,  0,1,0, 1,1,0, 1,1,1);
    set_tri(data, 9, 0,1,0,  0,1,0, 1,1,1, 0,1,1);

    /* -Y face: normal (0,-1,0), y=0 */
    set_tri(data, 10, 0,-1,0,  0,0,0, 1,0,0, 1,0,1);
    set_tri(data, 11, 0,-1,0,  0,0,0, 1,0,1, 0,0,1);

    return data;
}

/* Build a single triangle (1 face, 3 boundary edges). */
static float *
build_single_tri(void)
{
    float *data = (float *)calloc(18, sizeof(float));
    set_tri(data, 0, 0,0,1,  0,0,0, 1,0,0, 0.5f,1,0);
    return data;
}

/* Build two triangles sharing one edge, same normal (1 face). */
static float *
build_quad(void)
{
    float *data = (float *)calloc(36, sizeof(float));
    set_tri(data, 0, 0,0,1,  0,0,0, 1,0,0, 1,1,0);
    set_tri(data, 1, 0,0,1,  0,0,0, 1,1,0, 0,1,0);
    return data;
}

/* Build two triangles sharing one edge, different normals (2 faces). */
static float *
build_dihedral(void)
{
    float *data = (float *)calloc(36, sizeof(float));
    /* Triangle 0: normal (0,0,1) — flat on XY plane */
    set_tri(data, 0, 0,0,1,  0,0,0, 1,0,0, 0.5f,1,0);
    /* Triangle 1: normal (0,-0.7071,0.7071) — angled 45° */
    set_tri(data, 1, 0,-0.7071f,0.7071f,  0,0,0, 1,0,0, 0.5f,0,-1);
    return data;
}

/* Build a sphere-like mesh with N subdivisions.
 * Returns data + num_triangles via pointer. */
static float *
build_sphere(int subdivisions, int *out_ntri)
{
    /* UV sphere: lat x lon grid */
    int nlat = subdivisions;
    int nlon = subdivisions * 2;
    int ntri = 2 * nlat * nlon; /* quads = 2 tris each, caps included */
    float *data = (float *)calloc((size_t)ntri * 18, sizeof(float));
    *out_ntri = 0;

    int t = 0;
    for (int lat = 0; lat < nlat; lat++) {
        float theta0 = (float)M_PI * lat / nlat;
        float theta1 = (float)M_PI * (lat + 1) / nlat;
        for (int lon = 0; lon < nlon; lon++) {
            float phi0 = 2.0f * (float)M_PI * lon / nlon;
            float phi1 = 2.0f * (float)M_PI * (lon + 1) / nlon;

            /* 4 vertices of this quad */
            float v00[3] = {sinf(theta0)*cosf(phi0), sinf(theta0)*sinf(phi0), cosf(theta0)};
            float v10[3] = {sinf(theta1)*cosf(phi0), sinf(theta1)*sinf(phi0), cosf(theta1)};
            float v01[3] = {sinf(theta0)*cosf(phi1), sinf(theta0)*sinf(phi1), cosf(theta0)};
            float v11[3] = {sinf(theta1)*cosf(phi1), sinf(theta1)*sinf(phi1), cosf(theta1)};

            /* Normal = vertex position (unit sphere) */
            /* Triangle 1: v00, v10, v11 */
            float n1[3] = {(v00[0]+v10[0]+v11[0])/3,
                           (v00[1]+v10[1]+v11[1])/3,
                           (v00[2]+v10[2]+v11[2])/3};
            float n1len = sqrtf(n1[0]*n1[0]+n1[1]*n1[1]+n1[2]*n1[2]);
            if (n1len > 1e-8f) { n1[0]/=n1len; n1[1]/=n1len; n1[2]/=n1len; }

            set_tri(data, t, n1[0],n1[1],n1[2],
                    v00[0],v00[1],v00[2],
                    v10[0],v10[1],v10[2],
                    v11[0],v11[1],v11[2]);
            t++;

            /* Triangle 2: v00, v11, v01 */
            float n2[3] = {(v00[0]+v11[0]+v01[0])/3,
                           (v00[1]+v11[1]+v01[1])/3,
                           (v00[2]+v11[2]+v01[2])/3};
            float n2len = sqrtf(n2[0]*n2[0]+n2[1]*n2[1]+n2[2]*n2[2]);
            if (n2len > 1e-8f) { n2[0]/=n2len; n2[1]/=n2len; n2[2]/=n2len; }

            set_tri(data, t, n2[0],n2[1],n2[2],
                    v00[0],v00[1],v00[2],
                    v11[0],v11[1],v11[2],
                    v01[0],v01[1],v01[2]);
            t++;
        }
    }
    *out_ntri = t;
    return data;
}

/* Build a large grid mesh for stress testing.
 * NxN grid = 2*N*N triangles, all same normal (one huge face). */
static float *
build_flat_grid(int n, int *out_ntri)
{
    int ntri = 2 * n * n;
    float *data = (float *)calloc((size_t)ntri * 18, sizeof(float));
    int t = 0;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            float fx = (float)x, fy = (float)y;
            set_tri(data, t, 0,0,1,
                    fx, fy, 0,
                    fx+1, fy, 0,
                    fx+1, fy+1, 0);
            t++;
            set_tri(data, t, 0,0,1,
                    fx, fy, 0,
                    fx+1, fy+1, 0,
                    fx, fy+1, 0);
            t++;
        }
    }
    *out_ntri = ntri;
    return data;
}

/* =========================================================================
 * GREEN tests — must pass
 * ========================================================================= */

static void test_null_input(void)
{
    DC_Topo *t = dc_topo_build(NULL, 0);
    ASSERT_TRUE(t == NULL);
    t = dc_topo_build(NULL, 10);
    ASSERT_TRUE(t == NULL);
}

static void test_single_triangle(void)
{
    float *data = build_single_tri();
    DC_Topo *topo = dc_topo_build(data, 1);
    ASSERT_TRUE(topo != NULL);
    ASSERT_EQ(topo->num_triangles, 1);
    ASSERT_EQ(topo->face_count, 1);
    ASSERT_EQ(topo->face_groups[0].tri_count, 1);
    ASSERT_EQ(topo->face_groups[0].tri_indices[0], 0);
    /* 3 boundary edges (all on one triangle) */
    ASSERT_EQ(topo->edge_count, 3);
    /* All edges should have face_b == -1 (boundary) */
    for (int i = 0; i < topo->edge_count; i++) {
        ASSERT_TRUE(topo->edges[i].face_b == -1);
    }
    ASSERT_EQ(topo->tri_to_face[0], 0);
    dc_topo_free(topo);
    free(data);
}

static void test_quad_same_normal(void)
{
    float *data = build_quad();
    DC_Topo *topo = dc_topo_build(data, 2);
    ASSERT_TRUE(topo != NULL);
    ASSERT_EQ(topo->num_triangles, 2);
    /* Both triangles share same normal + share an edge → 1 face group */
    ASSERT_EQ(topo->face_count, 1);
    ASSERT_EQ(topo->face_groups[0].tri_count, 2);
    /* 4 boundary edges (outer perimeter of quad) */
    ASSERT_EQ(topo->edge_count, 4);
    /* tri_to_face: both map to same group */
    ASSERT_EQ(topo->tri_to_face[0], topo->tri_to_face[1]);
    dc_topo_free(topo);
    free(data);
}

static void test_dihedral_different_normals(void)
{
    float *data = build_dihedral();
    DC_Topo *topo = dc_topo_build(data, 2);
    ASSERT_TRUE(topo != NULL);
    ASSERT_EQ(topo->num_triangles, 2);
    /* Different normals → 2 face groups */
    ASSERT_EQ(topo->face_count, 2);
    ASSERT_EQ(topo->face_groups[0].tri_count, 1);
    ASSERT_EQ(topo->face_groups[1].tri_count, 1);
    /* Shared edge + 4 boundary edges = 5 total edges */
    ASSERT_EQ(topo->edge_count, 5);
    /* The shared edge should have both face_a and face_b set */
    int shared_count = 0;
    for (int i = 0; i < topo->edge_count; i++) {
        if (topo->edges[i].face_a >= 0 && topo->edges[i].face_b >= 0)
            shared_count++;
    }
    ASSERT_EQ(shared_count, 1);
    dc_topo_free(topo);
    free(data);
}

static void test_cube_6_faces(void)
{
    float *data = build_cube();
    DC_Topo *topo = dc_topo_build(data, 12);
    ASSERT_TRUE(topo != NULL);
    ASSERT_EQ(topo->num_triangles, 12);
    /* 6 axis-aligned faces, 2 triangles each → 6 face groups */
    ASSERT_EQ(topo->face_count, 6);

    /* Each face group should have exactly 2 triangles */
    for (int i = 0; i < topo->face_count; i++) {
        ASSERT_EQ(topo->face_groups[i].tri_count, 2);
    }

    /* Cube has 12 edges (face boundaries).
     * Each face has 4 boundary edges shared with neighbors.
     * 6 faces * 4 edges / 2 (each edge shared) = 12 edges. */
    ASSERT_EQ(topo->edge_count, 12);

    /* All edges should be shared (face_b >= 0) — closed mesh */
    for (int i = 0; i < topo->edge_count; i++) {
        ASSERT_TRUE(topo->edges[i].face_a >= 0);
        ASSERT_TRUE(topo->edges[i].face_b >= 0);
        ASSERT_TRUE(topo->edges[i].face_a != topo->edges[i].face_b);
    }

    /* Verify tri_to_face covers all triangles */
    for (int t = 0; t < 12; t++) {
        ASSERT_TRUE(topo->tri_to_face[t] >= 0);
        ASSERT_TRUE(topo->tri_to_face[t] < 6);
    }

    /* Verify paired triangles in same group (tris 0,1 = face 0, etc.) */
    ASSERT_EQ(topo->tri_to_face[0], topo->tri_to_face[1]);
    ASSERT_EQ(topo->tri_to_face[2], topo->tri_to_face[3]);
    ASSERT_EQ(topo->tri_to_face[4], topo->tri_to_face[5]);
    ASSERT_EQ(topo->tri_to_face[6], topo->tri_to_face[7]);
    ASSERT_EQ(topo->tri_to_face[8], topo->tri_to_face[9]);
    ASSERT_EQ(topo->tri_to_face[10], topo->tri_to_face[11]);

    /* All 6 face groups should be distinct */
    int seen[6] = {0};
    for (int t = 0; t < 12; t += 2) {
        int g = topo->tri_to_face[t];
        ASSERT_TRUE(!seen[g]);
        seen[g] = 1;
    }

    dc_topo_free(topo);
    free(data);
}

static void test_cube_face_normals(void)
{
    float *data = build_cube();
    DC_Topo *topo = dc_topo_build(data, 12);
    ASSERT_TRUE(topo != NULL);

    /* Verify each face group has a unit normal */
    for (int i = 0; i < topo->face_count; i++) {
        float *n = topo->face_groups[i].normal;
        float len = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        ASSERT_FLOAT_EQ(len, 1.0f, 0.01f);
    }

    /* Verify all 6 axis normals are represented */
    float expected[6][3] = {
        {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}
    };
    for (int e = 0; e < 6; e++) {
        int found = 0;
        for (int g = 0; g < topo->face_count; g++) {
            float dot = expected[e][0]*topo->face_groups[g].normal[0] +
                        expected[e][1]*topo->face_groups[g].normal[1] +
                        expected[e][2]*topo->face_groups[g].normal[2];
            if (dot > 0.99f) { found = 1; break; }
        }
        ASSERT_TRUE(found);
    }

    dc_topo_free(topo);
    free(data);
}

static void test_flat_grid_one_face(void)
{
    int ntri;
    float *data = build_flat_grid(10, &ntri);
    ASSERT_EQ(ntri, 200);

    DC_Topo *topo = dc_topo_build(data, ntri);
    ASSERT_TRUE(topo != NULL);
    /* All triangles same normal, all connected → 1 face group */
    ASSERT_EQ(topo->face_count, 1);
    ASSERT_EQ(topo->face_groups[0].tri_count, ntri);

    /* Boundary edges = perimeter of 10x10 grid = 4*10 = 40 edges */
    /* Interior edges where different face groups meet = 0 (all same face) */
    /* Only boundary edges (face_b == -1) exist */
    ASSERT_EQ(topo->edge_count, 40);

    dc_topo_free(topo);
    free(data);
}

static void test_sphere_many_faces(void)
{
    int ntri;
    float *data = build_sphere(8, &ntri);
    ASSERT_TRUE(ntri > 0);

    DC_Topo *topo = dc_topo_build(data, ntri);
    ASSERT_TRUE(topo != NULL);

    /* UV sphere with varying normals — each triangle should be its own face
     * (unless two adjacent tris happen to share a normal, which is rare). */
    /* For a UV sphere, adjacent quads have different normals, so
     * face_count should equal num_triangles (each tri is its own face). */
    ASSERT_TRUE(topo->face_count >= ntri / 2); /* at minimum */
    ASSERT_TRUE(topo->face_count <= ntri);     /* at maximum */

    /* Verify total triangles across all groups equals num_triangles */
    int total = 0;
    for (int i = 0; i < topo->face_count; i++)
        total += topo->face_groups[i].tri_count;
    ASSERT_EQ(total, ntri);

    /* Edges should exist (sphere has plenty of face boundaries) */
    ASSERT_TRUE(topo->edge_count > 0);

    dc_topo_free(topo);
    free(data);
}

static void test_tri_to_face_consistency(void)
{
    float *data = build_cube();
    DC_Topo *topo = dc_topo_build(data, 12);
    ASSERT_TRUE(topo != NULL);

    /* Every triangle referenced in tri_to_face must appear in corresponding
     * face group's tri_indices */
    for (int t = 0; t < 12; t++) {
        int g = topo->tri_to_face[t];
        DC_FaceGroup *fg = &topo->face_groups[g];
        int found = 0;
        for (int i = 0; i < fg->tri_count; i++) {
            if (fg->tri_indices[i] == t) { found = 1; break; }
        }
        ASSERT_TRUE(found);
    }

    dc_topo_free(topo);
    free(data);
}

static void test_edge_face_refs_valid(void)
{
    float *data = build_cube();
    DC_Topo *topo = dc_topo_build(data, 12);
    ASSERT_TRUE(topo != NULL);

    for (int i = 0; i < topo->edge_count; i++) {
        ASSERT_TRUE(topo->edges[i].face_a >= 0);
        ASSERT_TRUE(topo->edges[i].face_a < topo->face_count);
        if (topo->edges[i].face_b >= 0) {
            ASSERT_TRUE(topo->edges[i].face_b < topo->face_count);
        }
    }

    dc_topo_free(topo);
    free(data);
}

static void test_free_null(void)
{
    dc_topo_free(NULL); /* must not crash */
    ASSERT_TRUE(1);
}

/* =========================================================================
 * RED tests — edge cases and degenerate inputs
 * ========================================================================= */

static void test_zero_triangles(void)
{
    float dummy[18] = {0};
    DC_Topo *t = dc_topo_build(dummy, 0);
    ASSERT_TRUE(t == NULL);
}

static void test_negative_triangles(void)
{
    float dummy[18] = {0};
    DC_Topo *t = dc_topo_build(dummy, -5);
    ASSERT_TRUE(t == NULL);
}

static void test_degenerate_zero_area_tri(void)
{
    /* Triangle with all vertices at same point (zero area). */
    float *data = (float *)calloc(18, sizeof(float));
    /* Normal = (0,0,0), all verts = (1,1,1) */
    for (int v = 0; v < 3; v++) {
        data[v*6+3] = 1.0f; data[v*6+4] = 1.0f; data[v*6+5] = 1.0f;
    }
    DC_Topo *topo = dc_topo_build(data, 1);
    ASSERT_TRUE(topo != NULL);
    /* Still produces 1 face group (degenerate but valid) */
    ASSERT_EQ(topo->face_count, 1);
    ASSERT_EQ(topo->edge_count, 0); /* all edges collapsed to same point */
    dc_topo_free(topo);
    free(data);
}

static void test_disconnected_triangles_same_normal(void)
{
    /* Two triangles with same normal but NOT sharing any edge
     * → should be 2 separate face groups (not merged). */
    float *data = (float *)calloc(36, sizeof(float));
    set_tri(data, 0, 0,0,1,  0,0,0, 1,0,0, 0.5f,1,0);
    set_tri(data, 1, 0,0,1,  5,5,0, 6,5,0, 5.5f,6,0);
    DC_Topo *topo = dc_topo_build(data, 2);
    ASSERT_TRUE(topo != NULL);
    /* Same normal but no shared edge → 2 face groups */
    ASSERT_EQ(topo->face_count, 2);
    ASSERT_EQ(topo->face_groups[0].tri_count, 1);
    ASSERT_EQ(topo->face_groups[1].tri_count, 1);
    dc_topo_free(topo);
    free(data);
}

static void test_nearly_matching_normals(void)
{
    /* Two adjacent triangles with normals that differ by just under epsilon
     * → should merge. */
    float *data = (float *)calloc(36, sizeof(float));
    float n1[3] = {0, 0, 1.0f};
    /* Perturb normal slightly — within DC_TOPO_NORMAL_EPS */
    float n2[3] = {0.005f, 0, 0.999987f}; /* dot ≈ 0.999987, well above threshold */
    set_tri(data, 0, n1[0],n1[1],n1[2],  0,0,0, 1,0,0, 1,1,0);
    set_tri(data, 1, n2[0],n2[1],n2[2],  0,0,0, 1,1,0, 0,1,0);
    DC_Topo *topo = dc_topo_build(data, 2);
    ASSERT_TRUE(topo != NULL);
    ASSERT_EQ(topo->face_count, 1); /* merged */
    dc_topo_free(topo);
    free(data);
}

static void test_clearly_different_normals(void)
{
    /* Two adjacent triangles with normals that differ significantly
     * → should NOT merge. */
    float *data = (float *)calloc(36, sizeof(float));
    set_tri(data, 0, 0,0,1,     0,0,0, 1,0,0, 1,1,0);
    set_tri(data, 1, 0,1,0,     0,0,0, 1,1,0, 0,1,0);
    DC_Topo *topo = dc_topo_build(data, 2);
    ASSERT_TRUE(topo != NULL);
    ASSERT_EQ(topo->face_count, 2); /* not merged */
    dc_topo_free(topo);
    free(data);
}

/* =========================================================================
 * STRESS tests — invariant checks on complex meshes
 * ========================================================================= */

/* Global invariant: sum of all face group tri_counts == num_triangles */
static void test_invariant_tri_count_sum(void)
{
    int ntri;
    float *data = build_sphere(16, &ntri);
    DC_Topo *topo = dc_topo_build(data, ntri);
    ASSERT_TRUE(topo != NULL);

    int sum = 0;
    for (int i = 0; i < topo->face_count; i++)
        sum += topo->face_groups[i].tri_count;
    ASSERT_EQ(sum, ntri);

    dc_topo_free(topo);
    free(data);
}

/* Every triangle appears in exactly one face group */
static void test_invariant_tri_uniqueness(void)
{
    float *data = build_cube();
    DC_Topo *topo = dc_topo_build(data, 12);
    ASSERT_TRUE(topo != NULL);

    int seen[12] = {0};
    for (int g = 0; g < topo->face_count; g++) {
        for (int i = 0; i < topo->face_groups[g].tri_count; i++) {
            int t = topo->face_groups[g].tri_indices[i];
            ASSERT_TRUE(t >= 0 && t < 12);
            ASSERT_EQ(seen[t], 0); /* not seen before */
            seen[t] = 1;
        }
    }
    /* All triangles accounted for */
    for (int t = 0; t < 12; t++)
        ASSERT_EQ(seen[t], 1);

    dc_topo_free(topo);
    free(data);
}

/* tri_to_face[t] and face_groups[g].tri_indices are consistent */
static void test_invariant_bidirectional_map(void)
{
    int ntri;
    float *data = build_flat_grid(5, &ntri); /* 50 tri */
    DC_Topo *topo = dc_topo_build(data, ntri);
    ASSERT_TRUE(topo != NULL);

    /* Forward: tri_to_face[t] = g implies t is in face_groups[g].tri_indices */
    for (int t = 0; t < ntri; t++) {
        int g = topo->tri_to_face[t];
        ASSERT_TRUE(g >= 0 && g < topo->face_count);
        DC_FaceGroup *fg = &topo->face_groups[g];
        int found = 0;
        for (int i = 0; i < fg->tri_count; i++)
            if (fg->tri_indices[i] == t) { found = 1; break; }
        ASSERT_TRUE(found);
    }

    /* Reverse: face_groups[g].tri_indices[i] = t implies tri_to_face[t] = g */
    for (int g = 0; g < topo->face_count; g++) {
        for (int i = 0; i < topo->face_groups[g].tri_count; i++) {
            int t = topo->face_groups[g].tri_indices[i];
            ASSERT_EQ(topo->tri_to_face[t], g);
        }
    }

    dc_topo_free(topo);
    free(data);
}

/* All triangles in a face group have same normal (within epsilon) */
static void test_invariant_face_normal_consistency(void)
{
    int ntri;
    float *data = build_sphere(16, &ntri);
    DC_Topo *topo = dc_topo_build(data, ntri);
    ASSERT_TRUE(topo != NULL);

    for (int g = 0; g < topo->face_count; g++) {
        float *ref = topo->face_groups[g].normal;
        for (int i = 0; i < topo->face_groups[g].tri_count; i++) {
            int t = topo->face_groups[g].tri_indices[i];
            float n[3];
            dc_topo_tri_normal(data, t, n);
            float dot = ref[0]*n[0] + ref[1]*n[1] + ref[2]*n[2];
            ASSERT_TRUE(dot > (1.0f - DC_TOPO_NORMAL_EPS));
        }
    }

    dc_topo_free(topo);
    free(data);
}

/* Edges only exist between different face groups */
static void test_invariant_edges_cross_faces(void)
{
    int ntri;
    float *data = build_sphere(8, &ntri);
    DC_Topo *topo = dc_topo_build(data, ntri);
    ASSERT_TRUE(topo != NULL);

    for (int i = 0; i < topo->edge_count; i++) {
        /* face_a != face_b (or face_b == -1 for boundary) */
        ASSERT_TRUE(topo->edges[i].face_a != topo->edges[i].face_b);
        ASSERT_TRUE(topo->edges[i].face_a >= 0);
        ASSERT_TRUE(topo->edges[i].face_a < topo->face_count);
        if (topo->edges[i].face_b >= 0)
            ASSERT_TRUE(topo->edges[i].face_b < topo->face_count);
    }

    dc_topo_free(topo);
    free(data);
}

/* Large sphere — face_count should equal ntri (each face unique normal) */
static void test_stress_sphere_all_unique(void)
{
    int ntri;
    float *data = build_sphere(64, &ntri);
    DC_Topo *topo = dc_topo_build(data, ntri);
    ASSERT_TRUE(topo != NULL);

    /* UV sphere normals are all different → each tri is its own face.
     * Allow a small tolerance for polar degenerate tris. */
    ASSERT_TRUE(topo->face_count >= ntri * 9 / 10);

    /* Edge count should be approximately 3*ntri (every edge is a face boundary) */
    ASSERT_TRUE(topo->edge_count >= ntri);

    dc_topo_free(topo);
    free(data);
}

/* Multi-cube: 8 cubes stacked, should get 6*8=48 faces minus shared faces.
 * Actually each cube is independent (not sharing edges), so 48 faces. */
static void test_stress_multi_cube_stack(void)
{
    /* 8 cubes, each 12 tris = 96 tris */
    float *data = (float *)calloc(96 * 18, sizeof(float));
    int t = 0;
    for (int c = 0; c < 8; c++) {
        float ox = (float)(c % 2) * 2.0f;
        float oy = (float)((c / 2) % 2) * 2.0f;
        float oz = (float)(c / 4) * 2.0f;
        /* +Z */
        set_tri(data, t++, 0,0,1,  ox,oy,oz+1, ox+1,oy,oz+1, ox+1,oy+1,oz+1);
        set_tri(data, t++, 0,0,1,  ox,oy,oz+1, ox+1,oy+1,oz+1, ox,oy+1,oz+1);
        /* -Z */
        set_tri(data, t++, 0,0,-1,  ox,oy,oz, ox,oy+1,oz, ox+1,oy+1,oz);
        set_tri(data, t++, 0,0,-1,  ox,oy,oz, ox+1,oy+1,oz, ox+1,oy,oz);
        /* +X */
        set_tri(data, t++, 1,0,0,  ox+1,oy,oz, ox+1,oy+1,oz, ox+1,oy+1,oz+1);
        set_tri(data, t++, 1,0,0,  ox+1,oy,oz, ox+1,oy+1,oz+1, ox+1,oy,oz+1);
        /* -X */
        set_tri(data, t++, -1,0,0,  ox,oy,oz, ox,oy,oz+1, ox,oy+1,oz+1);
        set_tri(data, t++, -1,0,0,  ox,oy,oz, ox,oy+1,oz+1, ox,oy+1,oz);
        /* +Y */
        set_tri(data, t++, 0,1,0,  ox,oy+1,oz, ox+1,oy+1,oz, ox+1,oy+1,oz+1);
        set_tri(data, t++, 0,1,0,  ox,oy+1,oz, ox+1,oy+1,oz+1, ox,oy+1,oz+1);
        /* -Y */
        set_tri(data, t++, 0,-1,0,  ox,oy,oz, ox+1,oy,oz, ox+1,oy,oz+1);
        set_tri(data, t++, 0,-1,0,  ox,oy,oz, ox+1,oy,oz+1, ox,oy,oz+1);
    }
    ASSERT_EQ(t, 96);

    DC_Topo *topo = dc_topo_build(data, 96);
    ASSERT_TRUE(topo != NULL);

    /* Cubes at gap=2, size=1 → no shared edges between cubes.
     * Each cube has 6 faces. Adjacent cubes with same normal ARE separate
     * because they don't share edges. Total = 48 face groups. */
    ASSERT_EQ(topo->face_count, 48);

    /* Each face group has exactly 2 triangles */
    for (int g = 0; g < topo->face_count; g++)
        ASSERT_EQ(topo->face_groups[g].tri_count, 2);

    /* Each cube has 12 edges → 8*12 = 96 edges total (cubes don't share edges) */
    ASSERT_EQ(topo->edge_count, 96);

    dc_topo_free(topo);
    free(data);
}

/* Test with real STL from Trinity Site if available */
static void test_real_stl_cube(void)
{
    /* Generate a cube STL via ts_interp */
    const char *stl_path = "/tmp/test_topo_cube.stl";
    const char *scad_path = "/tmp/test_topo_cube.scad";
    FILE *scad = fopen(scad_path, "w");
    if (!scad) { printf("    (skipped — cannot write temp)\n"); return; }
    fprintf(scad, "cube([10,10,10]);\n");
    fclose(scad);
    int ret = system("ts_interp /tmp/test_topo_cube.scad -o /tmp/test_topo_cube.stl >/dev/null 2>&1");
    if (ret != 0) {
        printf("    (skipped — ts_interp not available)\n");
        remove(scad_path);
        return;
    }

    /* Load STL */
    FILE *f = fopen(stl_path, "rb");
    if (!f) { printf("    (skipped — cannot open STL)\n"); return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 84) { fclose(f); return; }

    unsigned char *raw = (unsigned char *)malloc((size_t)size);
    fread(raw, 1, (size_t)size, f);
    fclose(f);

    /* Parse binary STL manually (same as stl_loader.c) */
    unsigned int ntri;
    memcpy(&ntri, raw + 80, 4);
    float *data = (float *)malloc(ntri * 18 * sizeof(float));
    const unsigned char *ptr = raw + 84;
    float *out = data;
    for (unsigned int t = 0; t < ntri; t++) {
        float normal[3];
        memcpy(normal, ptr, 12); ptr += 12;
        for (int v = 0; v < 3; v++) {
            out[0] = normal[0]; out[1] = normal[2]; out[2] = -normal[1];
            float vx, vy, vz;
            memcpy(&vx, ptr, 4); memcpy(&vy, ptr+4, 4); memcpy(&vz, ptr+8, 4);
            out[3] = vx; out[4] = vz; out[5] = -vy;
            ptr += 12; out += 6;
        }
        ptr += 2;
    }
    free(raw);

    DC_Topo *topo = dc_topo_build(data, (int)ntri);
    ASSERT_TRUE(topo != NULL);

    /* A cube from Trinity Site should have exactly 6 face groups */
    ASSERT_EQ(topo->face_count, 6);
    ASSERT_EQ(topo->edge_count, 12);

    /* Invariant check */
    int sum = 0;
    for (int i = 0; i < topo->face_count; i++)
        sum += topo->face_groups[i].tri_count;
    ASSERT_EQ(sum, (int)ntri);

    dc_topo_free(topo);
    free(data);
    remove(stl_path);
    remove(scad_path);
}

/* Test with a real sphere STL from Trinity Site */
static void test_real_stl_sphere(void)
{
    const char *stl_path = "/tmp/test_topo_sphere.stl";
    const char *scad_path = "/tmp/test_topo_sphere.scad";
    FILE *scad = fopen(scad_path, "w");
    if (!scad) { printf("    (skipped)\n"); return; }
    fprintf(scad, "$fn=32; sphere(r=10);\n");
    fclose(scad);
    int ret = system("ts_interp /tmp/test_topo_sphere.scad -o /tmp/test_topo_sphere.stl >/dev/null 2>&1");
    if (ret != 0) {
        printf("    (skipped — ts_interp not available)\n");
        remove(scad_path);
        return;
    }

    FILE *f = fopen(stl_path, "rb");
    if (!f) { printf("    (skipped)\n"); return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 84) { fclose(f); return; }

    unsigned char *raw = (unsigned char *)malloc((size_t)size);
    fread(raw, 1, (size_t)size, f);
    fclose(f);

    unsigned int ntri;
    memcpy(&ntri, raw + 80, 4);
    float *data = (float *)malloc(ntri * 18 * sizeof(float));
    const unsigned char *ptr = raw + 84;
    float *out = data;
    for (unsigned int t = 0; t < ntri; t++) {
        float normal[3];
        memcpy(normal, ptr, 12); ptr += 12;
        for (int v = 0; v < 3; v++) {
            out[0] = normal[0]; out[1] = normal[2]; out[2] = -normal[1];
            float vx, vy, vz;
            memcpy(&vx, ptr, 4); memcpy(&vy, ptr+4, 4); memcpy(&vz, ptr+8, 4);
            out[3] = vx; out[4] = vz; out[5] = -vy;
            ptr += 12; out += 6;
        }
        ptr += 2;
    }
    free(raw);

    DC_Topo *topo = dc_topo_build(data, (int)ntri);
    ASSERT_TRUE(topo != NULL);

    /* Real sphere from Trinity Site: $fn=32 UV sphere has many shared normals
     * at latitude rings. face_count should be > 1 (not all one face) and
     * edges should exist. The exact count depends on triangle layout. */
    ASSERT_TRUE(topo->face_count > 1);
    ASSERT_TRUE(topo->face_count <= (int)ntri);
    ASSERT_TRUE(topo->edge_count > 0);

    /* Invariant: sum of tri_counts == ntri */
    int sum = 0;
    for (int i = 0; i < topo->face_count; i++)
        sum += topo->face_groups[i].tri_count;
    ASSERT_EQ(sum, (int)ntri);

    printf("    (sphere: %u tris → %d faces, %d edges)\n",
           ntri, topo->face_count, topo->edge_count);

    dc_topo_free(topo);
    free(data);
    remove(stl_path);
    remove(scad_path);
}

/* =========================================================================
 * PICK-COLOR encoding tests
 * ========================================================================= */

static void test_pick_color_roundtrip_basic(void)
{
    /* Encode (obj=0, sub=0) and decode back */
    float rgb[3];
    dc_topo_sub_to_color(0, 0, rgb);
    unsigned char r = (unsigned char)(rgb[0] * 255.0f + 0.5f);
    unsigned char g = (unsigned char)(rgb[1] * 255.0f + 0.5f);
    unsigned char b = (unsigned char)(rgb[2] * 255.0f + 0.5f);
    int obj, sub;
    dc_topo_color_to_sub(r, g, b, &obj, &sub);
    ASSERT_EQ(obj, 0);
    ASSERT_EQ(sub, 0);
}

static void test_pick_color_roundtrip_range(void)
{
    /* Test a range of obj/sub combinations */
    for (int o = 0; o < 50; o++) {
        for (int s = 0; s < 500; s += 7) {
            float rgb[3];
            dc_topo_sub_to_color(o, s, rgb);
            unsigned char r = (unsigned char)(rgb[0] * 255.0f + 0.5f);
            unsigned char g = (unsigned char)(rgb[1] * 255.0f + 0.5f);
            unsigned char b = (unsigned char)(rgb[2] * 255.0f + 0.5f);
            int out_obj, out_sub;
            dc_topo_color_to_sub(r, g, b, &out_obj, &out_sub);
            ASSERT_EQ(out_obj, o);
            ASSERT_EQ(out_sub, s);
        }
    }
}

static void test_pick_color_background(void)
{
    /* Background pixel (0,0,0) should decode to (-1, -1) */
    int obj, sub;
    dc_topo_color_to_sub(0, 0, 0, &obj, &sub);
    ASSERT_EQ(obj, -1);
    ASSERT_EQ(sub, -1);
}

static void test_pick_color_max_sub(void)
{
    /* Max sub_id = 65534 (65536-2 because +1 encoding) */
    float rgb[3];
    dc_topo_sub_to_color(0, 65534, rgb);
    unsigned char r = (unsigned char)(rgb[0] * 255.0f + 0.5f);
    unsigned char g = (unsigned char)(rgb[1] * 255.0f + 0.5f);
    unsigned char b = (unsigned char)(rgb[2] * 255.0f + 0.5f);
    int obj, sub;
    dc_topo_color_to_sub(r, g, b, &obj, &sub);
    ASSERT_EQ(obj, 0);
    ASSERT_EQ(sub, 65534);
}

static void test_pick_color_max_obj(void)
{
    /* Max obj_idx = 254 (255-1 because +1 encoding) */
    float rgb[3];
    dc_topo_sub_to_color(254, 0, rgb);
    unsigned char r = (unsigned char)(rgb[0] * 255.0f + 0.5f);
    unsigned char g = (unsigned char)(rgb[1] * 255.0f + 0.5f);
    unsigned char b = (unsigned char)(rgb[2] * 255.0f + 0.5f);
    int obj, sub;
    dc_topo_color_to_sub(r, g, b, &obj, &sub);
    ASSERT_EQ(obj, 254);
    ASSERT_EQ(sub, 0);
}

static void test_pick_color_uniqueness(void)
{
    /* All (obj, sub) pairs within range produce unique RGB triples.
     * Test a subset to avoid combinatorial explosion. */
    /* 5 objects * 100 subs = 500 entries */
    unsigned int seen[500];
    int idx = 0;
    for (int o = 0; o < 5; o++) {
        for (int s = 0; s < 100; s++) {
            float rgb[3];
            dc_topo_sub_to_color(o, s, rgb);
            unsigned char r = (unsigned char)(rgb[0] * 255.0f + 0.5f);
            unsigned char g = (unsigned char)(rgb[1] * 255.0f + 0.5f);
            unsigned char b = (unsigned char)(rgb[2] * 255.0f + 0.5f);
            unsigned int key = ((unsigned int)r) | ((unsigned int)g << 8) | ((unsigned int)b << 16);
            /* Check no duplicate */
            for (int j = 0; j < idx; j++) {
                ASSERT_TRUE(seen[j] != key);
            }
            seen[idx++] = key;
        }
    }
}

/* =========================================================================
 * BENCHMARKS
 * ========================================================================= */

static void bench_cube(void)
{
    float *data = build_cube();
    BENCH_START();
    int iters = 100000;
    for (int i = 0; i < iters; i++) {
        DC_Topo *topo = dc_topo_build(data, 12);
        dc_topo_free(topo);
    }
    BENCH_END("cube (12 tri)", iters);
    free(data);
}

static void bench_flat_grid_100(void)
{
    int ntri;
    float *data = build_flat_grid(100, &ntri); /* 20,000 tri */
    BENCH_START();
    int iters = 100;
    for (int i = 0; i < iters; i++) {
        DC_Topo *topo = dc_topo_build(data, ntri);
        dc_topo_free(topo);
    }
    BENCH_END("flat grid 100x100 (20K tri)", iters);
    free(data);
}

static void bench_sphere_32(void)
{
    int ntri;
    float *data = build_sphere(32, &ntri); /* ~4096 tri */
    BENCH_START();
    int iters = 500;
    for (int i = 0; i < iters; i++) {
        DC_Topo *topo = dc_topo_build(data, ntri);
        dc_topo_free(topo);
    }
    BENCH_END("sphere fn=32 (~4K tri)", iters);
    free(data);
}

static void bench_sphere_128(void)
{
    int ntri;
    float *data = build_sphere(128, &ntri); /* ~65K tri */
    BENCH_START();
    int iters = 20;
    for (int i = 0; i < iters; i++) {
        DC_Topo *topo = dc_topo_build(data, ntri);
        dc_topo_free(topo);
    }
    BENCH_END("sphere fn=128 (~65K tri)", iters);
    free(data);
}

static void bench_flat_grid_316(void)
{
    int ntri;
    float *data = build_flat_grid(316, &ntri); /* ~200K tri */
    BENCH_START();
    int iters = 5;
    for (int i = 0; i < iters; i++) {
        DC_Topo *topo = dc_topo_build(data, ntri);
        dc_topo_free(topo);
    }
    BENCH_END("flat grid 316x316 (200K tri)", iters);
    free(data);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(int argc, char **argv)
{
    int do_bench = 0;
    int do_test = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bench") == 0) do_bench = 1;
        if (strcmp(argv[i], "--test") == 0) do_test = 1;
        if (strcmp(argv[i], "--all") == 0) { do_bench = 1; do_test = 1; }
    }

    if (do_test) {
        printf("=== dc_topo GREEN tests ===\n");
        RUN_TEST(test_null_input);
        RUN_TEST(test_single_triangle);
        RUN_TEST(test_quad_same_normal);
        RUN_TEST(test_dihedral_different_normals);
        RUN_TEST(test_cube_6_faces);
        RUN_TEST(test_cube_face_normals);
        RUN_TEST(test_flat_grid_one_face);
        RUN_TEST(test_sphere_many_faces);
        RUN_TEST(test_tri_to_face_consistency);
        RUN_TEST(test_edge_face_refs_valid);
        RUN_TEST(test_free_null);

        printf("\n=== dc_topo RED tests (edge cases) ===\n");
        RUN_TEST(test_zero_triangles);
        RUN_TEST(test_negative_triangles);
        RUN_TEST(test_degenerate_zero_area_tri);
        RUN_TEST(test_disconnected_triangles_same_normal);
        RUN_TEST(test_nearly_matching_normals);
        RUN_TEST(test_clearly_different_normals);

        printf("\n=== dc_topo STRESS tests (invariants) ===\n");
        RUN_TEST(test_invariant_tri_count_sum);
        RUN_TEST(test_invariant_tri_uniqueness);
        RUN_TEST(test_invariant_bidirectional_map);
        RUN_TEST(test_invariant_face_normal_consistency);
        RUN_TEST(test_invariant_edges_cross_faces);
        RUN_TEST(test_stress_sphere_all_unique);
        RUN_TEST(test_stress_multi_cube_stack);

        printf("\n=== dc_topo REAL STL tests (Trinity Site) ===\n");
        RUN_TEST(test_real_stl_cube);
        RUN_TEST(test_real_stl_sphere);

        printf("\n=== dc_topo PICK-COLOR encoding tests ===\n");
        RUN_TEST(test_pick_color_roundtrip_basic);
        RUN_TEST(test_pick_color_roundtrip_range);
        RUN_TEST(test_pick_color_background);
        RUN_TEST(test_pick_color_max_sub);
        RUN_TEST(test_pick_color_max_obj);
        RUN_TEST(test_pick_color_uniqueness);

        printf("\n--- Results: %d passed, %d failed ---\n", g_pass, g_fail);
    }

    if (do_bench) {
        printf("\n=== dc_topo BENCHMARKS ===\n");
        bench_cube();
        bench_flat_grid_100();
        bench_sphere_32();
        bench_sphere_128();
        bench_flat_grid_316();
        printf("\n--- %d benchmarks complete ---\n", g_bench_count);
    }

    return g_fail > 0 ? 1 : 0;
}
