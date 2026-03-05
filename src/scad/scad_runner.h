#ifndef DC_SCAD_RUNNER_H
#define DC_SCAD_RUNNER_H

/*
 * scad_runner.h — OpenSCAD CLI subprocess wrapper.
 *
 * Invokes the external `openscad` binary for rendering (PNG preview),
 * exporting (STL/OFF/AMF), and syntax checking. All operations are
 * async via GSubprocess so the GTK main loop is not blocked.
 *
 * No GTK dependency in this header — only GLib/GIO for subprocess.
 * However, the async callbacks fire on the GLib main loop.
 *
 * Ownership:
 *   - DC_ScadJob is opaque; created by dc_scad_run_*(), freed
 *     automatically when the callback fires or by dc_scad_job_cancel().
 *   - DC_ScadResult is passed to the callback and owned by the caller
 *     after the callback returns. Free with dc_scad_result_free().
 */

#include "core/error.h"
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Result of a completed OpenSCAD invocation
 * ---------------------------------------------------------------------- */
typedef struct {
    int      exit_code;     /* 0 = success */
    char    *stdout_text;   /* owned, may be empty */
    char    *stderr_text;   /* owned, error/warning messages */
    char    *output_path;   /* owned, path to output file (if any) */
    double   elapsed_secs;  /* wall-clock time */
} DC_ScadResult;

/* Free all owned strings in a result. Safe with NULL. */
void dc_scad_result_free(DC_ScadResult *result);

/* -------------------------------------------------------------------------
 * Callback type — called when an OpenSCAD job completes
 * ---------------------------------------------------------------------- */
typedef void (*DC_ScadJobCb)(DC_ScadResult *result, void *userdata);

/* -------------------------------------------------------------------------
 * Job handle (opaque)
 * ---------------------------------------------------------------------- */
typedef struct DC_ScadJob DC_ScadJob;

/* Cancel a running job. The callback will NOT be called.
 * Safe with NULL. */
void dc_scad_job_cancel(DC_ScadJob *job);

/* -------------------------------------------------------------------------
 * Launch operations
 *
 * All return a job handle (NULL on immediate failure).
 * The callback fires on the GLib main loop when the job completes.
 * ---------------------------------------------------------------------- */

/* Render a .scad file to PNG (preview mode).
 * png_path: output PNG path. width/height: image dimensions (0 = default 400x300).
 * Uses --preview --viewall --autocenter. */
DC_ScadJob *dc_scad_render_png(const char *scad_path,
                                const char *png_path,
                                int width, int height,
                                DC_ScadJobCb cb, void *userdata);

/* Export a .scad file to STL/OFF/AMF/3MF (determined by output extension). */
DC_ScadJob *dc_scad_run_export(const char *scad_path,
                                const char *output_path,
                                DC_ScadJobCb cb, void *userdata);

/* Open the .scad file in OpenSCAD's GUI (detached, no callback). */
int dc_scad_open_gui(const char *scad_path);

/* -------------------------------------------------------------------------
 * Synchronous convenience (blocks — use only in tests/CLI tools)
 * ---------------------------------------------------------------------- */

/* Run OpenSCAD and block until complete. Caller owns the result.
 * Returns NULL on launch failure. */
DC_ScadResult *dc_scad_run_sync(const char *scad_path,
                                 const char *output_path,
                                 const char *const *extra_args,
                                 int num_extra_args);

/* -------------------------------------------------------------------------
 * Configuration
 * ---------------------------------------------------------------------- */

/* Override the openscad binary path (default: "openscad" from PATH).
 * The string is copied. Pass NULL to reset to default. */
void dc_scad_set_binary(const char *path);

/* Get the current openscad binary path. Borrowed pointer. */
const char *dc_scad_get_binary(void);

#endif /* DC_SCAD_RUNNER_H */
