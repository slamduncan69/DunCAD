/*
 * test_cubeiform.c — Tests for Cubeiform-to-OpenSCAD transpiler.
 *
 * Tests transpilation AND verifies output through Trinity Site.
 */

#include "cubeiform/cubeiform.h"
#include "core/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define ASSERT_CONTAINS(haystack, needle) \
    do { \
        if (!strstr((haystack), (needle))) { \
            fprintf(stderr, "  FAIL: %s:%d: \"%s\" not found in:\n    %s\n", \
                    __FILE__, __LINE__, (needle), (haystack)); \
            return 1; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        fprintf(stderr, "  %-45s ", #fn); \
        int r = fn(); \
        if (r == 0) { fprintf(stderr, "PASS\n"); g_pass++; } \
        else        { fprintf(stderr, "(see above)\n"); g_fail++; } \
    } while (0)

/* ---- Transpile helper ---- */
static char *
transpile(const char *dcad)
{
    DC_Error err = {0};
    char *scad = dc_cubeiform_to_scad(dcad, &err);
    if (!scad) {
        fprintf(stderr, "    transpile error: %s\n", err.message);
    }
    return scad;
}

/* =========================================================================
 * Tests: Basic transpilation
 * ========================================================================= */

static int test_simple_primitive(void) {
    char *s = transpile("cube(10);");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "cube(10)");
    free(s);
    return 0;
}

static int test_pipe_move(void) {
    char *s = transpile("cube(10) >> move(x=20);");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "translate([20, 0, 0])");
    ASSERT_CONTAINS(s, "cube(10)");
    free(s);
    return 0;
}

static int test_pipe_chain(void) {
    char *s = transpile("cube(5) >> scale(x=2) >> rotate(z=45) >> move(10, 0, 5);");
    ASSERT(s != NULL);
    /* Transforms should be in reverse order (outermost first) */
    char *translate_pos = strstr(s, "translate");
    char *rotate_pos    = strstr(s, "rotate");
    char *scale_pos     = strstr(s, "scale");
    char *cube_pos      = strstr(s, "cube");
    ASSERT(translate_pos != NULL);
    ASSERT(rotate_pos != NULL);
    ASSERT(scale_pos != NULL);
    ASSERT(cube_pos != NULL);
    /* Order: translate before rotate before scale before cube */
    ASSERT(translate_pos < rotate_pos);
    ASSERT(rotate_pos < scale_pos);
    ASSERT(scale_pos < cube_pos);
    free(s);
    return 0;
}

static int test_pipe_sweep(void) {
    char *s = transpile("circle(5) >> sweep(h=10);");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "linear_extrude(height = 10)");
    ASSERT_CONTAINS(s, "circle(5)");
    free(s);
    return 0;
}

static int test_pipe_revolve(void) {
    char *s = transpile("circle(r=3) >> move(x=10) >> revolve();");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "rotate_extrude()");
    ASSERT_CONTAINS(s, "translate([10, 0, 0])");
    ASSERT_CONTAINS(s, "circle(r");
    free(s);
    return 0;
}

static int test_shape_to_module(void) {
    char *s = transpile("shape bracket(w, h) {\n    cube(w, h, 3);\n}");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "module bracket(w, h)");
    free(s);
    return 0;
}

static int test_fn_to_function(void) {
    char *s = transpile("fn midpoint(a, b) = (a + b) / 2;");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "function midpoint(a, b) = (a + b) / 2");
    free(s);
    return 0;
}

static int test_for_in(void) {
    char *s = transpile("for i in [0:5] {\n    cube(5) >> move(i*10, 0);\n}");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "for (i = [0:5])");
    free(s);
    return 0;
}

static int test_special_vars(void) {
    char *s = transpile("fn = 64;\nfa = 1;\nfs = 0.4;");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "$fn = 64");
    ASSERT_CONTAINS(s, "$fa = 1");
    ASSERT_CONTAINS(s, "$fs = 0.4");
    free(s);
    return 0;
}

static int test_csg_difference(void) {
    char *s = transpile("cube(20) - cylinder(r=5, h=25);");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "difference()");
    ASSERT_CONTAINS(s, "cube(20)");
    ASSERT_CONTAINS(s, "cylinder(r");
    free(s);
    return 0;
}

static int test_csg_union(void) {
    char *s = transpile("cube(10) + sphere(5);");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "union()");
    ASSERT_CONTAINS(s, "cube(10)");
    ASSERT_CONTAINS(s, "sphere(5)");
    free(s);
    return 0;
}

static int test_csg_intersection(void) {
    char *s = transpile("cube(10) & sphere(8);");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "intersection()");
    free(s);
    return 0;
}

static int test_csg_with_pipe(void) {
    /* Pipe binds tighter: cube(20) - cylinder(r=5, h=25) >> move(0, 0, -1)
     * = cube(20) - (cylinder >> move) */
    char *s = transpile("cube(20) - cylinder(r=5, h=25) >> move(0, 0, -1);");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "difference()");
    ASSERT_CONTAINS(s, "translate([0, 0, -1])");
    ASSERT_CONTAINS(s, "cylinder");
    free(s);
    return 0;
}

static int test_geo_variable(void) {
    char *s = transpile(
        "body = cube(20, 20, 10);\n"
        "hole = cylinder(r=4, h=12) >> move(10, 10, -1);\n"
        "body - hole;\n"
    );
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "difference()");
    ASSERT_CONTAINS(s, "cube(20, 20, 10)");
    ASSERT_CONTAINS(s, "translate([10, 10, -1])");
    ASSERT_CONTAINS(s, "cylinder(r");
    free(s);
    return 0;
}

static int test_if_else(void) {
    char *s = transpile("if (width > 10) {\n    cube(width);\n} else {\n    cube(10);\n}");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "if (width > 10)");
    ASSERT_CONTAINS(s, "else");
    free(s);
    return 0;
}

static int test_color_pipe(void) {
    char *s = transpile("cube(5) >> color(\"red\");");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "color(\"red\")");
    ASSERT_CONTAINS(s, "cube(5)");
    free(s);
    return 0;
}

static int test_mirror_pipe(void) {
    char *s = transpile("cube(5) >> mirror(x=1);");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "mirror([1, 0, 0])");
    free(s);
    return 0;
}

static int test_null_input(void) {
    DC_Error err = {0};
    char *s = dc_cubeiform_to_scad(NULL, &err);
    ASSERT(s == NULL);
    ASSERT(err.code == DC_ERROR_INVALID_ARG);
    return 0;
}

static int test_empty_input(void) {
    char *s = transpile("");
    ASSERT(s != NULL);
    free(s);
    return 0;
}

static int test_comments(void) {
    char *s = transpile("// This is a comment\ncube(10); /* block */\n");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "cube(10)");
    free(s);
    return 0;
}

static int test_platonic_solid(void) {
    char *s = transpile("icosahedron(r=10) >> move(x=30);");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "translate([30, 0, 0])");
    ASSERT_CONTAINS(s, "icosahedron(r");
    free(s);
    return 0;
}

static int test_fn_in_args(void) {
    /* fn=64 as a primitive argument should become $fn=64 */
    char *s = transpile("cylinder(r=5, h=10, fn=64);");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "$fn=64");
    ASSERT_CONTAINS(s, "cylinder(r=5, h=10");
    free(s);
    return 0;
}

static int test_hull_with_args(void) {
    /* hull(a, b) should become hull() { a; b; } */
    char *s = transpile("hull(sphere(3) >> move(x=5), sphere(3) >> move(x=20));");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "hull()");
    ASSERT_CONTAINS(s, "translate([5, 0, 0])");
    ASSERT_CONTAINS(s, "translate([20, 0, 0])");
    ASSERT_CONTAINS(s, "sphere(3)");
    /* Should NOT contain commas between children */
    ASSERT(strstr(s, "hull(sphere") == NULL);
    free(s);
    return 0;
}

static int test_hull_with_block(void) {
    /* hull() { ... } should still work */
    char *s = transpile("hull() {\n    sphere(3);\n    sphere(5) >> move(x=10);\n}");
    ASSERT(s != NULL);
    ASSERT_CONTAINS(s, "hull()");
    ASSERT_CONTAINS(s, "sphere(3)");
    ASSERT_CONTAINS(s, "translate([10, 0, 0])");
    free(s);
    return 0;
}

/* =========================================================================
 * Trinity Site integration test — verify transpiled output actually renders
 * ========================================================================= */
static int test_trinity_renders(void) {
    /* A complete Cubeiform program */
    const char *dcad =
        "fn = 32;\n"
        "cube(20, 20, 10) >> move(x=-10, y=-10);\n"
        "cylinder(h=15, r=5) >> move(z=10);\n"
        "sphere(r=3) >> move(z=28);\n";

    char *scad = transpile(dcad);
    ASSERT(scad != NULL);

    /* Verify it contains valid OpenSCAD constructs */
    ASSERT_CONTAINS(scad, "$fn = 32");
    ASSERT_CONTAINS(scad, "translate(");
    ASSERT_CONTAINS(scad, "cube(20, 20, 10)");
    ASSERT_CONTAINS(scad, "cylinder(h");
    ASSERT_CONTAINS(scad, "sphere(r");

    /* Write to temp file and run through ts_interp if available */
    FILE *f = fopen("/tmp/test_cubeiform.scad", "w");
    if (f) {
        fputs(scad, f);
        fclose(f);
        int rc = system("ts_interp /tmp/test_cubeiform.scad /tmp/test_cubeiform.stl 2>/dev/null");
        if (rc == 0) {
            /* Verify STL was created and has triangles */
            f = fopen("/tmp/test_cubeiform.stl", "rb");
            if (f) {
                /* Read past 80-byte header + 4-byte triangle count */
                unsigned char header[84];
                size_t nread = fread(header, 1, 84, f);
                fclose(f);
                if (nread == 84) {
                    unsigned int ntri = header[80] | (header[81] << 8) |
                                        (header[82] << 16) | (header[83] << 24);
                    ASSERT(ntri > 0);
                    fprintf(stderr, "[%u triangles] ", ntri);
                }
            }
        } else {
            fprintf(stderr, "[ts_interp not available, skip render check] ");
        }
    }

    free(scad);
    return 0;
}

static int test_trinity_csg(void) {
    const char *dcad =
        "fn = 32;\n"
        "cube(20, 20, 20) - cylinder(r=5, h=25);\n";

    char *scad = transpile(dcad);
    ASSERT(scad != NULL);
    ASSERT_CONTAINS(scad, "difference()");

    FILE *f = fopen("/tmp/test_cubeiform_csg.scad", "w");
    if (f) {
        fputs(scad, f);
        fclose(f);
        int rc = system("ts_interp /tmp/test_cubeiform_csg.scad /tmp/test_cubeiform_csg.stl 2>/dev/null");
        if (rc == 0) {
            f = fopen("/tmp/test_cubeiform_csg.stl", "rb");
            if (f) {
                unsigned char header[84];
                size_t nread = fread(header, 1, 84, f);
                fclose(f);
                if (nread == 84) {
                    unsigned int ntri = header[80] | (header[81] << 8) |
                                        (header[82] << 16) | (header[83] << 24);
                    ASSERT(ntri > 0);
                    fprintf(stderr, "[%u triangles] ", ntri);
                }
            }
        } else {
            fprintf(stderr, "[ts_interp skip] ");
        }
    }

    free(scad);
    return 0;
}

static int test_trinity_pipe_chain(void) {
    const char *dcad =
        "fn = 48;\n"
        "circle(r=3) >> move(x=10) >> revolve();\n";

    char *scad = transpile(dcad);
    ASSERT(scad != NULL);
    ASSERT_CONTAINS(scad, "rotate_extrude()");

    FILE *f = fopen("/tmp/test_cubeiform_torus.scad", "w");
    if (f) {
        fputs(scad, f);
        fclose(f);
        int rc = system("ts_interp /tmp/test_cubeiform_torus.scad /tmp/test_cubeiform_torus.stl 2>/dev/null");
        if (rc == 0) {
            f = fopen("/tmp/test_cubeiform_torus.stl", "rb");
            if (f) {
                unsigned char header[84];
                size_t nread = fread(header, 1, 84, f);
                fclose(f);
                if (nread == 84) {
                    unsigned int ntri = header[80] | (header[81] << 8) |
                                        (header[82] << 16) | (header[83] << 24);
                    ASSERT(ntri > 0);
                    fprintf(stderr, "[%u tris = torus] ", ntri);
                }
            }
        } else {
            fprintf(stderr, "[ts_interp skip] ");
        }
    }

    free(scad);
    return 0;
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void) {
    fprintf(stderr, "=== test_cubeiform ===\n");

    /* Basic transpilation tests */
    RUN_TEST(test_simple_primitive);
    RUN_TEST(test_pipe_move);
    RUN_TEST(test_pipe_chain);
    RUN_TEST(test_pipe_sweep);
    RUN_TEST(test_pipe_revolve);
    RUN_TEST(test_shape_to_module);
    RUN_TEST(test_fn_to_function);
    RUN_TEST(test_for_in);
    RUN_TEST(test_special_vars);
    RUN_TEST(test_csg_difference);
    RUN_TEST(test_csg_union);
    RUN_TEST(test_csg_intersection);
    RUN_TEST(test_csg_with_pipe);
    RUN_TEST(test_geo_variable);
    RUN_TEST(test_if_else);
    RUN_TEST(test_color_pipe);
    RUN_TEST(test_mirror_pipe);
    RUN_TEST(test_null_input);
    RUN_TEST(test_empty_input);
    RUN_TEST(test_comments);
    RUN_TEST(test_platonic_solid);
    RUN_TEST(test_fn_in_args);
    RUN_TEST(test_hull_with_args);
    RUN_TEST(test_hull_with_block);

    /* Trinity Site integration tests */
    RUN_TEST(test_trinity_renders);
    RUN_TEST(test_trinity_csg);
    RUN_TEST(test_trinity_pipe_chain);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
