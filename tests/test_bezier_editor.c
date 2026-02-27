/*
 * test_bezier_editor.c — Tests for DC_BezierEditor.
 *
 * GTK-dependent (same pattern as test_bezier_canvas.c).
 */

#include "bezier/bezier_editor.h"

#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Minimal test framework
 * ---------------------------------------------------------------------- */
static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  FAIL: %s:%d: assertion failed: %s\n", \
                    __FILE__, __LINE__, #cond); \
            return 1; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        fprintf(stderr, "  %-50s ", #fn); \
        int r = fn(); \
        if (r == 0) { fprintf(stderr, "PASS\n"); g_pass++; } \
        else        { fprintf(stderr, "(see above)\n"); g_fail++; } \
    } while (0)

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

static int
test_new_free(void)
{
    DC_BezierEditor *ed = dc_bezier_editor_new();
    ASSERT(ed != NULL);
    dc_bezier_editor_free(ed);
    return 0;
}

static int
test_free_null_safe(void)
{
    dc_bezier_editor_free(NULL);
    return 0;
}

static int
test_widget_not_null(void)
{
    DC_BezierEditor *ed = dc_bezier_editor_new();
    ASSERT(ed != NULL);
    GtkWidget *w = dc_bezier_editor_widget(ed);
    ASSERT(w != NULL);
    ASSERT(GTK_IS_WIDGET(w));
    dc_bezier_editor_free(ed);
    return 0;
}

static int
test_initial_state(void)
{
    DC_BezierEditor *ed = dc_bezier_editor_new();
    ASSERT(ed != NULL);
    ASSERT(dc_bezier_editor_point_count(ed) == 0);
    ASSERT(dc_bezier_editor_selected_point(ed) == -1);
    dc_bezier_editor_free(ed);
    return 0;
}

static int
test_null_accessors(void)
{
    ASSERT(dc_bezier_editor_widget(NULL) == NULL);
    ASSERT(dc_bezier_editor_point_count(NULL) == 0);
    ASSERT(dc_bezier_editor_selected_point(NULL) == -1);
    ASSERT(dc_bezier_editor_is_closed(NULL) == 0);
    return 0;
}

static int
test_initial_not_closed(void)
{
    DC_BezierEditor *ed = dc_bezier_editor_new();
    ASSERT(ed != NULL);
    ASSERT(dc_bezier_editor_is_closed(ed) == 0);
    dc_bezier_editor_free(ed);
    return 0;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(int argc, char *argv[])
{
    gtk_init();
    (void)argc; (void)argv;

    fprintf(stderr, "=== test_bezier_editor ===\n");

    RUN_TEST(test_new_free);
    RUN_TEST(test_free_null_safe);
    RUN_TEST(test_widget_not_null);
    RUN_TEST(test_initial_state);
    RUN_TEST(test_null_accessors);
    RUN_TEST(test_initial_not_closed);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
