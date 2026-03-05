#define _POSIX_C_SOURCE 200809L
#include "inspect/inspect.h"
#include "ui/code_editor.h"
#include "bezier/bezier_canvas.h"
#include "scad/scad_runner.h"
#include "core/string_builder.h"
#include "core/log.h"

#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Module state (singleton — only one DunCAD instance per machine)
 * ---------------------------------------------------------------------- */
static GSocketService  *s_service   = NULL;
static DC_BezierEditor *s_editor    = NULL;
static DC_CodeEditor   *s_code_ed   = NULL;

/* -------------------------------------------------------------------------
 * Command handlers — each returns a malloc'd JSON string (caller frees)
 * ---------------------------------------------------------------------- */

static char *
cmd_state(void)
{
    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) return strdup("{\"error\":\"alloc\"}\n");

    int count  = dc_bezier_editor_point_count(s_editor);
    int sel    = dc_bezier_editor_selected_point(s_editor);
    int closed = dc_bezier_editor_is_closed(s_editor);
    int chain  = dc_bezier_editor_get_chain_mode(s_editor);

    dc_sb_appendf(sb,
        "{\"editor\":{\"point_count\":%d,\"selected\":%d,"
        "\"closed\":%s,\"chain_mode\":%s},",
        count, sel,
        closed ? "true" : "false",
        chain  ? "true" : "false");

    dc_sb_append(sb, "\"points\":[");
    for (int i = 0; i < count; i++) {
        double x, y;
        dc_bezier_editor_get_point(s_editor, i, &x, &y);
        int junc = dc_bezier_editor_is_juncture(s_editor, i);
        if (i > 0) dc_sb_append(sb, ",");
        dc_sb_appendf(sb,
            "{\"i\":%d,\"x\":%.4f,\"y\":%.4f,\"juncture\":%s}",
            i, x, y, junc ? "true" : "false");
    }
    dc_sb_append(sb, "],");

    DC_BezierCanvas *canvas = dc_bezier_editor_get_canvas(s_editor);
    double zoom = dc_bezier_canvas_get_zoom(canvas);
    double pan_x, pan_y;
    dc_bezier_canvas_get_pan(canvas, &pan_x, &pan_y);
    int vw, vh;
    dc_bezier_canvas_get_viewport_size(canvas, &vw, &vh);

    dc_sb_appendf(sb,
        "\"canvas\":{\"zoom\":%.4f,\"pan_x\":%.4f,"
        "\"pan_y\":%.4f,\"width\":%d,\"height\":%d}}",
        zoom, pan_x, pan_y, vw, vh);

    dc_sb_append(sb, "\n");
    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}

static char *
cmd_render(const char *args)
{
    const char *path = "/tmp/duncad-canvas.png";
    char custom_path[512];
    if (args && *args) {
        if (sscanf(args, "%511s", custom_path) == 1)
            path = custom_path;
    }

    DC_BezierCanvas *canvas = dc_bezier_editor_get_canvas(s_editor);
    int rc = dc_bezier_canvas_render_to_png(canvas, path, 0, 0);

    char *resp = malloc(640);
    if (!resp) return NULL;
    if (rc == 0)
        snprintf(resp, 640, "{\"ok\":true,\"path\":\"%s\"}\n", path);
    else
        snprintf(resp, 640, "{\"ok\":false,\"error\":\"render failed\"}\n");
    return resp;
}

static char *
cmd_select(const char *args)
{
    int index = -1;
    if (!args || sscanf(args, "%d", &index) != 1)
        return strdup("{\"error\":\"usage: select <index>\"}\n");

    dc_bezier_editor_select(s_editor, index);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_set_point(const char *args)
{
    int index;
    double x, y;
    if (!args || sscanf(args, "%d %lf %lf", &index, &x, &y) != 3)
        return strdup("{\"error\":\"usage: set_point <index> <x> <y>\"}\n");

    dc_bezier_editor_set_point(s_editor, index, x, y);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_add_point(const char *args)
{
    double x, y;
    if (!args || sscanf(args, "%lf %lf", &x, &y) != 2)
        return strdup("{\"error\":\"usage: add_point <x> <y>\"}\n");

    int rc = dc_bezier_editor_add_point_at(s_editor, x, y);
    char *resp = malloc(128);
    if (!resp) return NULL;
    snprintf(resp, 128, "{\"ok\":%s}\n", rc == 0 ? "true" : "false");
    return resp;
}

static char *
cmd_delete(void)
{
    dc_bezier_editor_delete_selected(s_editor);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_zoom(const char *args)
{
    double level;
    if (!args || sscanf(args, "%lf", &level) != 1)
        return strdup("{\"error\":\"usage: zoom <level>\"}\n");

    DC_BezierCanvas *canvas = dc_bezier_editor_get_canvas(s_editor);
    dc_bezier_canvas_set_zoom(canvas, level);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_pan(const char *args)
{
    double x, y;
    if (!args || sscanf(args, "%lf %lf", &x, &y) != 2)
        return strdup("{\"error\":\"usage: pan <x> <y>\"}\n");

    DC_BezierCanvas *canvas = dc_bezier_editor_get_canvas(s_editor);
    dc_bezier_canvas_set_pan(canvas, x, y);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_chain(const char *args)
{
    int on;
    if (!args || sscanf(args, "%d", &on) != 1)
        return strdup("{\"error\":\"usage: chain <0|1>\"}\n");

    dc_bezier_editor_set_chain_mode(s_editor, on);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_juncture(const char *args)
{
    int index, on;
    if (!args || sscanf(args, "%d %d", &index, &on) != 2)
        return strdup("{\"error\":\"usage: juncture <index> <0|1>\"}\n");

    dc_bezier_editor_set_juncture(s_editor, index, on);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_export(const char *args)
{
    if (!args || !*args)
        return strdup("{\"error\":\"usage: export <path>\"}\n");

    char path[512];
    if (sscanf(args, "%511s", path) != 1)
        return strdup("{\"error\":\"usage: export <path>\"}\n");

    DC_Error err = {0};
    int rc = dc_bezier_editor_export_scad(s_editor, path, &err);

    char *resp = malloc(640);
    if (!resp) return NULL;
    if (rc == 0)
        snprintf(resp, 640, "{\"ok\":true,\"path\":\"%s\"}\n", path);
    else
        snprintf(resp, 640, "{\"ok\":false,\"error\":\"%s\"}\n", err.message);
    return resp;
}

static char *
cmd_render_scad(const char *args)
{
    char scad_path[512];
    char png_path[512] = "/tmp/duncad-scad-preview.png";
    int parsed = sscanf(args, "%511s %511s", scad_path, png_path);
    if (parsed < 1 || !*scad_path)
        return strdup("{\"error\":\"usage: render_scad <scad_path> [png_path]\"}\n");

    /* Synchronous render for simplicity in the inspect protocol */
    const char *extra[] = {
        "--preview", "--viewall", "--autocenter",
        "--imgsize", "800,600"
    };
    DC_ScadResult *r = dc_scad_run_sync(scad_path, png_path, extra, 5);
    if (!r)
        return strdup("{\"ok\":false,\"error\":\"failed to launch openscad\"}\n");

    char *resp = malloc(1024);
    if (!resp) { dc_scad_result_free(r); return NULL; }

    if (r->exit_code == 0) {
        snprintf(resp, 1024,
            "{\"ok\":true,\"path\":\"%s\",\"time\":%.2f}\n",
            png_path, r->elapsed_secs);
    } else {
        /* Escape stderr for JSON (simple: replace quotes/newlines) */
        char safe_err[512];
        size_t j = 0;
        for (size_t i = 0; r->stderr_text[i] && j < sizeof(safe_err) - 2; i++) {
            char c = r->stderr_text[i];
            if (c == '"') { safe_err[j++] = '\\'; safe_err[j++] = '"'; }
            else if (c == '\n') { safe_err[j++] = '\\'; safe_err[j++] = 'n'; }
            else if (c == '\\') { safe_err[j++] = '\\'; safe_err[j++] = '\\'; }
            else safe_err[j++] = c;
        }
        safe_err[j] = '\0';
        snprintf(resp, 1024,
            "{\"ok\":false,\"exit\":%d,\"error\":\"%s\"}\n",
            r->exit_code, safe_err);
    }

    dc_scad_result_free(r);
    return resp;
}

static char *
cmd_open_scad(const char *args)
{
    char path[512];
    if (!args || sscanf(args, "%511s", path) != 1)
        return strdup("{\"error\":\"usage: open_scad <path>\"}\n");

    int rc = dc_scad_open_gui(path);
    char *resp = malloc(128);
    if (!resp) return NULL;
    snprintf(resp, 128, "{\"ok\":%s}\n", rc == 0 ? "true" : "false");
    return resp;
}

static char *
cmd_get_code(void)
{
    if (!s_code_ed)
        return strdup("{\"error\":\"no code editor\"}\n");

    char *text = dc_code_editor_get_text(s_code_ed);
    const char *path = dc_code_editor_get_path(s_code_ed);

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) { free(text); return strdup("{\"error\":\"alloc\"}\n"); }

    dc_sb_append(sb, "{\"ok\":true,\"path\":");
    if (path) {
        dc_sb_appendf(sb, "\"%s\"", path);
    } else {
        dc_sb_append(sb, "null");
    }
    dc_sb_appendf(sb, ",\"length\":%d}\n", text ? (int)strlen(text) : 0);
    free(text);

    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}

static char *
cmd_set_code(const char *args)
{
    if (!s_code_ed)
        return strdup("{\"error\":\"no code editor\"}\n");
    if (!args || !*args)
        return strdup("{\"error\":\"usage: set_code <text>\"}\n");

    dc_code_editor_set_text(s_code_ed, args);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_open_file(const char *args)
{
    if (!s_code_ed)
        return strdup("{\"error\":\"no code editor\"}\n");

    char path[512];
    if (!args || sscanf(args, "%511s", path) != 1)
        return strdup("{\"error\":\"usage: open_file <path>\"}\n");

    int rc = dc_code_editor_open_file(s_code_ed, path);
    char *resp = malloc(640);
    if (!resp) return NULL;
    if (rc == 0)
        snprintf(resp, 640, "{\"ok\":true,\"path\":\"%s\"}\n", path);
    else
        snprintf(resp, 640, "{\"ok\":false,\"error\":\"cannot open %s\"}\n", path);
    return resp;
}

static char *
cmd_save_file(const char *args)
{
    if (!s_code_ed)
        return strdup("{\"error\":\"no code editor\"}\n");

    if (args && *args) {
        char path[512];
        if (sscanf(args, "%511s", path) == 1) {
            int rc = dc_code_editor_save_as(s_code_ed, path);
            char *resp = malloc(640);
            if (!resp) return NULL;
            snprintf(resp, 640, "{\"ok\":%s}\n", rc == 0 ? "true" : "false");
            return resp;
        }
    }

    int rc = dc_code_editor_save(s_code_ed);
    char *resp = malloc(128);
    if (!resp) return NULL;
    snprintf(resp, 128, "{\"ok\":%s}\n", rc == 0 ? "true" : "false");
    return resp;
}

static char *
cmd_help(void)
{
    return strdup(
        "{\"commands\":["
        "\"state\","
        "\"render [path]\","
        "\"render_scad <scad_path> [png_path]\","
        "\"open_scad <path>\","
        "\"select <index>\","
        "\"set_point <index> <x> <y>\","
        "\"add_point <x> <y>\","
        "\"delete\","
        "\"zoom <level>\","
        "\"pan <x> <y>\","
        "\"chain <0|1>\","
        "\"juncture <index> <0|1>\","
        "\"export <path>\","
        "\"get_code\","
        "\"set_code <text>\","
        "\"open_file <path>\","
        "\"save_file [path]\","
        "\"help\""
        "]}\n"
    );
}

/* -------------------------------------------------------------------------
 * Command dispatch
 * ---------------------------------------------------------------------- */
static char *
dispatch(const char *cmd)
{
    if (!cmd || !*cmd)
        return strdup("{\"error\":\"empty command\"}\n");

    char name[64];
    int offset = 0;
    if (sscanf(cmd, "%63s%n", name, &offset) != 1)
        return strdup("{\"error\":\"parse error\"}\n");

    const char *args = cmd + offset;
    while (*args == ' ') args++;

    if (strcmp(name, "state")     == 0) return cmd_state();
    if (strcmp(name, "render")    == 0) return cmd_render(args);
    if (strcmp(name, "select")    == 0) return cmd_select(args);
    if (strcmp(name, "set_point") == 0) return cmd_set_point(args);
    if (strcmp(name, "add_point") == 0) return cmd_add_point(args);
    if (strcmp(name, "delete")    == 0) return cmd_delete();
    if (strcmp(name, "zoom")      == 0) return cmd_zoom(args);
    if (strcmp(name, "pan")       == 0) return cmd_pan(args);
    if (strcmp(name, "chain")     == 0) return cmd_chain(args);
    if (strcmp(name, "juncture")  == 0) return cmd_juncture(args);
    if (strcmp(name, "export")      == 0) return cmd_export(args);
    if (strcmp(name, "render_scad") == 0) return cmd_render_scad(args);
    if (strcmp(name, "open_scad")   == 0) return cmd_open_scad(args);
    if (strcmp(name, "get_code")    == 0) return cmd_get_code();
    if (strcmp(name, "set_code")    == 0) return cmd_set_code(args);
    if (strcmp(name, "open_file")   == 0) return cmd_open_file(args);
    if (strcmp(name, "save_file")   == 0) return cmd_save_file(args);
    if (strcmp(name, "help")        == 0) return cmd_help();

    char *err = malloc(256);
    if (!err) return NULL;
    snprintf(err, 256, "{\"error\":\"unknown command: %s\"}\n", name);
    return err;
}

/* -------------------------------------------------------------------------
 * Socket connection handler
 * ---------------------------------------------------------------------- */
static gboolean
on_incoming(GSocketService *service, GSocketConnection *conn,
            GObject *source, gpointer data)
{
    (void)service; (void)source; (void)data;

    GInputStream  *in  = g_io_stream_get_input_stream(G_IO_STREAM(conn));
    GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(conn));

    /* Read one line (command) */
    char buf[4096];
    gssize n = g_input_stream_read(in, buf, sizeof(buf) - 1, NULL, NULL);
    if (n <= 0) return TRUE;
    buf[n] = '\0';

    /* Strip trailing newline/carriage return */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';

    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "inspect: cmd='%s'", buf);

    char *response = dispatch(buf);
    if (response) {
        gsize written = 0;
        g_output_stream_write_all(out, response, strlen(response),
                                  &written, NULL, NULL);
        free(response);
    }

    return TRUE;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
int
dc_inspect_start(DC_BezierEditor *editor, DC_CodeEditor *code_ed)
{
    if (!editor) return -1;
    if (s_service) return -1;  /* already running */

    s_editor  = editor;
    s_code_ed = code_ed;

    /* Remove stale socket from previous crash */
    unlink(DC_INSPECT_SOCK_PATH);

    s_service = g_socket_service_new();
    GSocketAddress *addr = g_unix_socket_address_new(DC_INSPECT_SOCK_PATH);
    GError *err = NULL;

    gboolean ok = g_socket_listener_add_address(
        G_SOCKET_LISTENER(s_service), addr,
        G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT,
        NULL, NULL, &err);
    g_object_unref(addr);

    if (!ok) {
        dc_log(DC_LOG_ERROR, DC_LOG_EVENT_APP,
               "inspect: bind failed: %s", err->message);
        g_error_free(err);
        g_object_unref(s_service);
        s_service = NULL;
        return -1;
    }

    g_signal_connect(s_service, "incoming",
                     G_CALLBACK(on_incoming), NULL);
    g_socket_service_start(s_service);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "inspect: listening on %s", DC_INSPECT_SOCK_PATH);
    return 0;
}

void
dc_inspect_stop(void)
{
    if (!s_service) return;
    g_socket_service_stop(s_service);
    g_object_unref(s_service);
    s_service = NULL;
    s_editor  = NULL;
    s_code_ed = NULL;
    unlink(DC_INSPECT_SOCK_PATH);
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "inspect: stopped");
}
