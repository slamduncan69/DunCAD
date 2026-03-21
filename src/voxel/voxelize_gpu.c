/*
 * voxelize_gpu.c — Async coarse-to-fine bezier mesh voxelization.
 *
 * Progressive rendering: starts at low resolution (instant), then
 * refines to target resolution in the background. Each refinement
 * pass posts the result to the main thread so the user sees the
 * shape immediately and watches it sharpen.
 *
 * GPU path (OpenCL): TODO — will accelerate the SDF evaluation.
 * CPU path: background GLib thread, works everywhere.
 */

#include "voxel/voxelize_gpu.h"
#include "voxel/voxelize_bezier.h"
#include "core/log.h"

#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_mesh.h"

#include <glib.h>
#include <stdlib.h>

/* =========================================================================
 * GPU availability check
 * ========================================================================= */

static int s_gpu_checked = 0;
static int s_gpu_available = 0;

int
dc_voxelize_gpu_available(void)
{
    if (!s_gpu_checked) {
        /* TODO: check for OpenCL devices */
        s_gpu_available = 0;
        s_gpu_checked = 1;
    }
    return s_gpu_available;
}

/* =========================================================================
 * Coarse-to-fine background thread
 * ========================================================================= */

typedef struct {
    void           *mesh_ptr;     /* ts_bezier_mesh* — OWNED */
    int             target_res;
    DC_VoxelizeDoneCb done_cb;
    void           *userdata;
    volatile int    cancelled;
    DC_VoxelGrid   *grid;         /* latest result */
    int             final;        /* 1 = this is the last pass */
} VoxWorker;

static VoxWorker *s_active_worker = NULL;

/* Posted to main thread after each resolution pass */
typedef struct {
    VoxWorker    *worker;
    DC_VoxelGrid *grid;       /* owned — transferred to callback */
    int           final;      /* 1 = last pass, free worker */
    int           pass_res;   /* resolution of this pass */
} PassResult;

static gboolean
on_pass_done(gpointer data)
{
    PassResult *pr = data;
    VoxWorker *w = pr->worker;

    if (!w->cancelled && pr->grid && w->done_cb) {
        w->done_cb(pr->grid, w->userdata);
        pr->grid = NULL; /* ownership transferred */
    }

    /* Free unclaimed grid */
    if (pr->grid) dc_voxel_grid_free(pr->grid);

    if (pr->final) {
        /* Last pass — clean up worker */
        if (w->mesh_ptr) {
            ts_bezier_mesh *m = (ts_bezier_mesh *)w->mesh_ptr;
            ts_bezier_mesh_free(m);
            free(m);
        }
        if (s_active_worker == w) s_active_worker = NULL;
        free(w);
    }

    free(pr);
    return G_SOURCE_REMOVE;
}

static gpointer
voxelize_thread(gpointer data)
{
    VoxWorker *w = data;

    /* Coarse-to-fine resolution passes.
     * Start at 16, double until target resolution. */
    int passes[8];
    int n_passes = 0;
    int res = 16;
    while (res < w->target_res && n_passes < 7) {
        passes[n_passes++] = res;
        res *= 2;
    }
    passes[n_passes++] = w->target_res;

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "voxelize_async: coarse-to-fine, %d passes, target=%d",
           n_passes, w->target_res);

    for (int p = 0; p < n_passes && !w->cancelled; p++) {
        int r = passes[p];
        DC_Error err = {0};

        dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
               "voxelize_async: pass %d/%d res=%d", p+1, n_passes, r);

        DC_VoxelGrid *grid = dc_voxelize_bezier(w->mesh_ptr, r, 2, 15, &err);

        if (w->cancelled) {
            dc_voxel_grid_free(grid);
            break;
        }

        /* Post this pass's result to main thread */
        PassResult *pr = calloc(1, sizeof(PassResult));
        if (pr) {
            pr->worker = w;
            pr->grid = grid;
            pr->final = (p == n_passes - 1);
            pr->pass_res = r;
            g_idle_add(on_pass_done, pr);
        } else {
            dc_voxel_grid_free(grid);
        }

        if (p == n_passes - 1) {
            dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
                   "voxelize_async: final pass done, %zu active",
                   grid ? dc_voxel_grid_active_count(grid) : 0);
        }
    }

    /* If cancelled before any pass posted final, clean up */
    if (w->cancelled) {
        PassResult *pr = calloc(1, sizeof(PassResult));
        if (pr) {
            pr->worker = w;
            pr->grid = NULL;
            pr->final = 1;
            g_idle_add(on_pass_done, pr);
        }
    }

    return NULL;
}

int
dc_voxelize_async(void *mesh_ptr, int resolution,
                    DC_VoxelizeDoneCb done_cb, void *userdata,
                    DC_Error *err)
{
    if (!mesh_ptr || !done_cb) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL mesh or callback");
        return -1;
    }

    dc_voxelize_async_cancel();

    VoxWorker *w = calloc(1, sizeof(VoxWorker));
    if (!w) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "alloc VoxWorker");
        return -1;
    }

    w->mesh_ptr = mesh_ptr;
    w->target_res = resolution;
    w->done_cb = done_cb;
    w->userdata = userdata;
    w->cancelled = 0;

    s_active_worker = w;

    GThread *t = g_thread_new("voxelize", voxelize_thread, w);
    if (!t) {
        free(w);
        s_active_worker = NULL;
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "g_thread_new failed");
        return -1;
    }
    g_thread_unref(t);

    return 0;
}

void
dc_voxelize_async_cancel(void)
{
    if (s_active_worker) {
        s_active_worker->cancelled = 1;
    }
}
