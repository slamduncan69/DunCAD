#ifndef DC_MANIFEST_H
#define DC_MANIFEST_H

/*
 * manifest.h — Project manifest: tracks all design artifacts and their status.
 *
 * The manifest is the central workspace model.  It is intentionally free of
 * any UI dependencies so it can be serialised from a CLI flag, an HTTP
 * endpoint, or a Unix socket in the future.
 *
 * Ownership rules:
 *   - DC_Manifest owns its DC_Array *artifacts and DC_Array *active_errors.
 *   - Each DC_Artifact stored in the artifacts array is stored by value
 *     (copied in), so DC_Manifest owns those copies.
 *   - DC_Artifact.depends_on is an DC_Array * of (char *) path strings
 *     stored by value.  Each DC_Artifact owns its depends_on array.
 *   - dc_manifest_free() releases everything.
 *   - dc_manifest_capture_context() returns a heap-allocated string; caller
 *     must free() it.
 *   - dc_manifest_load() returns a heap-allocated DC_Manifest; caller must
 *     free with dc_manifest_free().
 */

#include "array.h"
#include "error.h"

/* -------------------------------------------------------------------------
 * Artifact type — describes the role of a design file.
 * ---------------------------------------------------------------------- */
typedef enum {
    DC_ARTIFACT_SCAD,
    DC_ARTIFACT_SCAD_GENERATED,
    DC_ARTIFACT_KICAD_PCB,
    DC_ARTIFACT_KICAD_SCH,
    DC_ARTIFACT_STEP,
    DC_ARTIFACT_STL,
    DC_ARTIFACT_UNKNOWN
} DC_ArtifactType;

/* -------------------------------------------------------------------------
 * Artifact status — reflects the last known build/validation state.
 * ---------------------------------------------------------------------- */
typedef enum {
    DC_STATUS_CLEAN,
    DC_STATUS_MODIFIED,
    DC_STATUS_ERROR,
    DC_STATUS_UNKNOWN
} DC_ArtifactStatus;

/* -------------------------------------------------------------------------
 * DC_Artifact — a single tracked design file.
 *
 * Fields:
 *   path          — relative or absolute path to the file (NUL-terminated)
 *   type          — semantic file type
 *   status        — last known status
 *   last_error    — human-readable error string; empty if no error
 *   last_modified — ISO-8601 timestamp of last modification
 *   generated_by  — tool or source that generated this file; empty if manual
 *   depends_on    — DC_Array of char[512] paths this artifact depends on;
 *                   owned by this struct; may be NULL (no dependencies)
 * ---------------------------------------------------------------------- */
typedef struct {
    char              path[512];
    DC_ArtifactType   type;
    DC_ArtifactStatus status;
    char              last_error[1024];
    char              last_modified[64];
    char              generated_by[256];
    DC_Array         *depends_on; /* DC_Array of char[512]; owned */
} DC_Artifact;

/* -------------------------------------------------------------------------
 * DC_Manifest — the top-level workspace model.
 *
 * Fields:
 *   project_name  — human-readable project name
 *   project_root  — absolute path to the project root directory
 *   artifacts     — DC_Array of DC_Artifact (stored by value); owned
 *   active_errors — DC_Array of char[1024] error messages; owned
 * ---------------------------------------------------------------------- */
typedef struct {
    char      project_name[256];
    char      project_root[512];
    DC_Array *artifacts;     /* DC_Array of DC_Artifact; owned */
    DC_Array *active_errors; /* DC_Array of char[1024]; owned */
} DC_Manifest;

/* -------------------------------------------------------------------------
 * dc_manifest_new — create an empty manifest.
 *
 * Parameters:
 *   project_name — display name for the project; copied in; must not be NULL
 *   root_path    — absolute path to project root; copied in; must not be NULL
 *
 * Returns: new DC_Manifest, or NULL on allocation failure.
 * Ownership: caller owns; free with dc_manifest_free().
 * ---------------------------------------------------------------------- */
DC_Manifest *dc_manifest_new(const char *project_name, const char *root_path);

/* -------------------------------------------------------------------------
 * dc_manifest_free — release all memory owned by manifest.
 *
 * Parameters:
 *   manifest — may be NULL (no-op)
 * ---------------------------------------------------------------------- */
void dc_manifest_free(DC_Manifest *manifest);

/* -------------------------------------------------------------------------
 * dc_manifest_add_artifact — copy artifact into the manifest's artifact list.
 *
 * Parameters:
 *   m        — must not be NULL
 *   artifact — pointer to the artifact to copy; must not be NULL
 *              NOTE: if artifact->depends_on is non-NULL it is NOT deep-copied;
 *              ownership transfers to the manifest.  The caller must not free
 *              artifact->depends_on after this call.
 *
 * Returns: 0 on success, -1 on allocation failure.
 * ---------------------------------------------------------------------- */
int dc_manifest_add_artifact(DC_Manifest *m, DC_Artifact *artifact);

/* -------------------------------------------------------------------------
 * dc_manifest_find_artifact — look up an artifact by its path.
 *
 * Parameters:
 *   m    — must not be NULL
 *   path — NUL-terminated path string to match against DC_Artifact.path
 *
 * Returns: pointer into internal array (borrowed), or NULL if not found.
 * Ownership: borrowed; invalidated by any mutating operation on the manifest.
 * ---------------------------------------------------------------------- */
DC_Artifact *dc_manifest_find_artifact(DC_Manifest *m, const char *path);

/* -------------------------------------------------------------------------
 * dc_manifest_save — serialise the manifest to a JSON file.
 *
 * Parameters:
 *   m    — must not be NULL
 *   path — file path to write; will be created or overwritten
 *   err  — populated on failure; may be NULL
 *
 * Returns: 0 on success, -1 on failure.
 * ---------------------------------------------------------------------- */
int dc_manifest_save(DC_Manifest *m, const char *path, DC_Error *err);

/* -------------------------------------------------------------------------
 * dc_manifest_load — deserialise a manifest from a JSON file.
 *
 * TODO: Phase 1 implementation is a minimal stub that reads the project_name
 * and project_root fields.  Full round-trip deserialisation is deferred to
 * Phase 2 once the JSON parser dependency is decided.
 *
 * Parameters:
 *   path — JSON file path to read
 *   err  — populated on failure; may be NULL
 *
 * Returns: new DC_Manifest, or NULL on failure.
 * Ownership: caller owns; free with dc_manifest_free().
 * ---------------------------------------------------------------------- */
DC_Manifest *dc_manifest_load(const char *path, DC_Error *err);

/* -------------------------------------------------------------------------
 * dc_manifest_capture_context — serialise workspace state to JSON for LLM.
 *
 * The returned string is valid JSON containing:
 *   - project_name and project_root
 *   - generation timestamp (ISO-8601 UTC)
 *   - all artifacts with path, type, status, last_error, last_modified,
 *     generated_by, and depends_on list
 *   - active_errors list
 *   - summary section: total artifacts, counts per status, error count
 *
 * This function has zero UI dependencies and may be called from a CLI flag,
 * HTTP endpoint, or Unix socket.
 *
 * Parameters:
 *   m — must not be NULL
 *
 * Returns: heap-allocated NUL-terminated JSON string, or NULL on OOM.
 * Ownership: caller must free().
 * ---------------------------------------------------------------------- */
char *dc_manifest_capture_context(DC_Manifest *m);

/* -------------------------------------------------------------------------
 * dc_manifest_export_context_to_file — write capture_context output to file.
 *
 * Parameters:
 *   m    — must not be NULL
 *   path — file path to write; created or overwritten
 *   err  — populated on failure; may be NULL
 *
 * Returns: 0 on success, -1 on failure.
 * ---------------------------------------------------------------------- */
int dc_manifest_export_context_to_file(DC_Manifest *m, const char *path,
                                        DC_Error *err);

/* -------------------------------------------------------------------------
 * Helper — return string label for DC_ArtifactType.
 * Returns a static string literal; caller must not free.
 * ---------------------------------------------------------------------- */
const char *dc_artifact_type_string(DC_ArtifactType type);

/* -------------------------------------------------------------------------
 * Helper — return string label for DC_ArtifactStatus.
 * Returns a static string literal; caller must not free.
 * ---------------------------------------------------------------------- */
const char *dc_artifact_status_string(DC_ArtifactStatus status);

#endif /* DC_MANIFEST_H */
