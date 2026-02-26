/*
 * test_manifest.c — Tests for EF_Manifest and ef_manifest_capture_context().
 *
 * All tests are designed to run cleanly under AddressSanitizer with leak
 * detection enabled.
 */

#include "core/manifest.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * Helper: check that `haystack` contains `needle`
 * ---------------------------------------------------------------------- */
static int
contains(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

static int
test_new_and_free(void)
{
    EF_Manifest *m = ef_manifest_new("TestProject", "/tmp/test");
    ASSERT(m != NULL);
    ASSERT(strcmp(m->project_name, "TestProject") == 0);
    ASSERT(strcmp(m->project_root, "/tmp/test") == 0);
    ASSERT(ef_array_length(m->artifacts) == 0);
    ASSERT(ef_array_length(m->active_errors) == 0);
    ef_manifest_free(m);
    return 0;
}

static int
test_new_null_args_returns_null(void)
{
    ASSERT(ef_manifest_new(NULL, "/tmp") == NULL);
    ASSERT(ef_manifest_new("name", NULL) == NULL);
    return 0;
}

static int
test_free_null_is_safe(void)
{
    ef_manifest_free(NULL);
    return 0;
}

static int
test_add_artifact(void)
{
    EF_Manifest *m = ef_manifest_new("ArtifactTest", "/tmp/at");
    ASSERT(m != NULL);

    EF_Artifact a = {0};
    strncpy(a.path, "enclosure.scad", sizeof(a.path) - 1);
    a.type   = EF_ARTIFACT_SCAD;
    a.status = EF_STATUS_CLEAN;

    ASSERT(ef_manifest_add_artifact(m, &a) == 0);
    ASSERT(ef_array_length(m->artifacts) == 1);

    ef_manifest_free(m);
    return 0;
}

static int
test_find_artifact(void)
{
    EF_Manifest *m = ef_manifest_new("FindTest", "/tmp/ft");
    ASSERT(m != NULL);

    EF_Artifact a = {0};
    strncpy(a.path, "board.kicad_pcb", sizeof(a.path) - 1);
    a.type   = EF_ARTIFACT_KICAD_PCB;
    a.status = EF_STATUS_MODIFIED;

    ASSERT(ef_manifest_add_artifact(m, &a) == 0);

    EF_Artifact *found = ef_manifest_find_artifact(m, "board.kicad_pcb");
    ASSERT(found != NULL);
    ASSERT(found->type == EF_ARTIFACT_KICAD_PCB);
    ASSERT(found->status == EF_STATUS_MODIFIED);

    ASSERT(ef_manifest_find_artifact(m, "nonexistent.scad") == NULL);

    ef_manifest_free(m);
    return 0;
}

static int
test_find_artifact_null_args(void)
{
    EF_Manifest *m = ef_manifest_new("NullTest", "/tmp/nt");
    ASSERT(m != NULL);

    ASSERT(ef_manifest_find_artifact(NULL, "x") == NULL);
    ASSERT(ef_manifest_find_artifact(m, NULL) == NULL);

    ef_manifest_free(m);
    return 0;
}

static int
test_capture_context_empty_project(void)
{
    EF_Manifest *m = ef_manifest_new("EmptyProject", "/tmp/ep");
    ASSERT(m != NULL);

    char *json = ef_manifest_capture_context(m);
    ASSERT(json != NULL);

    /* Must contain required top-level keys */
    ASSERT(contains(json, "\"project_name\""));
    ASSERT(contains(json, "\"EmptyProject\""));
    ASSERT(contains(json, "\"project_root\""));
    ASSERT(contains(json, "\"/tmp/ep\""));
    ASSERT(contains(json, "\"generated_at\""));
    ASSERT(contains(json, "\"artifacts\""));
    ASSERT(contains(json, "\"active_errors\""));
    ASSERT(contains(json, "\"summary\""));
    ASSERT(contains(json, "\"total_artifacts\""));

    /* Empty project: total_artifacts should be 0 */
    ASSERT(contains(json, "\"total_artifacts\": 0"));

    free(json);
    ef_manifest_free(m);
    return 0;
}

static int
test_capture_context_with_artifacts(void)
{
    EF_Manifest *m = ef_manifest_new("FullProject", "/tmp/fp");
    ASSERT(m != NULL);

    EF_Artifact a1 = {0};
    strncpy(a1.path, "enclosure.scad", sizeof(a1.path) - 1);
    a1.type   = EF_ARTIFACT_SCAD;
    a1.status = EF_STATUS_CLEAN;
    strncpy(a1.last_modified, "2026-02-25T12:00:00Z", sizeof(a1.last_modified) - 1);
    ASSERT(ef_manifest_add_artifact(m, &a1) == 0);

    EF_Artifact a2 = {0};
    strncpy(a2.path, "board.kicad_pcb", sizeof(a2.path) - 1);
    a2.type   = EF_ARTIFACT_KICAD_PCB;
    a2.status = EF_STATUS_ERROR;
    strncpy(a2.last_error, "DRC violation: clearance", sizeof(a2.last_error) - 1);
    ASSERT(ef_manifest_add_artifact(m, &a2) == 0);

    char *json = ef_manifest_capture_context(m);
    ASSERT(json != NULL);

    ASSERT(contains(json, "\"FullProject\""));
    ASSERT(contains(json, "\"enclosure.scad\""));
    ASSERT(contains(json, "\"board.kicad_pcb\""));
    ASSERT(contains(json, "\"SCAD\""));
    ASSERT(contains(json, "\"KICAD_PCB\""));
    ASSERT(contains(json, "\"CLEAN\""));
    ASSERT(contains(json, "\"ERROR\""));
    ASSERT(contains(json, "DRC violation"));
    ASSERT(contains(json, "\"total_artifacts\": 2"));
    ASSERT(contains(json, "\"clean\": 1"));
    ASSERT(contains(json, "\"error\": 1"));

    free(json);
    ef_manifest_free(m);
    return 0;
}

static int
test_capture_context_json_escaping(void)
{
    EF_Manifest *m = ef_manifest_new("Quote\"Project", "/tmp/qp");
    ASSERT(m != NULL);

    char *json = ef_manifest_capture_context(m);
    ASSERT(json != NULL);

    /* The quote in the project name must be escaped */
    ASSERT(contains(json, "Quote\\\"Project"));

    free(json);
    ef_manifest_free(m);
    return 0;
}

static int
test_artifact_with_dependencies(void)
{
    EF_Manifest *m = ef_manifest_new("DepTest", "/tmp/dt");
    ASSERT(m != NULL);

    EF_Artifact a = {0};
    strncpy(a.path, "generated.scad", sizeof(a.path) - 1);
    a.type   = EF_ARTIFACT_SCAD_GENERATED;
    a.status = EF_STATUS_CLEAN;
    strncpy(a.generated_by, "bezier_tool", sizeof(a.generated_by) - 1);

    /* Add dependency */
    a.depends_on = ef_array_new(sizeof(char[512]));
    ASSERT(a.depends_on != NULL);
    char dep_path[512] = "source.bezier";
    ASSERT(ef_array_push(a.depends_on, dep_path) == 0);

    /* Add artifact — manifest takes ownership of depends_on */
    ASSERT(ef_manifest_add_artifact(m, &a) == 0);

    char *json = ef_manifest_capture_context(m);
    ASSERT(json != NULL);

    ASSERT(contains(json, "\"generated.scad\""));
    ASSERT(contains(json, "\"SCAD_GENERATED\""));
    ASSERT(contains(json, "\"bezier_tool\""));
    ASSERT(contains(json, "source.bezier"));

    free(json);
    ef_manifest_free(m);
    return 0;
}

static int
test_save_and_load_roundtrip(void)
{
    const char *tmp_path = "/tmp/ef_test_manifest.json";

    EF_Manifest *m = ef_manifest_new("RoundtripProject", "/tmp/rp");
    ASSERT(m != NULL);

    EF_Error err = {0};
    ASSERT(ef_manifest_save(m, tmp_path, &err) == 0);
    ASSERT(err.code == EF_OK);

    /* Reload — Phase 1 stub recovers project_name and project_root */
    EF_Manifest *loaded = ef_manifest_load(tmp_path, &err);
    ASSERT(loaded != NULL);
    ASSERT(err.code == EF_OK);
    ASSERT(strcmp(loaded->project_name, "RoundtripProject") == 0);

    ef_manifest_free(m);
    ef_manifest_free(loaded);

    /* Clean up temp file */
    remove(tmp_path);

    return 0;
}

static int
test_export_context_to_file(void)
{
    const char *tmp_path = "/tmp/ef_test_context.json";

    EF_Manifest *m = ef_manifest_new("ExportTest", "/tmp/et");
    ASSERT(m != NULL);

    EF_Error err = {0};
    ASSERT(ef_manifest_export_context_to_file(m, tmp_path, &err) == 0);
    ASSERT(err.code == EF_OK);

    /* Verify file was written */
    FILE *f = fopen(tmp_path, "r");
    ASSERT(f != NULL);
    char buf[64];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    /* File should start with '{' */
    ASSERT(buf[0] == '{');

    ef_manifest_free(m);
    remove(tmp_path);
    return 0;
}

static int
test_error_propagation(void)
{
    EF_Manifest *m = ef_manifest_new("ErrTest", "/tmp/err");
    ASSERT(m != NULL);

    EF_Error err = {0};

    /* Save to an unwritable path */
    int rc = ef_manifest_save(m, "/nonexistent/path/manifest.json", &err);
    ASSERT(rc == -1);
    ASSERT(err.code == EF_ERROR_IO);
    ASSERT(strlen(err.message) > 0);

    ef_manifest_free(m);
    return 0;
}

static int
test_artifact_type_strings(void)
{
    ASSERT(strcmp(ef_artifact_type_string(EF_ARTIFACT_SCAD),           "SCAD")           == 0);
    ASSERT(strcmp(ef_artifact_type_string(EF_ARTIFACT_SCAD_GENERATED), "SCAD_GENERATED") == 0);
    ASSERT(strcmp(ef_artifact_type_string(EF_ARTIFACT_KICAD_PCB),      "KICAD_PCB")      == 0);
    ASSERT(strcmp(ef_artifact_type_string(EF_ARTIFACT_KICAD_SCH),      "KICAD_SCH")      == 0);
    ASSERT(strcmp(ef_artifact_type_string(EF_ARTIFACT_STEP),           "STEP")           == 0);
    ASSERT(strcmp(ef_artifact_type_string(EF_ARTIFACT_STL),            "STL")            == 0);
    ASSERT(strcmp(ef_artifact_type_string(EF_ARTIFACT_UNKNOWN),        "UNKNOWN")        == 0);
    return 0;
}

static int
test_artifact_status_strings(void)
{
    ASSERT(strcmp(ef_artifact_status_string(EF_STATUS_CLEAN),    "CLEAN")    == 0);
    ASSERT(strcmp(ef_artifact_status_string(EF_STATUS_MODIFIED), "MODIFIED") == 0);
    ASSERT(strcmp(ef_artifact_status_string(EF_STATUS_ERROR),    "ERROR")    == 0);
    ASSERT(strcmp(ef_artifact_status_string(EF_STATUS_UNKNOWN),  "UNKNOWN")  == 0);
    return 0;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int
main(void)
{
    /* Suppress log output in tests */
    ef_log_init(NULL);

    fprintf(stderr, "=== test_manifest ===\n");

    RUN_TEST(test_new_and_free);
    RUN_TEST(test_new_null_args_returns_null);
    RUN_TEST(test_free_null_is_safe);
    RUN_TEST(test_add_artifact);
    RUN_TEST(test_find_artifact);
    RUN_TEST(test_find_artifact_null_args);
    RUN_TEST(test_capture_context_empty_project);
    RUN_TEST(test_capture_context_with_artifacts);
    RUN_TEST(test_capture_context_json_escaping);
    RUN_TEST(test_artifact_with_dependencies);
    RUN_TEST(test_save_and_load_roundtrip);
    RUN_TEST(test_export_context_to_file);
    RUN_TEST(test_error_propagation);
    RUN_TEST(test_artifact_type_strings);
    RUN_TEST(test_artifact_status_strings);

    ef_log_shutdown();

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
