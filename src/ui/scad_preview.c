#define _POSIX_C_SOURCE 200809L
#include "ui/scad_preview.h"
#include "ui/code_editor.h"
#include "gl/gl_viewport.h"
#include "scad/scad_splitter.h"
#include "ui/transform_panel.h"
#include "voxel/voxelize_stl.h"
#include "voxel/voxel.h"
#include "core/log.h"
#include "cubeiform/cubeiform.h"
#include "cubeiform/cubeiform_eda.h"
#include "gl/gl_sdf_analytical.h"
#include "voxel/voxelize_gpu.h"
#include "inspect/inspect.h"

/* Trinity Site — native OpenSCAD interpreter (replaces OpenSCAD subprocess) */
#include "ts_eval.h"
#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_mesh.h"
#include "voxel/voxelize_bezier.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */
struct DC_ScadPreview {
    GtkWidget      *container;      /* GtkBox(V): toolbar + GL viewport */
    DC_GlViewport  *viewport;       /* real-time 3D viewport */
    GtkWidget      *status_label;
    GtkWidget      *progress_bar;   /* determinate progress during render */
    GtkWidget      *render_btn;
    GtkWidget      *reset_btn;
    GtkWidget      *ortho_btn;
    GtkWidget      *grid_btn;
    GtkWidget      *axes_btn;
    GtkWidget      *blocky_btn;
    GtkWidget      *density_combo;  /* editable combo: voxels per mm */
    GtkWidget      *log_view;       /* GtkTextView for log panel */
    GtkTextBuffer  *log_buffer;     /* log text buffer */
    DC_CodeEditor  *code_ed;        /* borrowed */
    int             rendering;
    guint           progress_id;    /* timer source for progress polling */

    /* Transform panel overlay */
    DC_TransformPanel *transform;

    /* Progressive render state */
    unsigned int    render_gen;     /* generation counter — incremented each render */
    volatile int    hq_cancel;      /* cooperative cancel flag for HQ task */
    int             hq_running;     /* is HQ task in flight? */
    ts_progress     progress;       /* shared progress — worker writes, UI reads */
    int             camera_fitted;  /* 1 after first fit — skip auto-fit on re-renders */
    int             render_pending; /* 1 = re-render queued (dropped while busy) */

    /* Voxel rendering — THE PURE PATH */
    int             voxel_resolution; /* cells per longest axis (default 64) */
    DC_VoxelGrid   *voxel_grid;      /* owned — last voxelized scene */

    /* Tricanvas: render mode and sibling */
    int             render_mode;     /* 0=all (legacy), 1=solid only, 2=mesh only */
    DC_ScadPreview *sibling;         /* the other preview to trigger on F5 */

    /* Analytical SDF scene (the Infinite Surface) */
    DC_GlSdfScene   sdf_scene;      /* built from Cubeiform VoxOps */
};

/* -------------------------------------------------------------------------
 * Progress bar — polls shared ts_progress from worker thread
 * ---------------------------------------------------------------------- */
static gboolean
poll_progress(gpointer data)
{
    DC_ScadPreview *pv = data;
    int done  = pv->progress.done;
    int total = pv->progress.total;

    if (total > 0) {
        double frac = (double)done / (double)total;
        if (frac > 1.0) frac = 1.0;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pv->progress_bar), frac);
    } else {
        /* Total not yet known — pulse */
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(pv->progress_bar));
    }
    return G_SOURCE_CONTINUE;
}

static void
__attribute__((unused))
progress_start(DC_ScadPreview *pv)
{
    pv->progress.done  = 0;
    pv->progress.total = 0;
    gtk_widget_set_visible(pv->progress_bar, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pv->progress_bar), 0.0);
    pv->progress_id = g_timeout_add(100, poll_progress, pv);
}

static void
progress_stop(DC_ScadPreview *pv)
{
    if (pv->progress_id) {
        g_source_remove(pv->progress_id);
        pv->progress_id = 0;
    }
    gtk_widget_set_visible(pv->progress_bar, FALSE);
}

/* -------------------------------------------------------------------------
 * Preamble detection — identifies non-geometry statements
 *
 * Preamble statements are prepended to each geometry statement when
 * rendering per-object. They include: include/use directives,
 * module/function definitions, and variable assignments.
 * ---------------------------------------------------------------------- */
static int
is_preamble(const char *stmt)
{
    const char *p = stmt;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return 1; /* empty = skip */

    /* Skip leading single-line comments so "// comment\nmodule foo()" is
     * correctly identified as a module definition, not geometry. */
    while (p[0] == '/' && p[1] == '/') {
        while (*p && *p != '\n') p++;
        while (*p && isspace((unsigned char)*p)) p++;
    }
    if (!*p) return 1; /* comment-only statement */

    /* include/use directives */
    if (strncmp(p, "include", 7) == 0 &&
        !isalnum((unsigned char)p[7]) && p[7] != '_')
        return 1;
    if (strncmp(p, "use", 3) == 0 &&
        !isalnum((unsigned char)p[3]) && p[3] != '_')
        return 1;

    /* module/function definitions */
    if (strncmp(p, "module", 6) == 0 &&
        !isalnum((unsigned char)p[6]) && p[6] != '_')
        return 1;
    if (strncmp(p, "function", 8) == 0 &&
        !isalnum((unsigned char)p[8]) && p[8] != '_')
        return 1;

    /* Variable assignment: identifier = expr; (but not ==) */
    const char *eq = strchr(p, '=');
    if (eq && eq > p && eq[1] != '=') {
        const char *q = p;
        while (q < eq && (isalnum((unsigned char)*q) || *q == '_' || *q == '$'))
            q++;
        while (q < eq && isspace((unsigned char)*q)) q++;
        if (q == eq) return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Multi-object progressive render pipeline
 *
 * Pass 1 (preview): $fn=12, force_quality — instant low-poly per object
 * Pass 2 (HQ):      $fn=100 — full quality per object, cancellable
 *
 * Each pass splits the SCAD source into top-level statements, identifies
 * preamble (variables, includes, module defs), and renders each geometry
 * statement separately with preamble prepended. Results are loaded as
 * separate GL objects for color-ID picking.
 * ---------------------------------------------------------------------- */

typedef struct {
    char *stl_path;     /* owned temp file path */
    int   line_start;   /* 1-based source line range */
    int   line_end;
} ObjSlot;

typedef struct {
    ObjSlot        *objs;
    int             obj_count;
    ts_parse_error  err;        /* first error encountered */
    double          elapsed;
    unsigned int    gen;
    int             is_hq;
    int             total_tris;
} RenderResult;

static void
render_result_free(RenderResult *res)
{
    if (!res) return;
    for (int i = 0; i < res->obj_count; i++)
        free(res->objs[i].stl_path);
    free(res->objs);
    free(res);
}

typedef struct {
    char           *source;         /* owned copy of SCAD source */
    unsigned int    gen;            /* generation counter */
    int             is_hq;          /* 0=preview, 1=high-quality */
    volatile int   *cancel;         /* points to pv->hq_cancel (HQ only) */
    ts_progress    *progress;       /* shared progress tracker */
} RenderTaskData;

static void
render_task_data_free(gpointer p)
{
    RenderTaskData *td = p;
    free(td->source);
    free(td);
}

/* Worker thread: split source, render each geometry statement, write STLs */
static void
render_thread_func(GTask *task, gpointer source_obj,
                   gpointer task_data, GCancellable *cancellable)
{
    (void)source_obj;
    (void)cancellable;
    RenderTaskData *td = task_data;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Split source into top-level statements */
    DC_ScadStatements *stmts = dc_scad_split(td->source);
    if (!stmts || stmts->count == 0) {
        RenderResult *res = calloc(1, sizeof(*res));
        res->gen = td->gen;
        res->is_hq = td->is_hq;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        res->elapsed = (double)(t1.tv_sec - t0.tv_sec)
                     + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
        g_task_return_pointer(task, res, NULL);
        dc_scad_stmts_free(stmts);
        return;
    }

    /* Build preamble: concatenate all preamble statements */
    size_t preamble_cap = 1;
    for (int i = 0; i < stmts->count; i++) {
        if (is_preamble(stmts->stmts[i].text))
            preamble_cap += strlen(stmts->stmts[i].text) + 1;
    }

    char *preamble = malloc(preamble_cap);
    preamble[0] = '\0';
    for (int i = 0; i < stmts->count; i++) {
        if (is_preamble(stmts->stmts[i].text)) {
            strcat(preamble, stmts->stmts[i].text);
            strcat(preamble, "\n");
        }
    }

    /* Count geometry statements */
    int geo_count = 0;
    for (int i = 0; i < stmts->count; i++) {
        if (!is_preamble(stmts->stmts[i].text))
            geo_count++;
    }

    if (geo_count == 0) {
        RenderResult *res = calloc(1, sizeof(*res));
        res->gen = td->gen;
        res->is_hq = td->is_hq;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        res->elapsed = (double)(t1.tv_sec - t0.tv_sec)
                     + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
        g_task_return_pointer(task, res, NULL);
        free(preamble);
        dc_scad_stmts_free(stmts);
        return;
    }

    /* Set up progress: one tick per geometry statement */
    if (td->progress) {
        td->progress->done = 0;
        td->progress->total = geo_count;
    }

    /* Allocate result */
    RenderResult *res = calloc(1, sizeof(*res));
    res->gen = td->gen;
    res->is_hq = td->is_hq;
    res->objs = calloc((size_t)geo_count, sizeof(ObjSlot));

    /* Set up interpret options */
    ts_interpret_opts opts = {0};
    if (td->is_hq) {
        opts.fn_override = 100;
        opts.fa_override = 1;
        opts.fs_override = 0.4;
        opts.force_quality = 0;
        opts.cancel = td->cancel;
    } else {
        opts.fn_override = 12;
        opts.fa_override = 12;
        opts.fs_override = 2.0;
        opts.force_quality = 1;
        opts.cancel = NULL;
    }

    int obj_idx = 0;
    for (int i = 0; i < stmts->count; i++) {
        if (is_preamble(stmts->stmts[i].text))
            continue;

        /* Check cancellation */
        if (td->cancel && *td->cancel)
            break;

        /* Build source: preamble + this statement */
        size_t src_len = strlen(preamble) + strlen(stmts->stmts[i].text) + 2;
        char *src = malloc(src_len);
        snprintf(src, src_len, "%s%s", preamble, stmts->stmts[i].text);

        ts_parse_error err = {0};
        opts.progress = NULL; /* track at statement level, not internal */
        ts_mesh mesh = ts_interpret_ex(src, &err, &opts);
        free(src);

        /* Store first error */
        if (err.msg[0] && res->err.msg[0] == '\0')
            res->err = err;

        if (mesh.tri_count > 0) {
            char path[128];
            snprintf(path, sizeof(path), "/tmp/duncad-obj-%02d-%s.stl",
                     obj_idx, td->is_hq ? "hq" : "lq");
            if (ts_mesh_write_stl(&mesh, path) == 0) {
                res->objs[obj_idx].stl_path = strdup(path);
                res->objs[obj_idx].line_start = stmts->stmts[i].line_start;
                res->objs[obj_idx].line_end = stmts->stmts[i].line_end;
                res->total_tris += mesh.tri_count;
                obj_idx++;
            }
        }
        ts_mesh_free(&mesh);

        if (td->progress)
            td->progress->done++;
    }
    res->obj_count = obj_idx;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    res->elapsed = (double)(t1.tv_sec - t0.tv_sec)
                 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;

    /* If cancelled, clean up */
    if (td->cancel && *td->cancel) {
        render_result_free(res);
        res = NULL;
    }

    free(preamble);
    dc_scad_stmts_free(stmts);

    g_task_return_pointer(task, res, NULL);
}

/* Forward declarations */
static void launch_hq_render(DC_ScadPreview *pv, const char *source);
static void do_render(DC_ScadPreview *pv);
static void log_append(DC_ScadPreview *pv, const char *msg);

/* Main thread callback for BOTH preview and HQ results */
static void
render_done_cb(GObject *source_obj, GAsyncResult *result, gpointer userdata)
{
    (void)source_obj;
    DC_ScadPreview *pv = userdata;
    RenderResult *res = g_task_propagate_pointer(G_TASK(result), NULL);

    /* NULL = cancelled */
    if (!res) {
        if (pv->hq_running)
            pv->hq_running = 0;
        return;
    }

    /* Stale generation — discard */
    if (res->gen != pv->render_gen) {
        render_result_free(res);
        return;
    }

    if (res->is_hq) {
        pv->hq_running = 0;
        progress_stop(pv);
    } else {
        pv->rendering = 0;
        gtk_widget_set_sensitive(pv->render_btn, TRUE);
    }

    /* Handle errors */
    if (res->err.msg[0]) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Parse error (line %d): %.200s",
                 res->err.line, res->err.msg);
        gtk_label_set_text(GTK_LABEL(pv->status_label), msg);
        if (!res->is_hq) progress_stop(pv);
        render_result_free(res);
        return;
    }

    if (res->obj_count == 0) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "No geometry produced");
        if (!res->is_hq) progress_stop(pv);
        render_result_free(res);
        return;
    }

    /* PURIFIED RENDERING PATH — voxelize all STL objects into SDF.
     * No triangles survive into the rendering pipeline. */
    dc_gl_viewport_clear_objects(pv->viewport);
    dc_gl_viewport_clear_mesh(pv->viewport);

    /* Merge all STL objects by voxelizing the first one.
     * TODO: union multiple objects via SDF composition. */
    int voxelized = 0;
    int vox_res = pv->voxel_resolution > 0 ? pv->voxel_resolution : 64;

    for (int i = 0; i < res->obj_count; i++) {
        if (!res->objs[i].stl_path) continue;

        DC_Error verr = {0};
        DC_VoxelGrid *grid = dc_voxelize_stl(res->objs[i].stl_path, vox_res, &verr);
        unlink(res->objs[i].stl_path);

        if (grid) {
            /* Free previous grid */
            dc_voxel_grid_free(pv->voxel_grid);
            pv->voxel_grid = grid;
            dc_gl_viewport_set_voxel_grid(pv->viewport, grid);
            voxelized++;
            DC_LOG_INFO_APP("voxelized object %d: %zu active voxels (res=%d)",
                             i, dc_voxel_grid_active_count(grid), vox_res);
        } else {
            DC_LOG_INFO_APP("voxelize failed for object %d: %s", i, verr.message);
        }

        break; /* First object only for now — TODO: SDF union of multiple */
    }

    if (voxelized > 0) {
        /* Fit camera to voxel bounds */
        if (!res->is_hq && !pv->camera_fitted) {
            DC_LOG_INFO_APP("fit camera to voxel scene (first render), gen=%u", pv->render_gen);
            /* Compute center from voxel grid bounds */
            float vmin[3] = {0}, vmax[3] = {0};
            dc_voxel_grid_bounds(pv->voxel_grid, &vmin[0], &vmin[1], &vmin[2],
                                                   &vmax[0], &vmax[1], &vmax[2]);
            float cx = (vmin[0]+vmax[0])*0.5f;
            float cy = (vmin[1]+vmax[1])*0.5f;
            float cz = (vmin[2]+vmax[2])*0.5f;
            float dx = vmax[0]-vmin[0], dy = vmax[1]-vmin[1], dz = vmax[2]-vmin[2];
            float diag = sqrtf(dx*dx + dy*dy + dz*dz);
            dc_gl_viewport_set_camera_center(pv->viewport, cx, cy, cz);
            dc_gl_viewport_set_camera_dist(pv->viewport, diag * 1.5f);
            pv->camera_fitted = 1;
        }

        size_t active = pv->voxel_grid ?
            dc_voxel_grid_active_count(pv->voxel_grid) : 0;
        char status[192];
        if (res->is_hq) {
            snprintf(status, sizeof(status),
                     "Voxelized: %zu voxels (res=%d) in %.3fs",
                     active, vox_res, res->elapsed);
        } else {
            snprintf(status, sizeof(status),
                     "Voxelized: %zu voxels (res=%d) — refining...",
                     active, vox_res);
        }
        gtk_label_set_text(GTK_LABEL(pv->status_label), status);
    } else {
        gtk_label_set_text(GTK_LABEL(pv->status_label),
                           "No geometry voxelized");
    }

    /* If preview just completed, check for queued re-render or launch HQ */
    if (!res->is_hq) {
        if (pv->render_pending) {
            /* A render was requested while we were busy — re-render with
             * fresh code (user may have moved objects since we started) */
            pv->render_pending = 0;
            render_result_free(res);
            dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
                   "executing queued re-render");
            do_render(pv);
            return;
        }
        if (res->obj_count > 0) {
            RenderTaskData *td = g_task_get_task_data(G_TASK(result));
            if (td && td->source) {
                launch_hq_render(pv, td->source);
            }
        }
    }

    render_result_free(res);
}

static void
launch_hq_render(DC_ScadPreview *pv, const char *source)
{
    /* Cancel any existing HQ render.
     * Don't reset immediately — old thread needs time to see the flag.
     * Reset only after setting up the new task, right before spawning it.
     * Old HQ results are discarded by generation counter anyway. */
    pv->hq_cancel = 1;

    /* Reset progress for HQ pass */
    pv->progress.done  = 0;
    pv->progress.total = 0;

    RenderTaskData *td = calloc(1, sizeof(*td));
    td->source   = strdup(source);
    td->gen      = pv->render_gen;
    td->is_hq    = 1;
    td->cancel   = &pv->hq_cancel;
    td->progress = &pv->progress;

    pv->hq_running = 1;

    /* Reset cancel flag just before launch — old HQ had time to see =1 */
    pv->hq_cancel = 0;

    GTask *task = g_task_new(NULL, NULL, render_done_cb, pv);
    g_task_set_task_data(task, td, render_task_data_free);
    g_task_run_in_thread(task, render_thread_func);
    g_object_unref(task);

    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "HQ render launched (gen %u)",
           pv->render_gen);
}

/* ---- Convert buttons: wrap code for cross-editor transfer ---- */

static void on_to_mesh_clicked(GtkButton *b, gpointer d)
{
    (void)b;
    DC_ScadPreview *pv = d;
    if (!pv->code_ed) return;

    char *text = dc_code_editor_get_text(pv->code_ed);
    if (!text || !*text) { free(text); return; }

    /* Strip $vd line if present — mesh editor doesn't need it */
    char *clean = text;
    char *vd = strstr(text, "$vd");
    if (vd) {
        char *ls = vd;
        while (ls > text && *(ls-1) != '\n') ls--;
        char *le = strchr(vd, '\n');
        if (!le) le = vd + strlen(vd); else le++;
        size_t before = (size_t)(ls - text);
        size_t after = strlen(le);
        clean = malloc(before + after + 1);
        memcpy(clean, text, before);
        memcpy(clean + before, le, after + 1);
        free(text);
    }

    /* Wrap in bezier_mesh{} */
    size_t clen = strlen(clean);
    char *wrapped = malloc(clen + 32);
    snprintf(wrapped, clen + 32, "bezier_mesh{ %s }", clean);
    if (clean != text) free(clean);

    dc_code_editor_set_text(pv->code_ed, wrapped);
    free(wrapped);

    /* Refit both canvases */
    pv->camera_fitted = 0;
    if (pv->sibling) pv->sibling->camera_fitted = 0;
    dc_scad_preview_render_refit(pv);
}

static void on_to_solid_clicked(GtkButton *b, gpointer d)
{
    (void)b;
    DC_ScadPreview *pv = d;
    if (!pv->code_ed) return;

    char *text = dc_code_editor_get_text(pv->code_ed);
    if (!text || !*text) { free(text); return; }

    /* Strip $vd line before wrapping — it will be re-prepended by do_render */
    char *clean = text;
    char *vd = strstr(text, "$vd");
    if (vd) {
        char *ls = vd;
        while (ls > text && *(ls-1) != '\n') ls--;
        char *le = strchr(vd, '\n');
        if (!le) le = vd + strlen(vd); else le++;
        /* Skip blank lines after $vd */
        while (*le == '\n') le++;
        size_t before = (size_t)(ls - text);
        size_t after = strlen(le);
        clean = malloc(before + after + 1);
        memcpy(clean, text, before);
        memcpy(clean + before, le, after + 1);
        free(text);
    }

    /* Wrap in to_solid */
    size_t clen = strlen(clean);
    char *wrapped;
    if (strstr(clean, "bezier_mesh")) {
        wrapped = malloc(clen + 32);
        snprintf(wrapped, clen + 32, "to_solid(%s);", clean);
    } else {
        wrapped = malloc(clen + 48);
        snprintf(wrapped, clen + 48, "to_solid(bezier_mesh{ %s });", clean);
    }
    if (clean != text) free(clean);

    dc_code_editor_set_text(pv->code_ed, wrapped);
    free(wrapped);

    /* Refit BOTH canvases — the solid canvas needs to fit the new voxels */
    pv->camera_fitted = 0;
    if (pv->sibling) pv->sibling->camera_fitted = 0;
    dc_scad_preview_render_refit(pv);
}


static void
do_render(DC_ScadPreview *pv)
{
    if (!pv->code_ed) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "No code editor connected");
        return;
    }
    if (pv->rendering) {
        pv->render_pending = 1;
        dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP,
               "render queued (busy), will re-render on completion");
        return;
    }

    char *text = dc_code_editor_get_text(pv->code_ed);
    if (!text || !*text) {
        gtk_label_set_text(GTK_LABEL(pv->status_label), "Nothing to render");
        free(text);
        return;
    }

    /* =====================================================================
     * ALL IS SDF. ALL IS RENDERED NATIVELY.
     * No triangles. No STL. No mesh. Just math.
     * ===================================================================== */
    {
        /* SDF RENDERING — the only path */
        gtk_label_set_text(GTK_LABEL(pv->status_label), "Rendering SDF...");

        /* If source already has $vd, use it as-is.
         * Otherwise prepend the UI density setting. */
        char *full_src = text;
        if (!strstr(text, "$vd")) {
            int ui_vd = pv->voxel_resolution > 0 ? pv->voxel_resolution : 3;
            size_t tlen = strlen(text);
            full_src = malloc(tlen + 32);
            if (full_src) {
                int hlen = snprintf(full_src, 32, "$vd = %d;\n", ui_vd);
                memcpy(full_src + hlen, text, tlen + 1);
                free(text);
            } else {
                full_src = text;
            }
        }

        DC_Error err = {0};
        DC_VoxelGrid *grid = NULL;
        void *bmesh = NULL;
        dc_cubeiform_execute_full(full_src, NULL, NULL, &grid, &bmesh, NULL, &err);
        free(full_src);

        int got_something = 0;

        /* --- MESH MODE: accept bezier mesh, ignore voxels --- */
        if (bmesh && pv->render_mode != DC_RENDER_SOLID) {
            ts_bezier_mesh *m = (ts_bezier_mesh *)bmesh;
            dc_gl_viewport_clear_objects(pv->viewport);
            dc_gl_viewport_clear_mesh(pv->viewport);
            dc_gl_viewport_set_bezier_mesh(pv->viewport, m);
            dc_gl_viewport_set_bezier_view(pv->viewport, DC_BEZIER_VIEW_WIREFRAME);

            /* Sync mesh to inspect so 2D editor can access it */
            dc_inspect_set_bezier_mesh(m);

            /* Fit camera to mesh CP bounding box */
            if (!pv->camera_fitted && m->cps && m->cp_rows > 0 && m->cp_cols > 0) {
                int total = m->cp_rows * m->cp_cols;
                float bmin[3] = {1e18f, 1e18f, 1e18f};
                float bmax[3] = {-1e18f, -1e18f, -1e18f};
                for (int j = 0; j < total; j++) {
                    for (int a = 0; a < 3; a++) {
                        float v = (float)m->cps[j].v[a];
                        if (v < bmin[a]) bmin[a] = v;
                        if (v > bmax[a]) bmax[a] = v;
                    }
                }
                float cx = (bmin[0]+bmax[0])*0.5f;
                float cy = (bmin[1]+bmax[1])*0.5f;
                float cz = (bmin[2]+bmax[2])*0.5f;
                float dx = bmax[0]-bmin[0], dy = bmax[1]-bmin[1], dz = bmax[2]-bmin[2];
                float diag = sqrtf(dx*dx + dy*dy + dz*dz);
                dc_gl_viewport_set_camera_center(pv->viewport, cx, cy, cz);
                dc_gl_viewport_set_camera_dist(pv->viewport, diag * 1.5f);
                pv->camera_fitted = 1;
            }

            char status[192];
            snprintf(status, sizeof(status),
                     "Bezier mesh: %dx%d patches (%dx%d CPs)",
                     m->rows, m->cols, m->cp_rows, m->cp_cols);
            gtk_label_set_text(GTK_LABEL(pv->status_label), status);
            log_append(pv, status);
            got_something = 1;

            ts_bezier_mesh_free(m);
            free(m);
        } else if (bmesh && !grid) {
            /* Mesh without grid (no to_solid) and wrong render mode — discard */
            ts_bezier_mesh *m = (ts_bezier_mesh *)bmesh;
            ts_bezier_mesh_free(m);
            free(m);
            bmesh = NULL;
        }

        /* --- Build analytical SDF scene from Cubeiform for infinite surface view --- */
        if (pv->render_mode != DC_RENDER_MESH) {
            DC_Error parse_err = {0};
            DC_CubeiformEda *eda = dc_cubeiform_parse_eda(
                dc_code_editor_get_text(pv->code_ed), &parse_err);
            if (eda) {
                dc_gl_sdf_scene_clear(&pv->sdf_scene);
                size_t nops = dc_cubeiform_eda_vox_op_count(eda);
                float cr = 0.7f, cg = 0.7f, cb = 0.7f; /* default grey */
                int pending_csg = DC_SDF_UNION; /* CSG op for next primitive */
                for (size_t oi = 0; oi < nops; oi++) {
                    const DC_VoxOp *op = dc_cubeiform_eda_get_vox_op(eda, oi);
                    if (!op) continue;
                    /* Track current color from COLOR ops */
                    if (op->type == DC_VOX_OP_COLOR) {
                        cr = op->r/255.0f; cg = op->g/255.0f; cb = op->b/255.0f;
                        continue;
                    }
                    /* CSG ops set pending state for the next primitive */
                    if (op->type == DC_VOX_OP_SUBTRACT)  { pending_csg = DC_SDF_SUBTRACT;  continue; }
                    if (op->type == DC_VOX_OP_INTERSECT) { pending_csg = DC_SDF_INTERSECT; continue; }
                    if (op->type == DC_VOX_OP_UNION)     { pending_csg = DC_SDF_UNION;     continue; }
                    /* Skip group markers */
                    if (op->type == DC_VOX_OP_GROUP_BEGIN || op->type == DC_VOX_OP_GROUP_END) continue;
                    /* Use op color if set, otherwise use current color */
                    float pr = (op->r || op->g || op->b) ? op->r/255.0f : cr;
                    float pg = (op->r || op->g || op->b) ? op->g/255.0f : cg;
                    float pb = (op->r || op->g || op->b) ? op->b/255.0f : cb;
                    switch (op->type) {
                    case DC_VOX_OP_SPHERE:
                        dc_gl_sdf_scene_add_sphere(&pv->sdf_scene,
                            (float)op->x, (float)op->y, (float)op->z,
                            (float)op->radius, pr, pg, pb);
                        break;
                    case DC_VOX_OP_BOX:
                        dc_gl_sdf_scene_add_box(&pv->sdf_scene,
                            (float)(op->x + op->x2) * 0.5f,
                            (float)(op->y + op->y2) * 0.5f,
                            (float)(op->z + op->z2) * 0.5f,
                            (float)(op->x2 - op->x) * 0.5f,
                            (float)(op->y2 - op->y) * 0.5f,
                            (float)(op->z2 - op->z) * 0.5f,
                            pr, pg, pb);
                        break;
                    case DC_VOX_OP_CYLINDER:
                        dc_gl_sdf_scene_add_cylinder(&pv->sdf_scene,
                            (float)op->x, (float)(op->z + op->radius2) * 0.5f, (float)op->y,
                            (float)op->radius, (float)(op->radius2 - op->z),
                            pr, pg, pb);
                        break;
                    case DC_VOX_OP_TORUS:
                        dc_gl_sdf_scene_add_torus(&pv->sdf_scene,
                            (float)op->x, (float)op->y, (float)op->z,
                            (float)op->radius, (float)op->radius2,
                            pr, pg, pb);
                        break;
                    default: break;
                    }
                    /* Apply pending CSG to the just-added primitive */
                    if (pending_csg != DC_SDF_UNION) {
                        dc_gl_sdf_scene_set_csg(&pv->sdf_scene, pending_csg);
                        pending_csg = DC_SDF_UNION;
                    }
                }
                dc_gl_sdf_scene_compute_bbox(&pv->sdf_scene);
                dc_gl_viewport_set_sdf_scene(pv->viewport, &pv->sdf_scene);
                dc_cubeiform_eda_free(eda);
            }
        }

        /* --- SOLID MODE: accept voxel grid, ignore bezier mesh --- */
        if (grid && pv->render_mode != DC_RENDER_MESH) {
            dc_gl_viewport_clear_objects(pv->viewport);
            dc_gl_viewport_clear_mesh(pv->viewport);
            dc_voxel_grid_free(pv->voxel_grid);
            pv->voxel_grid = grid;
            dc_gl_viewport_set_voxel_grid(pv->viewport, grid);

            /* If source bezier mesh is available, set up direct
             * surface raytracing for smooth mode. */
            if (bmesh) {
                dc_gl_viewport_set_bezier_ray_mesh(pv->viewport, bmesh);
                ts_bezier_mesh *bm = (ts_bezier_mesh *)bmesh;
                ts_bezier_mesh_free(bm);
                free(bm);
                bmesh = NULL;
            }

            size_t active = dc_voxel_grid_active_count(grid);
            int gx = dc_voxel_grid_size_x(grid);
            int gy = dc_voxel_grid_size_y(grid);
            int gz = dc_voxel_grid_size_z(grid);

            /* Fit camera */
            if (!pv->camera_fitted) {
                float vmin[3] = {0}, vmax[3] = {0};
                dc_voxel_grid_bounds(grid, &vmin[0], &vmin[1], &vmin[2],
                                           &vmax[0], &vmax[1], &vmax[2]);
                float cx = (vmin[0]+vmax[0])*0.5f;
                float cy = (vmin[1]+vmax[1])*0.5f;
                float cz = (vmin[2]+vmax[2])*0.5f;
                float dx = vmax[0]-vmin[0], dy = vmax[1]-vmin[1], dz = vmax[2]-vmin[2];
                float diag = sqrtf(dx*dx + dy*dy + dz*dz);
                dc_gl_viewport_set_camera_center(pv->viewport, cx, cy, cz);
                dc_gl_viewport_set_camera_dist(pv->viewport, diag * 1.5f);
                pv->camera_fitted = 1;
            }

            char status[192];
            snprintf(status, sizeof(status),
                     "Rendered: %zu active (%dx%dx%d)",
                     active, gx, gy, gz);
            gtk_label_set_text(GTK_LABEL(pv->status_label), status);
            log_append(pv, status);
            got_something = 1;

            dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
                   "SDF RENDER: %zu active, grid %dx%dx%d", active, gx, gy, gz);
        } else if (grid) {
            /* Mesh mode — discard voxel output */
            dc_voxel_grid_free(grid);
        }

        if (!got_something) {
            /* Clear viewport when there's nothing for this canvas */
            if (pv->render_mode == DC_RENDER_SOLID) {
                dc_gl_viewport_clear_objects(pv->viewport);
                dc_gl_viewport_clear_mesh(pv->viewport);
                dc_gl_viewport_set_voxel_grid(pv->viewport, NULL);
                dc_voxel_grid_free(pv->voxel_grid);
                pv->voxel_grid = NULL;
            } else if (pv->render_mode == DC_RENDER_MESH) {
                dc_gl_viewport_set_bezier_mesh(pv->viewport, NULL);
            }
            const char *msg = err.message[0]
                ? err.message
                : (pv->render_mode == DC_RENDER_MESH
                   ? "No bezier_mesh{} geometry"
                   : pv->render_mode == DC_RENDER_SOLID
                   ? "No solid geometry"
                   : "No renderable geometry");
            gtk_label_set_text(GTK_LABEL(pv->status_label), msg);
        }

        /* Trigger sibling render (the other canvas) */
        if (pv->sibling) {
            DC_ScadPreview *sib = pv->sibling;
            pv->sibling = NULL; /* prevent infinite recursion */
            dc_scad_preview_render(sib);
            pv->sibling = sib;
        }
    }
}

/* -------------------------------------------------------------------------
 * Button callbacks
 * ---------------------------------------------------------------------- */
static void on_render_clicked(GtkButton *b, gpointer d)
{
    (void)b;
    DC_ScadPreview *pv = d;
    pv->camera_fitted = 0; /* explicit render → refit camera */
    do_render(pv);
}

static void on_reset_clicked(GtkButton *b, gpointer d)
{
    (void)b;
    DC_ScadPreview *pv = d;
    dc_gl_viewport_fit_all_objects(pv->viewport);
    pv->camera_fitted = 1; /* still fitted, but user explicitly asked */
}

static void on_ortho_clicked(GtkButton *b, gpointer d)
{ (void)b; dc_gl_viewport_toggle_ortho(((DC_ScadPreview*)d)->viewport); }

static void on_grid_clicked(GtkButton *b, gpointer d)
{ (void)b; dc_gl_viewport_toggle_grid(((DC_ScadPreview*)d)->viewport); }

static void on_axes_clicked(GtkButton *b, gpointer d)
{ (void)b; dc_gl_viewport_toggle_axes(((DC_ScadPreview*)d)->viewport); }

/* ---- Log panel helper ---- */
static void
log_append(DC_ScadPreview *pv, const char *msg)
{
    if (!pv->log_buffer) return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(pv->log_buffer, &end);
    gtk_text_buffer_insert(pv->log_buffer, &end, msg, -1);
    gtk_text_buffer_insert(pv->log_buffer, &end, "\n", 1);
    /* Auto-scroll to bottom */
    gtk_text_buffer_get_end_iter(pv->log_buffer, &end);
    GtkTextMark *mark = gtk_text_buffer_get_mark(pv->log_buffer, "end");
    if (!mark)
        mark = gtk_text_buffer_create_mark(pv->log_buffer, "end", &end, FALSE);
    else
        gtk_text_buffer_move_mark(pv->log_buffer, mark, &end);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(pv->log_view), mark);
}

/* ---- Inject $vd = N; at top of code editor ---- */
static void
inject_vd_param(DC_ScadPreview *pv, int vd)
{
    if (!pv->code_ed) return;
    char *text = dc_code_editor_get_text(pv->code_ed);
    if (!text) return;

    /* Remove existing $vd line if present */
    char *clean = text;
    char *vd_line = strstr(text, "$vd");
    if (vd_line) {
        /* Find start of line */
        char *ls = vd_line;
        while (ls > text && *(ls-1) != '\n') ls--;
        /* Find end of line */
        char *le = strchr(vd_line, '\n');
        if (!le) le = vd_line + strlen(vd_line);
        else le++; /* include newline */
        /* Build new text without the $vd line */
        size_t before = (size_t)(ls - text);
        size_t after = strlen(le);
        clean = malloc(before + after + 1);
        memcpy(clean, text, before);
        memcpy(clean + before, le, after + 1);
        free(text);
    }

    /* Prepend $vd = N; with a blank line separating it from the code */
    size_t clen = strlen(clean);
    char *full = malloc(clen + 32);
    int hlen = snprintf(full, 32, "$vd = %d;\n\n", vd);
    memcpy(full + hlen, clean, clen + 1);
    dc_code_editor_set_text(pv->code_ed, full);
    free(full);
    if (clean != text) free(clean);
}

static void on_blocky_clicked(GtkButton *b, gpointer d)
{
    (void)b;
    DC_ScadPreview *pv = d;

    if (pv->render_mode == DC_RENDER_MESH) {
        /* Mesh canvas: cycle wireframe → solid → both → wireframe */
        DC_BezierViewMode cur = dc_gl_viewport_get_bezier_view(pv->viewport);
        DC_BezierViewMode next;
        const char *label;
        switch (cur) {
        case DC_BEZIER_VIEW_WIREFRAME: next = DC_BEZIER_VIEW_VOXEL;  label = "Solid";     break;
        case DC_BEZIER_VIEW_VOXEL:     next = DC_BEZIER_VIEW_BOTH;   label = "Both";      break;
        default:                       next = DC_BEZIER_VIEW_WIREFRAME; label = "Wireframe"; break;
        }
        dc_gl_viewport_set_bezier_view(pv->viewport, next);
        gtk_button_set_label(GTK_BUTTON(pv->blocky_btn), label);
    } else {
        /* Solid canvas: cycle Blocky → Smooth → Surface → Blocky */
        int blocky = dc_gl_viewport_get_voxel_blocky(pv->viewport);
        int analytical = dc_gl_viewport_get_analytical(pv->viewport);
        if (blocky) {
            /* Blocky → Smooth */
            dc_gl_viewport_set_voxel_blocky(pv->viewport, 0);
            dc_gl_viewport_set_analytical(pv->viewport, 0);
            gtk_button_set_label(GTK_BUTTON(pv->blocky_btn), "Smooth");
        } else if (!analytical) {
            /* Smooth → Surface (analytical) */
            dc_gl_viewport_set_analytical(pv->viewport, 1);
            gtk_button_set_label(GTK_BUTTON(pv->blocky_btn), "Surface");
        } else {
            /* Surface → Blocky */
            dc_gl_viewport_set_analytical(pv->viewport, 0);
            dc_gl_viewport_set_voxel_blocky(pv->viewport, 1);
            gtk_button_set_label(GTK_BUTTON(pv->blocky_btn), "Blocky");
        }
    }
}

/* Apply density from the entry widget */
static void apply_density(DC_ScadPreview *pv)
{
    if (!pv->code_ed || !pv->density_combo) return;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(pv->density_combo));
    if (!text || !*text) return;
    int val = atoi(text);
    if (val < 1) val = 1;
    if (val > 100) val = 100;

    if (val == pv->voxel_resolution) return; /* no change */

    pv->voxel_resolution = val;
    inject_vd_param(pv, val);

    char msg[64];
    snprintf(msg, sizeof(msg), "$vd = %d (voxels/mm)", val);
    log_append(pv, msg);

    dc_scad_preview_render(pv);
}

/* Density entry: user types a number, hits Enter. */
static void on_density_changed(GtkEntry *entry, gpointer d)
{
    (void)entry;
    apply_density((DC_ScadPreview *)d);
}

/* Focus left density entry — apply the value */
static void on_density_focus_leave(GtkEventControllerFocus *ctrl, gpointer d)
{
    (void)ctrl;
    apply_density((DC_ScadPreview *)d);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

DC_ScadPreview *
dc_scad_preview_new(void)
{
    DC_ScadPreview *pv = calloc(1, sizeof(*pv));
    if (!pv) return NULL;
    pv->voxel_resolution = 3; /* default $vd = 3 voxels/mm */

    /* GL viewport */
    pv->viewport = dc_gl_viewport_new();
    if (!pv->viewport) {
        free(pv);
        return NULL;
    }

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);
    gtk_widget_set_margin_top(toolbar, 2);
    gtk_widget_set_margin_bottom(toolbar, 2);

    pv->render_btn = gtk_button_new_with_label("Render");
    gtk_widget_set_focusable(pv->render_btn, FALSE);
    g_signal_connect(pv->render_btn, "clicked", G_CALLBACK(on_render_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->render_btn);

    pv->reset_btn = gtk_button_new_with_label("Reset");
    gtk_widget_set_focusable(pv->reset_btn, FALSE);
    g_signal_connect(pv->reset_btn, "clicked", G_CALLBACK(on_reset_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->reset_btn);

    pv->ortho_btn = gtk_button_new_with_label("Ortho");
    gtk_widget_set_focusable(pv->ortho_btn, FALSE);
    g_signal_connect(pv->ortho_btn, "clicked", G_CALLBACK(on_ortho_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->ortho_btn);

    pv->grid_btn = gtk_button_new_with_label("Grid");
    gtk_widget_set_focusable(pv->grid_btn, FALSE);
    g_signal_connect(pv->grid_btn, "clicked", G_CALLBACK(on_grid_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->grid_btn);

    pv->axes_btn = gtk_button_new_with_label("Axes");
    gtk_widget_set_focusable(pv->axes_btn, FALSE);
    g_signal_connect(pv->axes_btn, "clicked", G_CALLBACK(on_axes_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->axes_btn);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(toolbar), sep);

    /* Blocky/Smooth toggle */
    pv->blocky_btn = gtk_button_new_with_label("Blocky");
    gtk_widget_set_focusable(pv->blocky_btn, FALSE);
    g_signal_connect(pv->blocky_btn, "clicked", G_CALLBACK(on_blocky_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), pv->blocky_btn);

    /* Voxel density — editable entry. Type a number, hit Enter.
     * Values are voxels per mm ($vd parameter). */
    pv->density_combo = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(pv->density_combo), "$vd");
    gtk_editable_set_text(GTK_EDITABLE(pv->density_combo), "3");
    gtk_widget_set_size_request(pv->density_combo, 45, -1);
    gtk_widget_set_tooltip_text(pv->density_combo, "Voxel density — voxels/mm (Enter or click away to apply)");
    g_signal_connect(pv->density_combo, "activate", G_CALLBACK(on_density_changed), pv);
    /* Also apply when clicking away from the entry */
    {
        GtkEventController *focus = gtk_event_controller_focus_new();
        g_signal_connect(focus, "leave", G_CALLBACK(on_density_focus_leave), pv);
        gtk_widget_add_controller(pv->density_combo, focus);
    }
    gtk_box_append(GTK_BOX(toolbar), pv->density_combo);

    GtkWidget *vd_label = gtk_label_new("v/mm");
    gtk_widget_set_opacity(vd_label, 0.6);
    gtk_box_append(GTK_BOX(toolbar), vd_label);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(toolbar), sep2);

    /* Convert buttons — wraps code in bezier_mesh{} or to_solid() */
    GtkWidget *to_mesh_btn = gtk_button_new_with_label("→ Mesh");
    gtk_widget_set_focusable(to_mesh_btn, FALSE);
    gtk_widget_set_tooltip_text(to_mesh_btn, "Wrap in bezier_mesh{} and send to mesh editor");
    g_signal_connect(to_mesh_btn, "clicked", G_CALLBACK(on_to_mesh_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), to_mesh_btn);

    GtkWidget *to_solid_btn = gtk_button_new_with_label("→ Solid");
    gtk_widget_set_focusable(to_solid_btn, FALSE);
    gtk_widget_set_tooltip_text(to_solid_btn, "Wrap in to_solid(bezier_mesh{}) and send to solid editor");
    g_signal_connect(to_solid_btn, "clicked", G_CALLBACK(on_to_solid_clicked), pv);
    gtk_box_append(GTK_BOX(toolbar), to_solid_btn);

    GtkWidget *sep3 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(toolbar), sep3);

    pv->status_label = gtk_label_new("Ready — click Render");
    gtk_label_set_xalign(GTK_LABEL(pv->status_label), 0.0f);
    gtk_widget_set_hexpand(pv->status_label, TRUE);
    gtk_widget_set_opacity(pv->status_label, 0.7);
    gtk_box_append(GTK_BOX(toolbar), pv->status_label);

    /* Progress bar (hidden until render starts) */
    pv->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_size_request(pv->progress_bar, 120, -1);
    gtk_widget_set_valign(pv->progress_bar, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(pv->progress_bar, FALSE);
    gtk_box_append(GTK_BOX(toolbar), pv->progress_bar);

    /* Transform panel (overlay on viewport) */
    pv->transform = dc_transform_panel_new();

    /* Overlay: GL viewport as main child, transform panel floats on top */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), dc_gl_viewport_widget(pv->viewport));
    if (pv->transform) {
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                                dc_transform_panel_widget(pv->transform));
    }
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_widget_set_hexpand(overlay, TRUE);

    /* Log panel — scrollable text view below viewport */
    pv->log_buffer = gtk_text_buffer_new(NULL);
    pv->log_view = gtk_text_view_new_with_buffer(pv->log_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(pv->log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(pv->log_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(pv->log_view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_opacity(pv->log_view, 0.85);
    gtk_widget_add_css_class(pv->log_view, "monospace");

    GtkWidget *log_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll), pv->log_view);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(log_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(log_scroll, -1, 80);

    /* Paned: viewport on top, log on bottom */
    GtkWidget *vpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_start_child(GTK_PANED(vpaned), overlay);
    gtk_paned_set_end_child(GTK_PANED(vpaned), log_scroll);
    gtk_paned_set_resize_start_child(GTK_PANED(vpaned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(vpaned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(vpaned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(vpaned), TRUE);

    /* Container */
    pv->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(pv->container), toolbar);
    gtk_box_append(GTK_BOX(pv->container), vpaned);
    gtk_widget_set_vexpand(vpaned, TRUE);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "scad preview created");
    log_append(pv, "DunCAD ready.");
    return pv;
}

void
dc_scad_preview_free(DC_ScadPreview *pv)
{
    if (!pv) return;
    pv->hq_cancel = 1;  /* signal any in-flight HQ to stop */
    progress_stop(pv);
    dc_voxel_grid_free(pv->voxel_grid);
    dc_transform_panel_free(pv->transform);
    dc_gl_viewport_free(pv->viewport);
    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "scad preview freed");
    free(pv);
}

GtkWidget *
dc_scad_preview_widget(DC_ScadPreview *pv)
{
    return pv ? pv->container : NULL;
}

void
dc_scad_preview_set_code_editor(DC_ScadPreview *pv, DC_CodeEditor *ed)
{
    if (!pv) return;
    pv->code_ed = ed;
    dc_transform_panel_set_code_editor(pv->transform, ed);
}

void
dc_scad_preview_render(DC_ScadPreview *pv)
{
    if (pv) do_render(pv);
}

void
dc_scad_preview_render_refit(DC_ScadPreview *pv)
{
    if (!pv) return;
    pv->camera_fitted = 0;
    do_render(pv);
}

DC_GlViewport *
dc_scad_preview_get_viewport(DC_ScadPreview *pv)
{
    return pv ? pv->viewport : NULL;
}

DC_TransformPanel *
dc_scad_preview_get_transform(DC_ScadPreview *pv)
{
    return pv ? pv->transform : NULL;
}

const char *
dc_scad_preview_get_status(DC_ScadPreview *pv)
{
    if (!pv || !pv->status_label) return "";
    return gtk_label_get_text(GTK_LABEL(pv->status_label));
}

int
dc_scad_preview_is_rendering(DC_ScadPreview *pv)
{
    if (!pv) return 0;
    return pv->rendering || pv->hq_running;
}

void
dc_scad_preview_set_voxel_resolution(DC_ScadPreview *pv, int resolution)
{
    if (!pv) return;
    if (resolution < 8) resolution = 8;
    if (resolution > 4096) resolution = 4096;
    pv->voxel_resolution = resolution;
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "voxel resolution set to %d", resolution);
}

int
dc_scad_preview_get_voxel_resolution(DC_ScadPreview *pv)
{
    return pv ? pv->voxel_resolution : 64;
}

void
dc_scad_preview_set_render_mode(DC_ScadPreview *pv, int mode)
{
    if (pv) pv->render_mode = mode;
}

void
dc_scad_preview_set_sibling(DC_ScadPreview *pv, DC_ScadPreview *sibling)
{
    if (pv) pv->sibling = sibling;
}
