/*
 * test_marching_cubes.c — Tests for marching cubes isosurface extraction.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "voxel/voxel.h"
#include "voxel/sdf.h"
#include "voxel/marching_cubes.h"
#include "../talmud-main/talmud/sacred/trinity_site/ts_mesh.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  %-50s ", #name); } while(0)
#define PASS() do { printf("[PASS]\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("[FAIL] %s\n", msg); tests_failed++; } while(0)

/* =========================================================================
 * Test: sphere SDF → marching cubes produces valid mesh
 * ========================================================================= */
static void test_mc_sphere(void) {
    TEST(sphere_produces_vertices);

    int res = 32;
    float cs = 0.5f;
    DC_VoxelGrid *grid = dc_voxel_grid_new(res, res, res, cs);
    assert(grid);

    /* Sphere at grid center: SDF cell_center coords are (ix+0.5)*cs,
     * so grid center is at (res/2 * cs, res/2 * cs, res/2 * cs). */
    float center = (float)res * cs * 0.5f;
    float radius = 5.0f;
    dc_sdf_sphere(grid, center, center, center, radius);

    ts_mesh mesh = ts_mesh_init();
    int rc = dc_marching_cubes(grid, 0.0f, &mesh);

    if (rc != 0) { FAIL("dc_marching_cubes returned error"); goto done; }
    if (mesh.vert_count == 0) { FAIL("no vertices produced"); goto done; }
    if (mesh.tri_count == 0) { FAIL("no triangles produced"); goto done; }

    printf("[%d verts, %d tris] ", mesh.vert_count, mesh.tri_count);
    PASS();

    /* Check that all vertices are approximately on the sphere surface */
    TEST(sphere_vertices_near_radius);
    int bad = 0;
    double max_err = 0;
    for (int i = 0; i < mesh.vert_count; i++) {
        double x = mesh.verts[i].pos[0] - (double)center;
        double y = mesh.verts[i].pos[1] - (double)center;
        double z = mesh.verts[i].pos[2] - (double)center;
        double dist = sqrt(x*x + y*y + z*z);
        double err = fabs(dist - (double)radius);
        if (err > max_err) max_err = err;
        if (err > cs) bad++;  /* should be within one cell of the surface */
    }
    if (bad > 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%d verts off surface (max err=%.3f)", bad, max_err);
        FAIL(msg);
    } else {
        printf("[max_err=%.4f] ", max_err);
        PASS();
    }

    /* Verify normals point outward */
    TEST(sphere_normals_point_outward);
    int bad_normals = 0;
    for (int i = 0; i < mesh.vert_count; i++) {
        double x = mesh.verts[i].pos[0] - (double)center;
        double y = mesh.verts[i].pos[1] - (double)center;
        double z = mesh.verts[i].pos[2] - (double)center;
        double nx = mesh.verts[i].normal[0];
        double ny = mesh.verts[i].normal[1];
        double nz = mesh.verts[i].normal[2];
        /* Dot product of (pos-center) and normal should be positive (outward) */
        double dot = x*nx + y*ny + z*nz;
        if (dot < 0) bad_normals++;
    }
    if (bad_normals > mesh.vert_count / 10) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%d/%d normals point inward", bad_normals, mesh.vert_count);
        FAIL(msg);
    } else {
        PASS();
    }

done:
    ts_mesh_free(&mesh);
    dc_voxel_grid_free(grid);
}

/* =========================================================================
 * Test: box SDF → marching cubes
 * ========================================================================= */
static void test_mc_box(void) {
    TEST(box_produces_mesh);

    int res = 32;
    float cs = 0.5f;
    DC_VoxelGrid *grid = dc_voxel_grid_new(res, res, res, cs);
    assert(grid);

    /* Box centered at grid center */
    float center = (float)res * cs * 0.5f;
    dc_sdf_box(grid, center - 3, center - 3, center - 3,
                     center + 3, center + 3, center + 3);

    ts_mesh mesh = ts_mesh_init();
    int rc = dc_marching_cubes(grid, 0.0f, &mesh);

    if (rc != 0) { FAIL("dc_marching_cubes returned error"); goto done; }
    if (mesh.vert_count == 0) { FAIL("no vertices produced"); goto done; }
    if (mesh.tri_count == 0) { FAIL("no triangles produced"); goto done; }

    printf("[%d verts, %d tris] ", mesh.vert_count, mesh.tri_count);
    PASS();

    /* Verify vertices are near box surface */
    TEST(box_vertices_near_surface);
    double max_err = 0;
    for (int i = 0; i < mesh.vert_count; i++) {
        double x = fabs(mesh.verts[i].pos[0] - (double)center);
        double y = fabs(mesh.verts[i].pos[1] - (double)center);
        double z = fabs(mesh.verts[i].pos[2] - (double)center);
        /* Distance to box surface = max(|x|,|y|,|z|) - 3 for this centered box */
        double d = fmax(x, fmax(y, z)) - 3.0;
        double err = fabs(d);
        if (err > max_err) max_err = err;
    }
    printf("[max_err=%.4f] ", max_err);
    if (max_err > cs * 1.5) { FAIL("vertices too far from surface"); }
    else { PASS(); }

done:
    ts_mesh_free(&mesh);
    dc_voxel_grid_free(grid);
}

/* =========================================================================
 * Test: STL export round-trip
 * ========================================================================= */
static void test_mc_stl_export(void) {
    TEST(stl_export_roundtrip);

    int res = 24;
    float cs = 0.5f;
    DC_VoxelGrid *grid = dc_voxel_grid_new(res, res, res, cs);
    assert(grid);

    float ctr = (float)res * cs * 0.5f;
    dc_sdf_sphere(grid, ctr, ctr, ctr, 4.0f);

    ts_mesh mesh = ts_mesh_init();
    dc_marching_cubes(grid, 0.0f, &mesh);

    const char *path = "/tmp/test_mc_sphere.stl";
    int wrc = ts_mesh_write_stl(&mesh, path);
    if (wrc != 0) { FAIL("STL write failed"); goto done; }

    ts_mesh loaded = ts_mesh_init();
    int rrc = ts_mesh_read_stl(&loaded, path);
    if (rrc != 0) { FAIL("STL read failed"); goto done2; }

    if (loaded.tri_count != mesh.tri_count) {
        char msg[128];
        snprintf(msg, sizeof(msg), "tri count mismatch: wrote %d, read %d",
                 mesh.tri_count, loaded.tri_count);
        FAIL(msg);
    } else {
        printf("[%d tris] ", loaded.tri_count);
        PASS();
    }

done2:
    ts_mesh_free(&loaded);
done:
    ts_mesh_free(&mesh);
    dc_voxel_grid_free(grid);
}

/* =========================================================================
 * Test: null/empty grid handling
 * ========================================================================= */
static void test_mc_null(void) {
    TEST(null_grid_returns_error);
    ts_mesh mesh = ts_mesh_init();
    int rc = dc_marching_cubes(NULL, 0.0f, &mesh);
    if (rc != -1) { FAIL("should return -1 for NULL grid"); }
    else { PASS(); }
    ts_mesh_free(&mesh);
}

/* =========================================================================
 * Main
 * ========================================================================= */
int main(void) {
    printf("\n=== Marching Cubes Tests ===\n\n");

    test_mc_sphere();
    test_mc_box();
    test_mc_stl_export();
    test_mc_null();

    printf("\n--- Results: %d passed, %d failed ---\n\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
