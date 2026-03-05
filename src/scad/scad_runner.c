#define _POSIX_C_SOURCE 200809L
#include "scad/scad_runner.h"
#include "core/log.h"

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */
static char *s_binary = NULL;   /* NULL = use default "openscad" */

void
dc_scad_set_binary(const char *path)
{
    free(s_binary);
    s_binary = path ? strdup(path) : NULL;
}

const char *
dc_scad_get_binary(void)
{
    return s_binary ? s_binary : "openscad";
}

/* -------------------------------------------------------------------------
 * Result lifecycle
 * ---------------------------------------------------------------------- */
void
dc_scad_result_free(DC_ScadResult *r)
{
    if (!r) return;
    free(r->stdout_text);
    free(r->stderr_text);
    free(r->output_path);
    free(r);
}

/* -------------------------------------------------------------------------
 * Async job internals
 * ---------------------------------------------------------------------- */
struct DC_ScadJob {
    GSubprocess  *proc;
    DC_ScadJobCb  cb;
    void         *userdata;
    char         *output_path;
    double        start_time;
    int           cancelled;
};

static double
now_secs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static void
on_process_complete(GObject *source, GAsyncResult *res, gpointer data)
{
    (void)source;
    DC_ScadJob *job = data;
    if (job->cancelled) {
        /* Callback suppressed */
        g_object_unref(job->proc);
        free(job->output_path);
        free(job);
        return;
    }

    GError *err = NULL;
    g_subprocess_wait_finish(job->proc, res, &err);

    DC_ScadResult *result = calloc(1, sizeof(*result));
    if (!result) {
        if (err) g_error_free(err);
        g_object_unref(job->proc);
        free(job->output_path);
        free(job);
        return;
    }

    result->elapsed_secs = now_secs() - job->start_time;
    result->output_path = job->output_path;  /* transfer ownership */
    job->output_path = NULL;

    if (err) {
        result->exit_code = -1;
        result->stderr_text = strdup(err->message);
        g_error_free(err);
    } else {
        result->exit_code = g_subprocess_get_exit_status(job->proc);

        /* Read captured stdout/stderr */
        GInputStream *out_stream =
            g_subprocess_get_stdout_pipe(job->proc);
        GInputStream *err_stream =
            g_subprocess_get_stderr_pipe(job->proc);

        if (out_stream) {
            GBytes *bytes = g_input_stream_read_bytes(
                out_stream, 1024 * 64, NULL, NULL);
            if (bytes) {
                gsize size;
                const char *data_ptr = g_bytes_get_data(bytes, &size);
                result->stdout_text = strndup(data_ptr, size);
                g_bytes_unref(bytes);
            }
        }
        if (err_stream) {
            GBytes *bytes = g_input_stream_read_bytes(
                err_stream, 1024 * 64, NULL, NULL);
            if (bytes) {
                gsize size;
                const char *data_ptr = g_bytes_get_data(bytes, &size);
                result->stderr_text = strndup(data_ptr, size);
                g_bytes_unref(bytes);
            }
        }
    }

    if (!result->stdout_text) result->stdout_text = strdup("");
    if (!result->stderr_text) result->stderr_text = strdup("");

    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP,
           "scad_runner: exit=%d time=%.2fs",
           result->exit_code, result->elapsed_secs);

    if (job->cb)
        job->cb(result, job->userdata);

    g_object_unref(job->proc);
    free(job);
}

/* Launch an async OpenSCAD process with the given argv.
 * Returns a job handle or NULL on failure. */
static DC_ScadJob *
launch_async(const char *const *argv, const char *output_path,
             DC_ScadJobCb cb, void *userdata)
{
    GError *err = NULL;
    GSubprocess *proc = g_subprocess_newv(
        argv,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
        &err);

    if (!proc) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "scad_runner: launch failed: %s", err->message);
        g_error_free(err);
        return NULL;
    }

    DC_ScadJob *job = calloc(1, sizeof(*job));
    if (!job) {
        g_object_unref(proc);
        return NULL;
    }

    job->proc = proc;
    job->cb = cb;
    job->userdata = userdata;
    job->output_path = output_path ? strdup(output_path) : NULL;
    job->start_time = now_secs();

    g_subprocess_wait_async(proc, NULL, on_process_complete, job);

    return job;
}

/* -------------------------------------------------------------------------
 * Public async API
 * ---------------------------------------------------------------------- */

DC_ScadJob *
dc_scad_render_png(const char *scad_path, const char *png_path,
                    int width, int height,
                    DC_ScadJobCb cb, void *userdata)
{
    if (!scad_path || !png_path) return NULL;

    char imgsize[64];
    if (width > 0 && height > 0)
        snprintf(imgsize, sizeof(imgsize), "%d,%d", width, height);
    else
        snprintf(imgsize, sizeof(imgsize), "400,300");

    char out_arg[560];
    snprintf(out_arg, sizeof(out_arg), "%s", png_path);

    const char *argv[] = {
        dc_scad_get_binary(),
        "-o", out_arg,
        "--preview",
        "--viewall",
        "--autocenter",
        "--imgsize", imgsize,
        "-q",
        scad_path,
        NULL
    };

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "scad_runner: render_png %s -> %s (%s)",
           scad_path, png_path, imgsize);

    return launch_async(argv, png_path, cb, userdata);
}

DC_ScadJob *
dc_scad_render_png_camera(const char *scad_path, const char *png_path,
                           int width, int height,
                           const DC_ScadCamera *cam,
                           DC_ScadJobCb cb, void *userdata)
{
    if (!cam || cam->dist <= 0)
        return dc_scad_render_png(scad_path, png_path, width, height, cb, userdata);

    if (!scad_path || !png_path) return NULL;

    char imgsize[64];
    if (width > 0 && height > 0)
        snprintf(imgsize, sizeof(imgsize), "%d,%d", width, height);
    else
        snprintf(imgsize, sizeof(imgsize), "400,300");

    char camera[256];
    snprintf(camera, sizeof(camera), "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
             cam->tx, cam->ty, cam->tz,
             cam->rx, cam->ry, cam->rz, cam->dist);

    char out_arg[560];
    snprintf(out_arg, sizeof(out_arg), "%s", png_path);

    const char *argv[] = {
        dc_scad_get_binary(),
        "-o", out_arg,
        "--preview",
        "--camera", camera,
        "--autocenter",
        "--imgsize", imgsize,
        "-q",
        scad_path,
        NULL
    };

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "scad_runner: render_png_camera %s -> %s (cam=%s)",
           scad_path, png_path, camera);

    return launch_async(argv, png_path, cb, userdata);
}

DC_ScadJob *
dc_scad_run_export(const char *scad_path, const char *output_path,
                     DC_ScadJobCb cb, void *userdata)
{
    if (!scad_path || !output_path) return NULL;

    const char *argv[] = {
        dc_scad_get_binary(),
        "-o", output_path,
        "-q",
        scad_path,
        NULL
    };

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "scad_runner: export %s -> %s", scad_path, output_path);

    return launch_async(argv, output_path, cb, userdata);
}

void
dc_scad_job_cancel(DC_ScadJob *job)
{
    if (!job) return;
    job->cancelled = 1;
    g_subprocess_force_exit(job->proc);
}

/* -------------------------------------------------------------------------
 * Open GUI (detached, fire-and-forget)
 * ---------------------------------------------------------------------- */
int
dc_scad_open_gui(const char *scad_path)
{
    if (!scad_path) return -1;

    const char *argv[] = {
        dc_scad_get_binary(),
        scad_path,
        NULL
    };

    GError *err = NULL;
    GSubprocess *proc = g_subprocess_newv(
        argv, G_SUBPROCESS_FLAGS_NONE, &err);

    if (!proc) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "scad_runner: open_gui failed: %s", err->message);
        g_error_free(err);
        return -1;
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "scad_runner: opened GUI for %s", scad_path);

    /* Detach — we don't wait or track this process */
    g_object_unref(proc);
    return 0;
}

/* -------------------------------------------------------------------------
 * Synchronous convenience
 * ---------------------------------------------------------------------- */
DC_ScadResult *
dc_scad_run_sync(const char *scad_path, const char *output_path,
                  const char *const *extra_args, int num_extra_args)
{
    if (!scad_path) return NULL;

    /* Build argv: binary [-o output] [extra_args...] [-q] scad_path */
    int max_args = 4 + num_extra_args + 2;
    const char **argv = calloc((size_t)(max_args + 1), sizeof(char *));
    if (!argv) return NULL;

    int argc = 0;
    argv[argc++] = dc_scad_get_binary();

    if (output_path) {
        argv[argc++] = "-o";
        argv[argc++] = output_path;
    }

    for (int i = 0; i < num_extra_args && extra_args; i++)
        argv[argc++] = extra_args[i];

    argv[argc++] = "-q";
    argv[argc++] = scad_path;
    argv[argc] = NULL;

    GError *err = NULL;
    GSubprocess *proc = g_subprocess_newv(
        argv,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
        &err);
    free(argv);

    if (!proc) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "scad_runner: sync launch failed: %s", err->message);
        g_error_free(err);
        return NULL;
    }

    double t0 = now_secs();

    char *out_buf = NULL;
    char *err_buf = NULL;
    g_subprocess_communicate_utf8(proc, NULL, NULL,
                                   &out_buf, &err_buf, &err);

    DC_ScadResult *result = calloc(1, sizeof(*result));
    if (!result) {
        g_object_unref(proc);
        g_free(out_buf);
        g_free(err_buf);
        if (err) g_error_free(err);
        return NULL;
    }

    result->elapsed_secs = now_secs() - t0;
    result->output_path = output_path ? strdup(output_path) : NULL;

    if (err) {
        result->exit_code = -1;
        result->stderr_text = strdup(err->message);
        result->stdout_text = strdup("");
        g_error_free(err);
    } else {
        result->exit_code = g_subprocess_get_exit_status(proc);
        /* g_subprocess_communicate_utf8 returns g_malloc'd strings */
        result->stdout_text = out_buf ? strdup(out_buf) : strdup("");
        result->stderr_text = err_buf ? strdup(err_buf) : strdup("");
    }

    g_free(out_buf);
    g_free(err_buf);
    g_object_unref(proc);

    return result;
}
