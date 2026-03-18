/*
 * test_voxel.c — Tests for voxel grid + SDF operations.
 * No GTK dependency — links only dc_core.
 */

#include "voxel/voxel.h"
#include "voxel/sdf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- Minimal test framework ---- */
static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        fprintf(stderr, "  %-44s ", #fn); \
        int r = fn(); \
        if (r == 0) { fprintf(stderr, "PASS\n"); g_pass++; } \
        else        { fprintf(stderr, "(see above)\n"); g_fail++; } \
    } while (0)

/* ---- Tests ---- */

static int
test_grid_new(void)
{
    DC_VoxelGrid *g = dc_voxel_grid_new(8, 8, 8, 1.0f);
    ASSERT(g != NULL);
    ASSERT(dc_voxel_grid_size_x(g) == 8);
    ASSERT(dc_voxel_grid_size_y(g) == 8);
    ASSERT(dc_voxel_grid_size_z(g) == 8);
    ASSERT(dc_voxel_grid_cell_size(g) == 1.0f);
    ASSERT(dc_voxel_grid_active_count(g) == 0);
    dc_voxel_grid_free(g);
    return 0;
}

static int
test_grid_set_get(void)
{
    DC_VoxelGrid *g = dc_voxel_grid_new(4, 4, 4, 0.5f);
    ASSERT(g != NULL);

    DC_Voxel v = { .active = 1, .r = 255, .g = 128, .b = 64, .distance = -0.1f };
    ASSERT(dc_voxel_grid_set(g, 2, 2, 2, v) == 0);

    const DC_Voxel *got = dc_voxel_grid_get_const(g, 2, 2, 2);
    ASSERT(got != NULL);
    ASSERT(got->active == 1);
    ASSERT(got->r == 255);
    ASSERT(got->g == 128);
    ASSERT(got->b == 64);

    /* Out of bounds */
    ASSERT(dc_voxel_grid_set(g, -1, 0, 0, v) == -1);
    ASSERT(dc_voxel_grid_set(g, 4, 0, 0, v) == -1);
    ASSERT(dc_voxel_grid_get(g, 99, 0, 0) == NULL);

    ASSERT(dc_voxel_grid_active_count(g) == 1);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_grid_bounds(void)
{
    DC_VoxelGrid *g = dc_voxel_grid_new(10, 20, 30, 0.5f);
    float minx, miny, minz, maxx, maxy, maxz;
    dc_voxel_grid_bounds(g, &minx, &miny, &minz, &maxx, &maxy, &maxz);
    ASSERT(minx == 0.0f);
    ASSERT(miny == 0.0f);
    ASSERT(minz == 0.0f);
    ASSERT(fabsf(maxx - 5.0f) < 0.001f);   /* 10 * 0.5 */
    ASSERT(fabsf(maxy - 10.0f) < 0.001f);  /* 20 * 0.5 */
    ASSERT(fabsf(maxz - 15.0f) < 0.001f);  /* 30 * 0.5 */
    dc_voxel_grid_free(g);
    return 0;
}

static int
test_coord_conversion(void)
{
    DC_VoxelGrid *g = dc_voxel_grid_new(8, 8, 8, 2.0f);
    int ix, iy, iz;

    /* World (3.0, 5.0, 7.0) -> cell (1, 2, 3) with cell_size=2 */
    ASSERT(dc_voxel_grid_world_to_cell(g, 3.0f, 5.0f, 7.0f, &ix, &iy, &iz) == 0);
    ASSERT(ix == 1);
    ASSERT(iy == 2);
    ASSERT(iz == 3);

    /* Cell center of (1, 2, 3) -> (3.0, 5.0, 7.0) */
    float wx, wy, wz;
    dc_voxel_grid_cell_center(g, 1, 2, 3, &wx, &wy, &wz);
    ASSERT(fabsf(wx - 3.0f) < 0.001f);
    ASSERT(fabsf(wy - 5.0f) < 0.001f);
    ASSERT(fabsf(wz - 7.0f) < 0.001f);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_fill_sphere(void)
{
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);
    /* Sphere at center (16,16,16), radius 8 */
    dc_voxel_grid_fill_sphere(g, 16.0f, 16.0f, 16.0f, 8.0f, 200, 50, 50);

    size_t active = dc_voxel_grid_active_count(g);
    /* Expected ~2145 voxels for r=8 sphere (4/3 * pi * 8^3 / 1^3 ≈ 2145) */
    ASSERT(active > 1500);
    ASSERT(active < 3000);

    /* Center should be active */
    const DC_Voxel *center = dc_voxel_grid_get_const(g, 16, 16, 16);
    ASSERT(center != NULL);
    ASSERT(center->active == 1);
    ASSERT(center->r == 200);

    /* Far corner should be inactive */
    const DC_Voxel *corner = dc_voxel_grid_get_const(g, 0, 0, 0);
    ASSERT(corner != NULL);
    ASSERT(corner->active == 0);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_fill_box(void)
{
    DC_VoxelGrid *g = dc_voxel_grid_new(16, 16, 16, 1.0f);
    dc_voxel_grid_fill_box(g, 4.0f, 4.0f, 4.0f, 12.0f, 12.0f, 12.0f,
                            50, 200, 50);

    size_t active = dc_voxel_grid_active_count(g);
    /* 8x8x8 = 512 voxels */
    ASSERT(active == 512);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_sdf_sphere(void)
{
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);
    dc_sdf_sphere(g, 16.0f, 16.0f, 16.0f, 8.0f);

    /* Center should have negative distance */
    const DC_Voxel *center = dc_voxel_grid_get_const(g, 16, 16, 16);
    ASSERT(center != NULL);
    ASSERT(center->distance < 0.0f);
    ASSERT(fabsf(center->distance + 8.0f) < 1.0f); /* ~-8 at center */

    /* Surface should be near 0 */
    const DC_Voxel *surf = dc_voxel_grid_get_const(g, 24, 16, 16);
    ASSERT(surf != NULL);
    ASSERT(fabsf(surf->distance) < 1.5f);

    /* Far away should be positive */
    const DC_Voxel *far = dc_voxel_grid_get_const(g, 0, 0, 0);
    ASSERT(far != NULL);
    ASSERT(far->distance > 10.0f);

    dc_sdf_activate_color(g, 200, 100, 100);
    size_t active = dc_voxel_grid_active_count(g);
    ASSERT(active > 1500);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_sdf_csg_union(void)
{
    int res = 32;
    float cs = 1.0f;
    DC_VoxelGrid *a = dc_voxel_grid_new(res, res, res, cs);
    DC_VoxelGrid *b = dc_voxel_grid_new(res, res, res, cs);
    DC_VoxelGrid *out = dc_voxel_grid_new(res, res, res, cs);

    dc_sdf_sphere(a, 12.0f, 16.0f, 16.0f, 6.0f);
    dc_sdf_sphere(b, 20.0f, 16.0f, 16.0f, 6.0f);

    ASSERT(dc_sdf_union(a, b, out) == 0);
    dc_sdf_activate(out);

    /* Union should have more voxels than either alone */
    dc_sdf_activate(a);
    dc_sdf_activate(b);
    size_t count_a = dc_voxel_grid_active_count(a);
    size_t count_b = dc_voxel_grid_active_count(b);
    size_t count_u = dc_voxel_grid_active_count(out);
    ASSERT(count_u >= count_a);
    ASSERT(count_u >= count_b);
    ASSERT(count_u <= count_a + count_b); /* can't exceed sum */

    dc_voxel_grid_free(a);
    dc_voxel_grid_free(b);
    dc_voxel_grid_free(out);
    return 0;
}

static int
test_sdf_csg_subtract(void)
{
    int res = 32;
    DC_VoxelGrid *a = dc_voxel_grid_new(res, res, res, 1.0f);
    DC_VoxelGrid *b = dc_voxel_grid_new(res, res, res, 1.0f);
    DC_VoxelGrid *out = dc_voxel_grid_new(res, res, res, 1.0f);

    /* Big sphere minus small sphere = hollow shell */
    dc_sdf_sphere(a, 16.0f, 16.0f, 16.0f, 10.0f);
    dc_sdf_sphere(b, 16.0f, 16.0f, 16.0f, 6.0f);

    ASSERT(dc_sdf_subtract(a, b, out) == 0);
    dc_sdf_activate(out);

    size_t count = dc_voxel_grid_active_count(out);
    ASSERT(count > 0);

    /* Center should be hollow (subtracted) */
    const DC_Voxel *center = dc_voxel_grid_get_const(out, 16, 16, 16);
    ASSERT(center != NULL);
    ASSERT(center->active == 0);

    /* Shell should be active */
    const DC_Voxel *shell = dc_voxel_grid_get_const(out, 24, 16, 16);
    ASSERT(shell != NULL);
    ASSERT(shell->active == 1);

    dc_voxel_grid_free(a);
    dc_voxel_grid_free(b);
    dc_voxel_grid_free(out);
    return 0;
}

static int
test_sdf_cylinder(void)
{
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);
    dc_sdf_cylinder(g, 16.0f, 16.0f, 5.0f, 8.0f, 24.0f);
    dc_sdf_activate(g);

    size_t active = dc_voxel_grid_active_count(g);
    /* pi * 5^2 * 16 ≈ 1257 */
    ASSERT(active > 800);
    ASSERT(active < 1800);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_sdf_torus(void)
{
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);
    dc_sdf_torus(g, 16.0f, 16.0f, 16.0f, 8.0f, 3.0f);
    dc_sdf_activate(g);

    size_t active = dc_voxel_grid_active_count(g);
    /* 2 * pi^2 * R * r^2 ≈ 2 * 9.87 * 8 * 9 ≈ 1421 */
    ASSERT(active > 800);
    ASSERT(active < 2200);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_sdf_color_by_normal(void)
{
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);
    dc_sdf_sphere(g, 16.0f, 16.0f, 16.0f, 8.0f);
    dc_sdf_activate(g);
    dc_sdf_color_by_normal(g);

    /* Check that surface voxels have non-zero colors.
     * Pick a voxel just inside the surface on +X side:
     * center=16, radius=8, cell 23 center = 23.5, dist = 7.5 - 8 = -0.5 */
    const DC_Voxel *v = dc_voxel_grid_get_const(g, 23, 16, 16);
    ASSERT(v != NULL);
    ASSERT(v->active == 1);
    /* Normal pointing +X should have r > 128 */
    ASSERT(v->r > 128);

    dc_voxel_grid_free(g);
    return 0;
}

/* ---- Transform tests ---- */

static int
test_transform_identity(void)
{
    /* sphere_t with NULL transform should behave like union at origin */
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);
    dc_sdf_sphere_t(g, 16.0f, 16.0f, 16.0f, 8.0f, NULL);
    dc_sdf_activate(g);

    size_t active = dc_voxel_grid_active_count(g);
    ASSERT(active > 1500);
    ASSERT(active < 3000);

    const DC_Voxel *center = dc_voxel_grid_get_const(g, 16, 16, 16);
    ASSERT(center && center->active == 1);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_transform_translate_sphere(void)
{
    /* Sphere at origin, translated to (16,16,16) in a 32^3 grid with cell_size=1.
     * Center of grid cell (16,16,16) = world (16.5, 16.5, 16.5).
     * After translate(16,16,16), sphere center is at world (16,16,16).
     * Cell (16,16,16) center is (16.5, 16.5, 16.5) — 0.87 from sphere center.
     * With r=6, distance at that cell = 0.87 - 6 = -5.13. Definitely inside. */
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);

    DC_SdfTransform t;
    dc_sdf_transform_identity(&t);
    dc_sdf_transform_translate(&t, 16.0f, 16.0f, 16.0f);

    dc_sdf_sphere_t(g, 0.0f, 0.0f, 0.0f, 6.0f, &t);
    dc_sdf_activate(g);

    /* Sphere should be centered near cell (16, 16, 16) */
    const DC_Voxel *at_center = dc_voxel_grid_get_const(g, 16, 16, 16);
    ASSERT(at_center && at_center->active == 1);
    ASSERT(at_center->distance < -4.0f);

    /* Origin should be empty (far from translated sphere) */
    const DC_Voxel *at_origin = dc_voxel_grid_get_const(g, 0, 0, 0);
    ASSERT(at_origin && at_origin->active == 0);

    /* Translated sphere voxel count should be reasonable */
    size_t active = dc_voxel_grid_active_count(g);
    /* 4/3 * pi * 6^3 ≈ 905 */
    ASSERT(active > 600);
    ASSERT(active < 1400);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_transform_translate_box(void)
{
    /* Box from (-3,-3,-3) to (3,3,3), translated to (16,16,16). */
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);

    DC_SdfTransform t;
    dc_sdf_transform_identity(&t);
    dc_sdf_transform_translate(&t, 16.0f, 16.0f, 16.0f);

    dc_sdf_box_t(g, -3.0f, -3.0f, -3.0f, 3.0f, 3.0f, 3.0f, &t);
    dc_sdf_activate(g);

    /* Cell (16, 16, 16) center = (16.5, 16.5, 16.5), local = (0.5, 0.5, 0.5) — inside box */
    const DC_Voxel *center = dc_voxel_grid_get_const(g, 16, 16, 16);
    ASSERT(center && center->active == 1);

    /* Cell (0, 0, 0) should be empty */
    const DC_Voxel *origin = dc_voxel_grid_get_const(g, 0, 0, 0);
    ASSERT(origin && origin->active == 0);

    /* ~6^3 = 216 voxels expected */
    size_t active = dc_voxel_grid_active_count(g);
    ASSERT(active > 150);
    ASSERT(active < 300);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_transform_scale_sphere(void)
{
    /* Sphere at origin with r=4, scaled by 2x -> effective r=8.
     * Translate to center of grid so it fits. */
    DC_VoxelGrid *g = dc_voxel_grid_new(48, 48, 48, 1.0f);

    DC_SdfTransform t;
    dc_sdf_transform_identity(&t);
    dc_sdf_transform_translate(&t, 24.0f, 24.0f, 24.0f);
    dc_sdf_transform_scale(&t, 2.0f, 2.0f, 2.0f);

    dc_sdf_sphere_t(g, 0.0f, 0.0f, 0.0f, 4.0f, &t);
    dc_sdf_activate(g);

    size_t active = dc_voxel_grid_active_count(g);
    /* Effective radius 8: 4/3 * pi * 8^3 ≈ 2145 */
    ASSERT(active > 1500);
    ASSERT(active < 3000);

    /* Point at distance 7 from center (inside scaled sphere) */
    const DC_Voxel *near_edge = dc_voxel_grid_get_const(g, 31, 24, 24);
    ASSERT(near_edge && near_edge->active == 1);

    /* Point at distance 9 from center (outside scaled sphere) */
    const DC_Voxel *outside = dc_voxel_grid_get_const(g, 33, 24, 24);
    ASSERT(outside && outside->active == 0);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_transform_rotate_box(void)
{
    /* Box from (-5,-2,-2) to (5,2,2) = 10x4x4.
     * Rotated 90 degrees around Z axis → becomes 4x10x4.
     * Translated to (16,16,16). */
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);

    DC_SdfTransform t;
    dc_sdf_transform_identity(&t);
    dc_sdf_transform_translate(&t, 16.0f, 16.0f, 16.0f);
    dc_sdf_transform_rotate(&t, 0.0f, 0.0f, 1.0f, 90.0f);

    dc_sdf_box_t(g, -5.0f, -2.0f, -2.0f, 5.0f, 2.0f, 2.0f, &t);
    dc_sdf_activate(g);

    /* After 90° Z rotation: X-long box becomes Y-long.
     * At world (16, 20, 16) -> local should be inside the box.
     * Cell (16, 20, 16) center = (16.5, 20.5, 16.5).
     * Local = inv(T*R) * world. T=(16,16,16), R=90°Z.
     * Inv = Rinv * Tinv. Tinv shifts by (-16,-16,-16) -> (0.5, 4.5, 0.5).
     * Rinv (90° Z) = -90° Z rotation: (x,y) -> (y, -x) = (4.5, -0.5).
     * Local = (4.5, -0.5, 0.5). Box is [-5,5]x[-2,2]x[-2,2]. Inside! */
    const DC_Voxel *rotated = dc_voxel_grid_get_const(g, 16, 20, 16);
    ASSERT(rotated && rotated->active == 1);

    /* At world (16, 16, 21) -> local after same inv:
     * Tinv -> (0.5, 0.5, 5.5). Rinv -> (0.5, -0.5, 5.5).
     * Box z is [-2,2], 5.5 > 2 -> outside. */
    const DC_Voxel *outside_z = dc_voxel_grid_get_const(g, 16, 16, 21);
    ASSERT(outside_z && outside_z->active == 0);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_transform_compose(void)
{
    /* Two spheres: one translated left, one translated right.
     * Both should appear in the grid via _t's min-union behavior. */
    DC_VoxelGrid *g = dc_voxel_grid_new(48, 16, 16, 1.0f);

    DC_SdfTransform t1;
    dc_sdf_transform_identity(&t1);
    dc_sdf_transform_translate(&t1, 8.0f, 8.0f, 8.0f);

    DC_SdfTransform t2;
    dc_sdf_transform_identity(&t2);
    dc_sdf_transform_translate(&t2, 40.0f, 8.0f, 8.0f);

    dc_sdf_sphere_t(g, 0.0f, 0.0f, 0.0f, 5.0f, &t1);
    dc_sdf_sphere_t(g, 0.0f, 0.0f, 0.0f, 5.0f, &t2);
    dc_sdf_activate(g);

    /* Both spheres should be active */
    const DC_Voxel *left = dc_voxel_grid_get_const(g, 8, 8, 8);
    ASSERT(left && left->active == 1);

    const DC_Voxel *right = dc_voxel_grid_get_const(g, 40, 8, 8);
    ASSERT(right && right->active == 1);

    /* Gap between them should be empty */
    const DC_Voxel *gap = dc_voxel_grid_get_const(g, 24, 8, 8);
    ASSERT(gap && gap->active == 0);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_transform_cylinder_t(void)
{
    /* Cylinder at origin, translated to (16,16,16) */
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);

    DC_SdfTransform t;
    dc_sdf_transform_identity(&t);
    dc_sdf_transform_translate(&t, 16.0f, 16.0f, 8.0f);

    dc_sdf_cylinder_t(g, 0.0f, 0.0f, 5.0f, 0.0f, 16.0f, &t);
    dc_sdf_activate(g);

    const DC_Voxel *center = dc_voxel_grid_get_const(g, 16, 16, 16);
    ASSERT(center && center->active == 1);

    size_t active = dc_voxel_grid_active_count(g);
    ASSERT(active > 800);

    dc_voxel_grid_free(g);
    return 0;
}

static int
test_transform_torus_t(void)
{
    /* Torus at origin, translated to (16,16,16) */
    DC_VoxelGrid *g = dc_voxel_grid_new(32, 32, 32, 1.0f);

    DC_SdfTransform t;
    dc_sdf_transform_identity(&t);
    dc_sdf_transform_translate(&t, 16.0f, 16.0f, 16.0f);

    dc_sdf_torus_t(g, 0.0f, 0.0f, 0.0f, 8.0f, 3.0f, &t);
    dc_sdf_activate(g);

    size_t active = dc_voxel_grid_active_count(g);
    ASSERT(active > 800);

    /* Center of torus (the hole) should be empty */
    const DC_Voxel *hole = dc_voxel_grid_get_const(g, 16, 16, 16);
    ASSERT(hole && hole->active == 0);

    /* On the ring should be active */
    const DC_Voxel *ring = dc_voxel_grid_get_const(g, 24, 16, 16);
    ASSERT(ring && ring->active == 1);

    dc_voxel_grid_free(g);
    return 0;
}

/* ---- main ---- */
int
main(void)
{
    fprintf(stderr, "=== test_voxel ===\n");

    RUN_TEST(test_grid_new);
    RUN_TEST(test_grid_set_get);
    RUN_TEST(test_grid_bounds);
    RUN_TEST(test_coord_conversion);
    RUN_TEST(test_fill_sphere);
    RUN_TEST(test_fill_box);
    RUN_TEST(test_sdf_sphere);
    RUN_TEST(test_sdf_csg_union);
    RUN_TEST(test_sdf_csg_subtract);
    RUN_TEST(test_sdf_cylinder);
    RUN_TEST(test_sdf_torus);
    RUN_TEST(test_sdf_color_by_normal);

    /* V1.6: Transform tests */
    RUN_TEST(test_transform_identity);
    RUN_TEST(test_transform_translate_sphere);
    RUN_TEST(test_transform_translate_box);
    RUN_TEST(test_transform_scale_sphere);
    RUN_TEST(test_transform_rotate_box);
    RUN_TEST(test_transform_compose);
    RUN_TEST(test_transform_cylinder_t);
    RUN_TEST(test_transform_torus_t);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
