#include "manifest.h"
#include "string_builder.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void
iso8601_now_manifest(char *buf, size_t buf_size)
{
    time_t     now = time(NULL);
    struct tm *utc = gmtime(&now); /* Not thread-safe; consistent with logger */

    strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", utc);
}

/*
 * json_escape_into_sb — append a JSON-escaped version of str to sb.
 * Returns 0 on success, -1 on failure.
 */
static int
json_escape_into_sb(DC_StringBuilder *sb, const char *str)
{
    if (!str) return dc_sb_append(sb, "");
    const char *p = str;
    while (*p) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') {
            if (dc_sb_append(sb, "\\\"") != 0) return -1;
        } else if (c == '\\') {
            if (dc_sb_append(sb, "\\\\") != 0) return -1;
        } else if (c == '\n') {
            if (dc_sb_append(sb, "\\n") != 0) return -1;
        } else if (c == '\r') {
            if (dc_sb_append(sb, "\\r") != 0) return -1;
        } else if (c == '\t') {
            if (dc_sb_append(sb, "\\t") != 0) return -1;
        } else if (c < 0x20) {
            if (dc_sb_appendf(sb, "\\u%04x", c) != 0) return -1;
        } else {
            if (dc_sb_append_char(sb, (char)c) != 0) return -1;
        }
        p++;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * String label helpers
 * ---------------------------------------------------------------------- */

const char *
dc_artifact_type_string(DC_ArtifactType type)
{
    switch (type) {
        case DC_ARTIFACT_SCAD:            return "SCAD";
        case DC_ARTIFACT_SCAD_GENERATED:  return "SCAD_GENERATED";
        case DC_ARTIFACT_KICAD_PCB:       return "KICAD_PCB";
        case DC_ARTIFACT_KICAD_SCH:       return "KICAD_SCH";
        case DC_ARTIFACT_STEP:            return "STEP";
        case DC_ARTIFACT_STL:             return "STL";
        case DC_ARTIFACT_UNKNOWN:         return "UNKNOWN";
        default:                          return "UNKNOWN";
    }
}

const char *
dc_artifact_status_string(DC_ArtifactStatus status)
{
    switch (status) {
        case DC_STATUS_CLEAN:    return "CLEAN";
        case DC_STATUS_MODIFIED: return "MODIFIED";
        case DC_STATUS_ERROR:    return "ERROR";
        case DC_STATUS_UNKNOWN:  return "UNKNOWN";
        default:                 return "UNKNOWN";
    }
}

/* -------------------------------------------------------------------------
 * dc_manifest_new
 * ---------------------------------------------------------------------- */
DC_Manifest *
dc_manifest_new(const char *project_name, const char *root_path)
{
    if (!project_name || !root_path) return NULL;

    DC_Manifest *m = calloc(1, sizeof(DC_Manifest));
    if (!m) return NULL;

    strncpy(m->project_name, project_name, sizeof(m->project_name) - 1);
    strncpy(m->project_root, root_path,    sizeof(m->project_root) - 1);

    m->artifacts = dc_array_new(sizeof(DC_Artifact));
    if (!m->artifacts) {
        free(m);
        return NULL;
    }

    m->active_errors = dc_array_new(sizeof(char[1024]));
    if (!m->active_errors) {
        dc_array_free(m->artifacts);
        free(m);
        return NULL;
    }

    return m;
}

/* -------------------------------------------------------------------------
 * dc_manifest_free
 * ---------------------------------------------------------------------- */
void
dc_manifest_free(DC_Manifest *manifest)
{
    if (!manifest) return;

    /* Free each artifact's depends_on array */
    size_t n = dc_array_length(manifest->artifacts);
    for (size_t i = 0; i < n; i++) {
        DC_Artifact *a = dc_array_get(manifest->artifacts, i);
        if (a && a->depends_on) {
            dc_array_free(a->depends_on);
            a->depends_on = NULL;
        }
    }

    dc_array_free(manifest->artifacts);
    dc_array_free(manifest->active_errors);
    free(manifest);
}

/* -------------------------------------------------------------------------
 * dc_manifest_add_artifact
 * ---------------------------------------------------------------------- */
int
dc_manifest_add_artifact(DC_Manifest *m, DC_Artifact *artifact)
{
    if (!m || !artifact) return -1;
    return dc_array_push(m->artifacts, artifact);
}

/* -------------------------------------------------------------------------
 * dc_manifest_find_artifact
 * ---------------------------------------------------------------------- */
DC_Artifact *
dc_manifest_find_artifact(DC_Manifest *m, const char *path)
{
    if (!m || !path) return NULL;

    size_t n = dc_array_length(m->artifacts);
    for (size_t i = 0; i < n; i++) {
        DC_Artifact *a = dc_array_get(m->artifacts, i);
        if (a && strcmp(a->path, path) == 0) {
            return a;
        }
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * dc_manifest_save — write manifest as JSON
 * ---------------------------------------------------------------------- */
int
dc_manifest_save(DC_Manifest *m, const char *path, DC_Error *err)
{
    if (!m || !path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL argument");
        return -1;
    }

    char *json = dc_manifest_capture_context(m);
    if (!json) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "context capture OOM");
        return -1;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "cannot open: %s", path);
        free(json);
        return -1;
    }

    int ok = (fputs(json, f) >= 0);
    fclose(f);
    free(json);

    if (!ok) {
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "write failed: %s", path);
        return -1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * dc_manifest_load — minimal stub (Phase 1)
 *
 * TODO: Full round-trip JSON deserialisation is deferred to Phase 2 once a
 * JSON parser dependency is chosen.  For now we open the file to verify it
 * exists and return an empty manifest with placeholder name/root extracted
 * via a simple string scan.  This satisfies test coverage requirements while
 * flagging the incompleteness.
 * ---------------------------------------------------------------------- */
DC_Manifest *
dc_manifest_load(const char *path, DC_Error *err)
{
    if (!path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL path");
        return NULL;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "cannot open: %s", path);
        return NULL;
    }

    /* Read entire file into memory */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size <= 0) {
        fclose(f);
        if (err) DC_SET_ERROR(err, DC_ERROR_PARSE, "empty file: %s", path);
        return NULL;
    }

    char *buf = malloc((size_t)file_size + 1);
    if (!buf) {
        fclose(f);
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "OOM reading file");
        return NULL;
    }

    size_t n_read = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    buf[n_read] = '\0';

    /* Minimal extraction: find "project_name" and "project_root" values.
     * TODO: replace with proper JSON parser in Phase 2. */
    char project_name[256] = "unknown";
    char project_root[512] = "/";

    const char *pn_key = "\"project_name\"";
    const char *pr_key = "\"project_root\"";

    /* Helper lambda-style local extraction via a macro */
#define EXTRACT_STRING(key, dest, dest_size) \
    do { \
        const char *kp = strstr(buf, (key)); \
        if (kp) { \
            kp += strlen((key)); \
            while (*kp == ' ' || *kp == ':' || *kp == ' ') kp++; \
            if (*kp == '"') { \
                kp++; \
                size_t wi = 0; \
                while (*kp && *kp != '"' && wi < (dest_size) - 1) { \
                    (dest)[wi++] = *kp++; \
                } \
                (dest)[wi] = '\0'; \
            } \
        } \
    } while(0)

    EXTRACT_STRING(pn_key, project_name, sizeof(project_name));
    EXTRACT_STRING(pr_key, project_root, sizeof(project_root));

#undef EXTRACT_STRING

    free(buf);

    DC_Manifest *loaded = dc_manifest_new(project_name, project_root);
    if (!loaded) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "OOM creating manifest");
    }
    return loaded;
}

/* -------------------------------------------------------------------------
 * dc_manifest_capture_context — serialise workspace state to JSON
 * ---------------------------------------------------------------------- */
char *
dc_manifest_capture_context(DC_Manifest *m)
{
    if (!m) return NULL;

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) return NULL;

    char ts[32];
    iso8601_now_manifest(ts, sizeof(ts));

    /* Compute summary statistics */
    size_t total        = dc_array_length(m->artifacts);
    size_t n_clean      = 0;
    size_t n_modified   = 0;
    size_t n_error      = 0;
    size_t n_unknown    = 0;
    size_t n_errs       = dc_array_length(m->active_errors);

    for (size_t i = 0; i < total; i++) {
        DC_Artifact *a = dc_array_get(m->artifacts, i);
        if (!a) continue;
        switch (a->status) {
            case DC_STATUS_CLEAN:    n_clean++;    break;
            case DC_STATUS_MODIFIED: n_modified++; break;
            case DC_STATUS_ERROR:    n_error++;    break;
            default:                 n_unknown++;  break;
        }
    }

#define SB_APPEND(str)        if (dc_sb_append(sb, (str))       != 0) goto oom
#define SB_APPENDF(fmt, ...)  if (dc_sb_appendf(sb, (fmt), ##__VA_ARGS__) != 0) goto oom
#define SB_ESCAPE(str)        if (json_escape_into_sb(sb, (str)) != 0) goto oom

    SB_APPEND("{\n");
    SB_APPEND("  \"project_name\": \"");  SB_ESCAPE(m->project_name); SB_APPEND("\",\n");
    SB_APPEND("  \"project_root\": \"");  SB_ESCAPE(m->project_root); SB_APPEND("\",\n");
    SB_APPEND("  \"generated_at\": \"");  SB_APPEND(ts);              SB_APPEND("\",\n");

    /* Artifacts array */
    SB_APPEND("  \"artifacts\": [\n");
    for (size_t i = 0; i < total; i++) {
        DC_Artifact *a = dc_array_get(m->artifacts, i);
        if (!a) continue;

        SB_APPEND("    {\n");
        SB_APPEND("      \"path\": \"");          SB_ESCAPE(a->path);                            SB_APPEND("\",\n");
        SB_APPEND("      \"type\": \"");           SB_APPEND(dc_artifact_type_string(a->type));   SB_APPEND("\",\n");
        SB_APPEND("      \"status\": \"");         SB_APPEND(dc_artifact_status_string(a->status)); SB_APPEND("\",\n");
        SB_APPEND("      \"last_error\": \"");     SB_ESCAPE(a->last_error);                      SB_APPEND("\",\n");
        SB_APPEND("      \"last_modified\": \"");  SB_ESCAPE(a->last_modified);                   SB_APPEND("\",\n");
        SB_APPEND("      \"generated_by\": \"");   SB_ESCAPE(a->generated_by);                    SB_APPEND("\",\n");

        /* depends_on sub-array */
        SB_APPEND("      \"depends_on\": [");
        if (a->depends_on) {
            size_t nd = dc_array_length(a->depends_on);
            for (size_t j = 0; j < nd; j++) {
                char *dep = dc_array_get(a->depends_on, j);
                SB_APPEND("\"");
                if (dep) { SB_ESCAPE(dep); }
                SB_APPEND("\"");
                if (j + 1 < nd) SB_APPEND(", ");
            }
        }
        SB_APPEND("]\n");

        SB_APPEND("    }");
        if (i + 1 < total) SB_APPEND(",");
        SB_APPEND("\n");
    }
    SB_APPEND("  ],\n");

    /* Active errors array */
    SB_APPEND("  \"active_errors\": [\n");
    for (size_t i = 0; i < n_errs; i++) {
        char *e = dc_array_get(m->active_errors, i);
        SB_APPEND("    \"");
        if (e) { SB_ESCAPE(e); }
        SB_APPEND("\"");
        if (i + 1 < n_errs) SB_APPEND(",");
        SB_APPEND("\n");
    }
    SB_APPEND("  ],\n");

    /* Summary section */
    SB_APPEND("  \"summary\": {\n");
    SB_APPENDF("    \"total_artifacts\": %zu,\n", total);
    SB_APPENDF("    \"clean\": %zu,\n",           n_clean);
    SB_APPENDF("    \"modified\": %zu,\n",        n_modified);
    SB_APPENDF("    \"error\": %zu,\n",           n_error);
    SB_APPENDF("    \"unknown\": %zu,\n",         n_unknown);
    SB_APPENDF("    \"active_error_count\": %zu\n", n_errs);
    SB_APPEND("  }\n");
    SB_APPEND("}\n");

#undef SB_APPEND
#undef SB_APPENDF
#undef SB_ESCAPE

    /* dc_sb_take transfers the buffer; we must still free the empty builder struct */
    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;

oom:
    dc_sb_free(sb);
    return NULL;
}

/* -------------------------------------------------------------------------
 * dc_manifest_export_context_to_file
 * ---------------------------------------------------------------------- */
int
dc_manifest_export_context_to_file(DC_Manifest *m, const char *path,
                                    DC_Error *err)
{
    if (!m || !path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL argument");
        return -1;
    }

    char *json = dc_manifest_capture_context(m);
    if (!json) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "context capture OOM");
        return -1;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "cannot open: %s", path);
        free(json);
        return -1;
    }

    int ok = (fputs(json, f) >= 0);
    fclose(f);
    free(json);

    if (!ok) {
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "write failed: %s", path);
        return -1;
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_LLM, "context exported to: %s", path);
    return 0;
}
