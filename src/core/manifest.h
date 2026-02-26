#ifndef EF_MANIFEST_H
#define EF_MANIFEST_H

/*
 * manifest.h — Project manifest: tracks all design artifacts and their status.
 *
 * The manifest is the central workspace model.  It is intentionally free of
 * any UI dependencies so it can be serialised from a CLI flag, an HTTP
 * endpoint, or a Unix socket in the future.
 *
 * Ownership rules:
 *   - EF_Manifest owns its EF_Array *artifacts and EF_Array *active_errors.
 *   - Each EF_Artifact stored in the artifacts array is stored by value
 *     (copied in), so EF_Manifest owns those copies.
 *   - EF_Artifact.depends_on is an EF_Array * of (char *) path strings
 *     stored by value.  Each EF_Artifact owns its depends_on array.
 *   - ef_manifest_free() releases everything.
 *   - ef_manifest_capture_context() returns a heap-allocated string; caller
 *     must free() it.
 *   - ef_manifest_load() returns a heap-allocated EF_Manifest; caller must
 *     free with ef_manifest_free().
 */

#include "array.h"
#include "error.h"

/* -------------------------------------------------------------------------
 * Artifact type — describes the role of a design file.
 * ---------------------------------------------------------------------- */
typedef enum {
    EF_ARTIFACT_SCAD,
    EF_ARTIFACT_SCAD_GENERATED,
    EF_ARTIFACT_KICAD_PCB,
    EF_ARTIFACT_KICAD_SCH,
    EF_ARTIFACT_STEP,
    EF_ARTIFACT_STL,
    EF_ARTIFACT_UNKNOWN
} EF_ArtifactType;

/* -------------------------------------------------------------------------
 * Artifact status — reflects the last known build/validation state.
 * ---------------------------------------------------------------------- */
typedef enum {
    EF_STATUS_CLEAN,
    EF_STATUS_MODIFIED,
    EF_STATUS_ERROR,
    EF_STATUS_UNKNOWN
} EF_ArtifactStatus;

/* -------------------------------------------------------------------------
 * EF_Artifact — a single tracked design file.
 *
 * Fields:
 *   path          — relative or absolute path to the file (NUL-terminated)
 *   type          — semantic file type
 *   status        — last known status
 *   last_error    — human-readable error string; empty if no error
 *   last_modified — ISO-8601 timestamp of last modification
 *   generated_by  — tool or source that generated this file; empty if manual
 *   depends_on    — EF_Array of char[512] paths this artifact depends on;
 *                   owned by this struct; may be NULL (no dependencies)
 * ---------------------------------------------------------------------- */
typedef struct {
    char              path[512];
    EF_ArtifactType   type;
    EF_ArtifactStatus status;
    char              last_error[1024];
    char              last_modified[64];
    char              generated_by[256];
    EF_Array         *depends_on; /* EF_Array of char[512]; owned */
} EF_Artifact;

/* -------------------------------------------------------------------------
 * EF_Manifest — the top-level workspace model.
 *
 * Fields:
 *   project_name  — human-readable project name
 *   project_root  — absolute path to the project root directory
 *   artifacts     — EF_Array of EF_Artifact (stored by value); owned
 *   active_errors — EF_Array of char[1024] error messages; owned
 * ---------------------------------------------------------------------- */
typedef struct {
    char      project_name[256];
    char      project_root[512];
    EF_Array *artifacts;     /* EF_Array of EF_Artifact; owned */
    EF_Array *active_errors; /* EF_Array of char[1024]; owned */
} EF_Manifest;

/* -------------------------------------------------------------------------
 * ef_manifest_new — create an empty manifest.
 *
 * Parameters:
 *   project_name — display name for the project; copied in; must not be NULL
 *   root_path    — absolute path to project root; copied in; must not be NULL
 *
 * Returns: new EF_Manifest, or NULL on allocation failure.
 * Ownership: caller owns; free with ef_manifest_free().
 * ---------------------------------------------------------------------- */
EF_Manifest *ef_manifest_new(const char *project_name, const char *root_path);

/* -------------------------------------------------------------------------
 * ef_manifest_free — release all memory owned by manifest.
 *
 * Parameters:
 *   manifest — may be NULL (no-op)
 * ---------------------------------------------------------------------- */
void ef_manifest_free(EF_Manifest *manifest);

/* -------------------------------------------------------------------------
 * ef_manifest_add_artifact — copy artifact into the manifest's artifact list.
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
int ef_manifest_add_artifact(EF_Manifest *m, EF_Artifact *artifact);

/* -------------------------------------------------------------------------
 * ef_manifest_find_artifact — look up an artifact by its path.
 *
 * Parameters:
 *   m    — must not be NULL
 *   path — NUL-terminated path string to match against EF_Artifact.path
 *
 * Returns: pointer into internal array (borrowed), or NULL if not found.
 * Ownership: borrowed; invalidated by any mutating operation on the manifest.
 * ---------------------------------------------------------------------- */
EF_Artifact *ef_manifest_find_artifact(EF_Manifest *m, const char *path);

/* -------------------------------------------------------------------------
 * ef_manifest_save — serialise the manifest to a JSON file.
 *
 * Parameters:
 *   m    — must not be NULL
 *   path — file path to write; will be created or overwritten
 *   err  — populated on failure; may be NULL
 *
 * Returns: 0 on success, -1 on failure.
 * ---------------------------------------------------------------------- */
int ef_manifest_save(EF_Manifest *m, const char *path, EF_Error *err);

/* -------------------------------------------------------------------------
 * ef_manifest_load — deserialise a manifest from a JSON file.
 *
 * TODO: Phase 1 implementation is a minimal stub that reads the project_name
 * and project_root fields.  Full round-trip deserialisation is deferred to
 * Phase 2 once the JSON parser dependency is decided.
 *
 * Parameters:
 *   path — JSON file path to read
 *   err  — populated on failure; may be NULL
 *
 * Returns: new EF_Manifest, or NULL on failure.
 * Ownership: caller owns; free with ef_manifest_free().
 * ---------------------------------------------------------------------- */
EF_Manifest *ef_manifest_load(const char *path, EF_Error *err);

/* -------------------------------------------------------------------------
 * ef_manifest_capture_context — serialise workspace state to JSON for LLM.
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
char *ef_manifest_capture_context(EF_Manifest *m);

/* -------------------------------------------------------------------------
 * ef_manifest_export_context_to_file — write capture_context output to file.
 *
 * Parameters:
 *   m    — must not be NULL
 *   path — file path to write; created or overwritten
 *   err  — populated on failure; may be NULL
 *
 * Returns: 0 on success, -1 on failure.
 * ---------------------------------------------------------------------- */
int ef_manifest_export_context_to_file(EF_Manifest *m, const char *path,
                                        EF_Error *err);

/* -------------------------------------------------------------------------
 * Helper — return string label for EF_ArtifactType.
 * Returns a static string literal; caller must not free.
 * ---------------------------------------------------------------------- */
const char *ef_artifact_type_string(EF_ArtifactType type);

/* -------------------------------------------------------------------------
 * Helper — return string label for EF_ArtifactStatus.
 * Returns a static string literal; caller must not free.
 * ---------------------------------------------------------------------- */
const char *ef_artifact_status_string(EF_ArtifactStatus status);

#endif /* EF_MANIFEST_H */
