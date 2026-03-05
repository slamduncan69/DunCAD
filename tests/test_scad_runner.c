#define _POSIX_C_SOURCE 200809L
#include "scad/scad_runner.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Write a temp .scad file, return heap path (caller frees) */
static char *
write_temp_scad(const char *code)
{
    char *path = strdup("/tmp/test_scad_runner_XXXXXX.scad");
    /* mkstemp needs a mutable template without .scad suffix */
    char tmpl[] = "/tmp/test_scad_runner_XXXXXX";
    int fd = mkstemp(tmpl);
    assert(fd >= 0);
    /* Rename to .scad */
    char scad[256];
    snprintf(scad, sizeof(scad), "%s.scad", tmpl);
    free(path);
    /* Write code to the fd, then close and rename */
    write(fd, code, strlen(code));
    close(fd);
    rename(tmpl, scad);
    return strdup(scad);
}

static void
test_sync_stl_export(void)
{
    printf("  test_sync_stl_export... ");

    char *scad = write_temp_scad("cube(10);");
    const char *stl = "/tmp/test_runner_out.stl";

    DC_ScadResult *r = dc_scad_run_sync(scad, stl, NULL, 0);
    assert(r != NULL);
    assert(r->exit_code == 0);
    assert(r->stderr_text != NULL);
    assert(r->output_path != NULL);
    assert(strcmp(r->output_path, stl) == 0);

    /* Verify STL file exists and has content */
    struct stat st;
    assert(stat(stl, &st) == 0);
    assert(st.st_size > 0);

    printf("OK (%.2fs, %ld bytes)\n", r->elapsed_secs, (long)st.st_size);

    dc_scad_result_free(r);
    unlink(stl);
    unlink(scad);
    free(scad);
}

static void
test_sync_png_preview(void)
{
    printf("  test_sync_png_preview... ");

    char *scad = write_temp_scad(
        "sphere(r=10, $fn=32);\n"
        "translate([25,0,0]) cube(15);\n"
    );
    const char *png = "/tmp/test_runner_out.png";

    const char *extra[] = {
        "--preview", "--viewall", "--autocenter",
        "--imgsize", "200,150"
    };
    DC_ScadResult *r = dc_scad_run_sync(scad, png, extra, 5);
    assert(r != NULL);
    assert(r->exit_code == 0);

    struct stat st;
    assert(stat(png, &st) == 0);
    assert(st.st_size > 100);  /* PNG has real content */

    printf("OK (%.2fs, %ld bytes)\n", r->elapsed_secs, (long)st.st_size);

    dc_scad_result_free(r);
    unlink(png);
    unlink(scad);
    free(scad);
}

static void
test_sync_parse_error(void)
{
    printf("  test_sync_parse_error... ");

    char *scad = write_temp_scad("cube([10, 10)];");
    const char *stl = "/tmp/test_runner_bad.stl";

    DC_ScadResult *r = dc_scad_run_sync(scad, stl, NULL, 0);
    assert(r != NULL);
    assert(r->exit_code != 0);
    /* stderr should contain error message */
    assert(strlen(r->stderr_text) > 0);
    assert(strstr(r->stderr_text, "ERROR") != NULL ||
           strstr(r->stderr_text, "error") != NULL ||
           strstr(r->stderr_text, "syntax") != NULL);

    printf("OK (exit=%d, stderr=%zu bytes)\n",
           r->exit_code, strlen(r->stderr_text));

    dc_scad_result_free(r);
    unlink(stl);
    unlink(scad);
    free(scad);
}

static void
test_binary_config(void)
{
    printf("  test_binary_config... ");

    assert(strcmp(dc_scad_get_binary(), "openscad") == 0);

    dc_scad_set_binary("/usr/bin/openscad");
    assert(strcmp(dc_scad_get_binary(), "/usr/bin/openscad") == 0);

    /* Should still work with explicit path */
    char *scad = write_temp_scad("cube(1);");
    const char *stl = "/tmp/test_runner_binconf.stl";
    DC_ScadResult *r = dc_scad_run_sync(scad, stl, NULL, 0);
    assert(r != NULL);
    assert(r->exit_code == 0);

    dc_scad_result_free(r);
    unlink(stl);
    unlink(scad);
    free(scad);

    /* Reset to default */
    dc_scad_set_binary(NULL);
    assert(strcmp(dc_scad_get_binary(), "openscad") == 0);

    printf("OK\n");
}

static void
test_null_safety(void)
{
    printf("  test_null_safety... ");

    assert(dc_scad_run_sync(NULL, NULL, NULL, 0) == NULL);
    assert(dc_scad_render_png(NULL, NULL, 0, 0, NULL, NULL) == NULL);
    assert(dc_scad_open_gui(NULL) == -1);
    dc_scad_result_free(NULL);  /* should not crash */
    dc_scad_job_cancel(NULL);   /* should not crash */

    printf("OK\n");
}

int
main(void)
{
    printf("test_scad_runner:\n");

    test_null_safety();
    test_binary_config();
    test_sync_stl_export();
    test_sync_png_preview();
    test_sync_parse_error();

    printf("All scad_runner tests passed.\n");
    return 0;
}
