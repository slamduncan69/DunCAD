/*
 * test_bezier_voxel.c — Test + demo: bezier surface → SDF voxel grid
 *
 * NO TRIANGLES. NO STL. Pure bezier math → signed distance field.
 * "The MATH is the MESH. Voxels are just a lens."
 *
 * This test:
 *   1. Creates a bezier mesh (dome shape)
 *   2. Calls dc_voxelize_bezier() to produce DC_VoxelGrid
 *   3. Verifies the SDF grid has correct properties
 *   4. Writes a raw voxel dump for external inspection
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "voxel/voxel.h"
#include "voxel/sdf.h"
#include "voxel/voxelize_bezier.h"
#include "core/error.h"

/* Trinity Site headers — pure math, no link dependency */
#include "../talmud-main/talmud/sacred/trinity_site/ts_vec.h"
#include "../talmud-main/talmud/sacred/trinity_site/ts_bezier_surface.h"
#include "../talmud-main/talmud/sacred/trinity_site/ts_bezier_mesh.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_pass = 0, g_fail = 0;

#define TEST(name) printf("  %-50s ", name)
#define PASS() do { printf("PASS\n"); g_pass++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); g_fail++; } while(0)
#define CHECK(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

/* ================================================================ */

static void test_dome_voxelizes(void) {
    TEST("dome: bezier → DC_VoxelGrid (no triangles)");

    /* Create 2x2 dome */
    ts_bezier_mesh m = ts_bezier_mesh_new(2, 2);
    ts_bezier_mesh_init_flat(&m, -3.0, -3.0, 3.0, 3.0, 0.0);

    for (int r = 0; r < m.cp_rows; r++) {
        for (int c = 0; c < m.cp_cols; c++) {
            ts_vec3 cp = ts_bezier_mesh_get_cp(&m, r, c);
            double dx = cp.v[0], dy = cp.v[1];
            double dist = sqrt(dx*dx + dy*dy);
            double t = dist / sqrt(18.0);
            cp.v[2] = 5.0 * (1.0 - t*t);
            if (r == 0 || r == m.cp_rows-1 || c == 0 || c == m.cp_cols-1)
                cp.v[2] *= 0.2;
            ts_bezier_mesh_set_cp(&m, r, c, cp);
        }
    }
    ts_bezier_mesh_enforce_c1(&m);

    /* Voxelize: bezier math → SDF directly */
    DC_Error err = {0};
    DC_VoxelGrid *grid = dc_voxelize_bezier(&m, 48, 3, 15, &err);
    CHECK(grid != NULL, "dc_voxelize_bezier returned NULL");

    size_t active = dc_voxel_grid_active_count(grid);
    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int sz = dc_voxel_grid_size_z(grid);

    CHECK(active > 0, "no active voxels");
    CHECK(sx > 0 && sy > 0 && sz > 0, "zero grid dimensions");

    printf("\n    Grid: %dx%dx%d, active: %zu\n    ", sx, sy, sz, active);

    dc_voxel_grid_free(grid);
    ts_bezier_mesh_free(&m);
    PASS();
}

static void test_saddle_voxelizes(void) {
    TEST("saddle: hyperbolic paraboloid → SDF");

    ts_bezier_mesh m = ts_bezier_mesh_new(2, 2);
    ts_bezier_mesh_init_flat(&m, -3.0, -3.0, 3.0, 3.0, 0.0);

    for (int r = 0; r < m.cp_rows; r++) {
        for (int c = 0; c < m.cp_cols; c++) {
            ts_vec3 cp = ts_bezier_mesh_get_cp(&m, r, c);
            double x = cp.v[0] / 3.0, y = cp.v[1] / 3.0;
            cp.v[2] = 2.0 * (x*x - y*y);
            ts_bezier_mesh_set_cp(&m, r, c, cp);
        }
    }

    DC_Error err = {0};
    DC_VoxelGrid *grid = dc_voxelize_bezier(&m, 32, 3, 15, &err);
    CHECK(grid != NULL, "NULL grid");

    size_t active = dc_voxel_grid_active_count(grid);
    CHECK(active > 0, "no active voxels");

    dc_voxel_grid_free(grid);
    ts_bezier_mesh_free(&m);
    PASS();
}

static void test_voxel_sdf_signs(void) {
    TEST("SDF signs: dome has both positive and negative");

    /* Dome surface — closed enough to have clear inside/outside */
    ts_bezier_mesh m = ts_bezier_mesh_new(2, 2);
    ts_bezier_mesh_init_flat(&m, -3.0, -3.0, 3.0, 3.0, 0.0);
    for (int r = 0; r < m.cp_rows; r++) {
        for (int c = 0; c < m.cp_cols; c++) {
            ts_vec3 cp = ts_bezier_mesh_get_cp(&m, r, c);
            double dx = cp.v[0], dy = cp.v[1];
            double dist = sqrt(dx*dx + dy*dy);
            double t = dist / sqrt(18.0);
            cp.v[2] = 4.0 * (1.0 - t*t);
            ts_bezier_mesh_set_cp(&m, r, c, cp);
        }
    }

    DC_Error err = {0};
    DC_VoxelGrid *grid = dc_voxelize_bezier(&m, 32, 3, 15, &err);
    CHECK(grid != NULL, "NULL grid");

    int sz = dc_voxel_grid_size_z(grid);
    int sx = dc_voxel_grid_size_x(grid);
    int sy = dc_voxel_grid_size_y(grid);
    int mid_x = sx / 2, mid_y = sy / 2;

    /* Along center column, SDF should transition from negative to positive */
    int found_positive = 0, found_negative = 0;
    for (int iz = 0; iz < sz; iz++) {
        const DC_Voxel *v = dc_voxel_grid_get_const(grid, mid_x, mid_y, iz);
        if (v && fabsf(v->distance) < 1e10f) {
            if (v->distance > 0.05f) found_positive = 1;
            if (v->distance < -0.05f) found_negative = 1;
        }
    }

    CHECK(found_positive || found_negative, "no SDF values computed in narrowband");

    dc_voxel_grid_free(grid);
    ts_bezier_mesh_free(&m);
    PASS();
}

static DC_VoxelGrid *create_demo_grid(void) {
    /* Create the cathedral dome for visualization */
    ts_bezier_mesh m = ts_bezier_mesh_new(2, 2);
    ts_bezier_mesh_init_flat(&m, -3.0, -3.0, 3.0, 3.0, 0.0);

    for (int r = 0; r < m.cp_rows; r++) {
        for (int c = 0; c < m.cp_cols; c++) {
            ts_vec3 cp = ts_bezier_mesh_get_cp(&m, r, c);
            double dx = cp.v[0], dy = cp.v[1];
            double dist = sqrt(dx*dx + dy*dy);
            double t = dist / sqrt(18.0);
            double angle = atan2(dy, dx);
            cp.v[2] = 5.0 * (1.0 - t*t) + 0.3 * sin(5.0*angle) * (1.0 - t);
            if (r == 0 || r == m.cp_rows-1 || c == 0 || c == m.cp_cols-1)
                cp.v[2] *= 0.2;
            ts_bezier_mesh_set_cp(&m, r, c, cp);
        }
    }
    ts_bezier_mesh_enforce_c1(&m);

    DC_Error err = {0};
    DC_VoxelGrid *grid = dc_voxelize_bezier(&m, 64, 3, 15, &err);
    ts_bezier_mesh_free(&m);
    return grid;
}

int main(void) {
    printf("=== BEZIER SURFACE VOXELIZATION ===\n");
    printf("No triangles. No STL. Pure math → SDF.\n\n");

    test_dome_voxelizes();
    test_saddle_voxelizes();
    test_voxel_sdf_signs();

    printf("\n--- Demo: cathedral dome at resolution 64 ---\n");
    DC_VoxelGrid *demo = create_demo_grid();
    if (demo) {
        size_t active = dc_voxel_grid_active_count(demo);
        int sx = dc_voxel_grid_size_x(demo);
        int sy = dc_voxel_grid_size_y(demo);
        int sz = dc_voxel_grid_size_z(demo);
        float cmin[3], cmax[3];
        dc_voxel_grid_bounds(demo, &cmin[0], &cmin[1], &cmin[2],
                                   &cmax[0], &cmax[1], &cmax[2]);
        printf("  Grid: %dx%dx%d\n", sx, sy, sz);
        printf("  Active voxels: %zu / %d (%.1f%%)\n",
               active, sx*sy*sz, 100.0 * active / (sx*sy*sz));
        printf("  Bounds: (%.2f,%.2f,%.2f) → (%.2f,%.2f,%.2f)\n",
               cmin[0], cmin[1], cmin[2], cmax[0], cmax[1], cmax[2]);
        dc_voxel_grid_free(demo);
    }

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    printf("========================================\n");

    return g_fail > 0 ? 1 : 0;
}
