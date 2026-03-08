#define _POSIX_C_SOURCE 200809L
#include "inspect/inspect.h"
#include "ui/app_window.h"
#include "ui/code_editor.h"
#include "ui/scad_preview.h"
#include "ui/transform_panel.h"
#include "gl/gl_viewport.h"
#include "bezier/bezier_editor.h"
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
static GSocketService  *s_service = NULL;
static GtkWidget       *s_window  = NULL;

/* Convenience accessors — all borrowed pointers, may be NULL */
static DC_BezierEditor  *get_editor(void)  { return dc_app_window_get_editor(s_window); }
static DC_CodeEditor    *get_code_ed(void) { return dc_app_window_get_code_editor(s_window); }
static DC_ScadPreview   *get_preview(void) { return dc_app_window_get_scad_preview(s_window); }

static DC_GlViewport *
get_viewport(void)
{
    DC_ScadPreview *pv = get_preview();
    return pv ? dc_scad_preview_get_viewport(pv) : NULL;
}

static DC_TransformPanel *
get_transform(void)
{
    DC_ScadPreview *pv = get_preview();
    return pv ? dc_scad_preview_get_transform(pv) : NULL;
}

/* -------------------------------------------------------------------------
 * JSON string escape helper
 * ---------------------------------------------------------------------- */
static void
sb_append_json_str(DC_StringBuilder *sb, const char *s)
{
    dc_sb_append(sb, "\"");
    for (const char *p = s; *p; p++) {
        if (*p == '"')       dc_sb_append(sb, "\\\"");
        else if (*p == '\\') dc_sb_append(sb, "\\\\");
        else if (*p == '\n') dc_sb_append(sb, "\\n");
        else if (*p == '\r') dc_sb_append(sb, "\\r");
        else if (*p == '\t') dc_sb_append(sb, "\\t");
        else                 dc_sb_append_char(sb, *p);
    }
    dc_sb_append(sb, "\"");
}

/* =========================================================================
 * BEZIER COMMANDS — existing, preserved from original inspect.c
 * ========================================================================= */

static char *
cmd_state(void)
{
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) return strdup("{\"error\":\"alloc\"}\n");

    int count  = dc_bezier_editor_point_count(ed);
    int sel    = dc_bezier_editor_selected_point(ed);
    int closed = dc_bezier_editor_is_closed(ed);
    int chain  = dc_bezier_editor_get_chain_mode(ed);

    dc_sb_appendf(sb,
        "{\"editor\":{\"point_count\":%d,\"selected\":%d,"
        "\"closed\":%s,\"chain_mode\":%s},",
        count, sel,
        closed ? "true" : "false",
        chain  ? "true" : "false");

    dc_sb_append(sb, "\"points\":[");
    for (int i = 0; i < count; i++) {
        double x, y;
        dc_bezier_editor_get_point(ed, i, &x, &y);
        int junc = dc_bezier_editor_is_juncture(ed, i);
        if (i > 0) dc_sb_append(sb, ",");
        dc_sb_appendf(sb,
            "{\"i\":%d,\"x\":%.4f,\"y\":%.4f,\"juncture\":%s}",
            i, x, y, junc ? "true" : "false");
    }
    dc_sb_append(sb, "],");

    DC_BezierCanvas *canvas = dc_bezier_editor_get_canvas(ed);
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
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    const char *path = "/tmp/duncad-canvas.png";
    char custom_path[512];
    if (args && *args) {
        if (sscanf(args, "%511s", custom_path) == 1)
            path = custom_path;
    }

    DC_BezierCanvas *canvas = dc_bezier_editor_get_canvas(ed);
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
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    int index = -1;
    if (!args || sscanf(args, "%d", &index) != 1)
        return strdup("{\"error\":\"usage: select <index>\"}\n");

    dc_bezier_editor_select(ed, index);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_set_point(const char *args)
{
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    int index;
    double x, y;
    if (!args || sscanf(args, "%d %lf %lf", &index, &x, &y) != 3)
        return strdup("{\"error\":\"usage: set_point <index> <x> <y>\"}\n");

    dc_bezier_editor_set_point(ed, index, x, y);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_add_point(const char *args)
{
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    double x, y;
    if (!args || sscanf(args, "%lf %lf", &x, &y) != 2)
        return strdup("{\"error\":\"usage: add_point <x> <y>\"}\n");

    int rc = dc_bezier_editor_add_point_at(ed, x, y);
    char *resp = malloc(128);
    if (!resp) return NULL;
    snprintf(resp, 128, "{\"ok\":%s}\n", rc == 0 ? "true" : "false");
    return resp;
}

static char *
cmd_delete(void)
{
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");
    dc_bezier_editor_delete_selected(ed);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_zoom(const char *args)
{
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    double level;
    if (!args || sscanf(args, "%lf", &level) != 1)
        return strdup("{\"error\":\"usage: zoom <level>\"}\n");

    DC_BezierCanvas *canvas = dc_bezier_editor_get_canvas(ed);
    dc_bezier_canvas_set_zoom(canvas, level);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_pan(const char *args)
{
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    double x, y;
    if (!args || sscanf(args, "%lf %lf", &x, &y) != 2)
        return strdup("{\"error\":\"usage: pan <x> <y>\"}\n");

    DC_BezierCanvas *canvas = dc_bezier_editor_get_canvas(ed);
    dc_bezier_canvas_set_pan(canvas, x, y);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_chain(const char *args)
{
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    int on;
    if (!args || sscanf(args, "%d", &on) != 1)
        return strdup("{\"error\":\"usage: chain <0|1>\"}\n");

    dc_bezier_editor_set_chain_mode(ed, on);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_juncture(const char *args)
{
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    int index, on;
    if (!args || sscanf(args, "%d %d", &index, &on) != 2)
        return strdup("{\"error\":\"usage: juncture <index> <0|1>\"}\n");

    dc_bezier_editor_set_juncture(ed, index, on);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_export(const char *args)
{
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    if (!args || !*args)
        return strdup("{\"error\":\"usage: export <path>\"}\n");

    char path[512];
    if (sscanf(args, "%511s", path) != 1)
        return strdup("{\"error\":\"usage: export <path>\"}\n");

    DC_Error err = {0};
    int rc = dc_bezier_editor_export_scad(ed, path, &err);

    char *resp = malloc(640);
    if (!resp) return NULL;
    if (rc == 0)
        snprintf(resp, 640, "{\"ok\":true,\"path\":\"%s\"}\n", path);
    else
        snprintf(resp, 640, "{\"ok\":false,\"error\":\"%s\"}\n", err.message);
    return resp;
}

static char *
cmd_insert_scad(void)
{
    DC_BezierEditor *ed = get_editor();
    if (!ed) return strdup("{\"error\":\"no bezier editor\"}\n");

    DC_Error err = {0};
    int rc = dc_bezier_editor_insert_scad(ed, &err);

    char *resp = malloc(640);
    if (!resp) return NULL;
    if (rc == 0)
        snprintf(resp, 640, "{\"ok\":true}\n");
    else
        snprintf(resp, 640, "{\"ok\":false,\"error\":\"%s\"}\n", err.message);
    return resp;
}

/* =========================================================================
 * SCAD / OPENSCAD COMMANDS
 * ========================================================================= */

static char *
cmd_render_scad(const char *args)
{
    char scad_path[512];
    char png_path[512] = "/tmp/duncad-scad-preview.png";
    int parsed = sscanf(args, "%511s %511s", scad_path, png_path);
    if (parsed < 1 || !*scad_path)
        return strdup("{\"error\":\"usage: render_scad <scad_path> [png_path]\"}\n");

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

/* Trigger SCAD preview render (F5 equivalent) */
static char *
cmd_preview_render(void)
{
    DC_ScadPreview *pv = get_preview();
    if (!pv) return strdup("{\"error\":\"no scad preview\"}\n");

    dc_scad_preview_render(pv);
    return strdup("{\"ok\":true}\n");
}

/* =========================================================================
 * CODE EDITOR COMMANDS
 * ========================================================================= */

static char *
cmd_get_code(void)
{
    DC_CodeEditor *ed = get_code_ed();
    if (!ed) return strdup("{\"error\":\"no code editor\"}\n");

    char *text = dc_code_editor_get_text(ed);
    const char *path = dc_code_editor_get_path(ed);

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
cmd_get_code_text(void)
{
    DC_CodeEditor *ed = get_code_ed();
    if (!ed) return strdup("{\"error\":\"no code editor\"}\n");

    char *text = dc_code_editor_get_text(ed);
    if (!text)
        return strdup("{\"ok\":true,\"text\":\"\"}\n");

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) { free(text); return strdup("{\"error\":\"alloc\"}\n"); }

    dc_sb_append(sb, "{\"ok\":true,\"text\":");
    sb_append_json_str(sb, text);
    dc_sb_append(sb, "}\n");
    free(text);

    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}

static char *
cmd_set_code(const char *args)
{
    DC_CodeEditor *ed = get_code_ed();
    if (!ed) return strdup("{\"error\":\"no code editor\"}\n");
    if (!args || !*args)
        return strdup("{\"error\":\"usage: set_code <text>\"}\n");

    dc_code_editor_set_text(ed, args);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_open_file(const char *args)
{
    DC_CodeEditor *ed = get_code_ed();
    if (!ed) return strdup("{\"error\":\"no code editor\"}\n");

    char path[512];
    if (!args || sscanf(args, "%511s", path) != 1)
        return strdup("{\"error\":\"usage: open_file <path>\"}\n");

    int rc = dc_code_editor_open_file(ed, path);
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
    DC_CodeEditor *ed = get_code_ed();
    if (!ed) return strdup("{\"error\":\"no code editor\"}\n");

    if (args && *args) {
        char path[512];
        if (sscanf(args, "%511s", path) == 1) {
            int rc = dc_code_editor_save_as(ed, path);
            char *resp = malloc(640);
            if (!resp) return NULL;
            snprintf(resp, 640, "{\"ok\":%s}\n", rc == 0 ? "true" : "false");
            return resp;
        }
    }

    int rc = dc_code_editor_save(ed);
    char *resp = malloc(128);
    if (!resp) return NULL;
    snprintf(resp, 128, "{\"ok\":%s}\n", rc == 0 ? "true" : "false");
    return resp;
}

static char *
cmd_select_lines(const char *args)
{
    DC_CodeEditor *ed = get_code_ed();
    if (!ed) return strdup("{\"error\":\"no code editor\"}\n");

    int start, end;
    if (!args || sscanf(args, "%d %d", &start, &end) != 2)
        return strdup("{\"error\":\"usage: select_lines <start> <end>\"}\n");

    dc_code_editor_select_lines(ed, start, end);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_insert_text(const char *args)
{
    DC_CodeEditor *ed = get_code_ed();
    if (!ed) return strdup("{\"error\":\"no code editor\"}\n");
    if (!args || !*args)
        return strdup("{\"error\":\"usage: insert_text <text>\"}\n");

    dc_code_editor_insert_at_cursor(ed, args);
    return strdup("{\"ok\":true}\n");
}

/* =========================================================================
 * GL VIEWPORT COMMANDS — NEW
 * ========================================================================= */

static char *
cmd_gl_state(void)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    float cx, cy, cz;
    dc_gl_viewport_get_camera_center(vp, &cx, &cy, &cz);
    float dist = dc_gl_viewport_get_camera_dist(vp);
    float theta, phi;
    dc_gl_viewport_get_camera_angles(vp, &theta, &phi);
    int ortho   = dc_gl_viewport_get_ortho(vp);
    int grid    = dc_gl_viewport_get_grid(vp);
    int axes    = dc_gl_viewport_get_axes(vp);
    int obj_cnt = dc_gl_viewport_get_object_count(vp);
    int sel     = dc_gl_viewport_get_selected(vp);

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) return strdup("{\"error\":\"alloc\"}\n");

    dc_sb_appendf(sb,
        "{\"camera\":{\"center\":[%.3f,%.3f,%.3f],"
        "\"dist\":%.3f,\"theta\":%.3f,\"phi\":%.3f,"
        "\"ortho\":%s},",
        cx, cy, cz, dist, theta, phi,
        ortho ? "true" : "false");

    dc_sb_appendf(sb,
        "\"display\":{\"grid\":%s,\"axes\":%s},",
        grid ? "true" : "false",
        axes ? "true" : "false");

    dc_sb_appendf(sb,
        "\"objects\":{\"count\":%d,\"selected\":%d}}\n",
        obj_cnt, sel);

    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}

static char *
cmd_gl_camera(const char *args)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    float cx, cy, cz, dist, theta, phi;
    int parsed = sscanf(args, "%f %f %f %f %f %f",
                         &cx, &cy, &cz, &dist, &theta, &phi);
    if (parsed != 6)
        return strdup("{\"error\":\"usage: gl_camera <cx> <cy> <cz> <dist> <theta> <phi>\"}\n");

    dc_gl_viewport_set_camera_center(vp, cx, cy, cz);
    dc_gl_viewport_set_camera_dist(vp, dist);
    dc_gl_viewport_set_camera_angles(vp, theta, phi);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_gl_reset(void)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    dc_gl_viewport_reset_camera(vp);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_gl_ortho(void)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    dc_gl_viewport_toggle_ortho(vp);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_gl_grid(void)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    dc_gl_viewport_toggle_grid(vp);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_gl_axes(void)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    dc_gl_viewport_toggle_axes(vp);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_gl_select(const char *args)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    int idx;
    if (!args || sscanf(args, "%d", &idx) != 1)
        return strdup("{\"error\":\"usage: gl_select <index>\"}\n");

    dc_gl_viewport_select_object(vp, idx);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_gl_load(const char *args)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    char path[512];
    if (!args || sscanf(args, "%511s", path) != 1)
        return strdup("{\"error\":\"usage: gl_load <stl_path>\"}\n");

    int rc = dc_gl_viewport_load_stl(vp, path);
    char *resp = malloc(640);
    if (!resp) return NULL;
    if (rc == 0)
        snprintf(resp, 640, "{\"ok\":true,\"path\":\"%s\"}\n", path);
    else
        snprintf(resp, 640, "{\"ok\":false,\"error\":\"load failed\"}\n");
    return resp;
}

static char *
cmd_gl_clear(void)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    dc_gl_viewport_clear_objects(vp);
    return strdup("{\"ok\":true}\n");
}

/* =========================================================================
 * TRANSFORM PANEL COMMANDS — NEW
 * ========================================================================= */

static char *
cmd_transform_show(const char *args)
{
    DC_TransformPanel *tp = get_transform();
    if (!tp) return strdup("{\"error\":\"no transform panel\"}\n");

    if (!args || !*args)
        return strdup("{\"error\":\"usage: transform_show <stmt_text> <line_start> <line_end>\"}\n");

    /* Parse: last two tokens are line_start line_end, rest is stmt_text */
    int line_start = 0, line_end = 0;
    /* Find the last two space-separated integers */
    const char *p = args + strlen(args) - 1;
    /* Skip trailing whitespace */
    while (p > args && *p == ' ') p--;
    /* Find end number */
    while (p > args && p[-1] >= '0' && p[-1] <= '9') p--;
    if (p > args && p[-1] == '-') p--;
    const char *end_num_start = p;
    /* Skip space */
    if (p > args) p--;
    while (p > args && *p == ' ') p--;
    /* Find start number */
    while (p > args && p[-1] >= '0' && p[-1] <= '9') p--;
    if (p > args && p[-1] == '-') p--;
    const char *start_num_start = p;

    if (sscanf(start_num_start, "%d", &line_start) != 1 ||
        sscanf(end_num_start, "%d", &line_end) != 1) {
        return strdup("{\"error\":\"usage: transform_show <stmt_text> <line_start> <line_end>\"}\n");
    }

    /* stmt_text is everything before start_num_start */
    size_t text_len = (size_t)(start_num_start - args);
    while (text_len > 0 && args[text_len - 1] == ' ') text_len--;
    char *stmt = strndup(args, text_len);
    if (!stmt) return strdup("{\"error\":\"alloc\"}\n");

    dc_transform_panel_show(tp, stmt, line_start, line_end);
    free(stmt);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_transform_hide(void)
{
    DC_TransformPanel *tp = get_transform();
    if (!tp) return strdup("{\"error\":\"no transform panel\"}\n");

    dc_transform_panel_hide(tp);
    return strdup("{\"ok\":true}\n");
}

/* =========================================================================
 * WINDOW COMMANDS — NEW
 * ========================================================================= */

static char *
cmd_window_title(const char *args)
{
    if (!s_window) return strdup("{\"error\":\"no window\"}\n");
    if (!args || !*args)
        return strdup("{\"error\":\"usage: window_title <title>\"}\n");

    dc_app_window_set_project_name(s_window, args);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_window_status(const char *args)
{
    if (!s_window) return strdup("{\"error\":\"no window\"}\n");

    dc_app_window_set_status(s_window, (args && *args) ? args : NULL);
    return strdup("{\"ok\":true}\n");
}

static char *
cmd_window_size(void)
{
    if (!s_window) return strdup("{\"error\":\"no window\"}\n");

    int w = gtk_widget_get_width(s_window);
    int h = gtk_widget_get_height(s_window);

    char *resp = malloc(128);
    if (!resp) return NULL;
    snprintf(resp, 128, "{\"width\":%d,\"height\":%d}\n", w, h);
    return resp;
}

/* =========================================================================
 * HELP — comprehensive command listing
 * ========================================================================= */

static char *
cmd_help(void)
{
    return strdup(
        "{\"commands\":{"

        "\"bezier\":["
        "\"state\","
        "\"render [path]\","
        "\"select <index>\","
        "\"set_point <index> <x> <y>\","
        "\"add_point <x> <y>\","
        "\"delete\","
        "\"zoom <level>\","
        "\"pan <x> <y>\","
        "\"chain <0|1>\","
        "\"juncture <index> <0|1>\","
        "\"export <path>\","
        "\"insert_scad\""
        "],"

        "\"code\":["
        "\"get_code\","
        "\"get_code_text\","
        "\"set_code <text>\","
        "\"open_file <path>\","
        "\"save_file [path]\","
        "\"select_lines <start> <end>\","
        "\"insert_text <text>\""
        "],"

        "\"scad\":["
        "\"render_scad <scad_path> [png_path]\","
        "\"open_scad <path>\","
        "\"preview_render\""
        "],"

        "\"gl\":["
        "\"gl_state\","
        "\"gl_camera <cx> <cy> <cz> <dist> <theta> <phi>\","
        "\"gl_reset\","
        "\"gl_ortho\","
        "\"gl_grid\","
        "\"gl_axes\","
        "\"gl_select <index>\","
        "\"gl_load <stl_path>\","
        "\"gl_clear\""
        "],"

        "\"transform\":["
        "\"transform_show <stmt> <line_start> <line_end>\","
        "\"transform_hide\""
        "],"

        "\"window\":["
        "\"window_title <title>\","
        "\"window_status <text>\","
        "\"window_size\""
        "],"

        "\"meta\":["
        "\"help\""
        "]"

        "}}\n"
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

    /* Bezier */
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
    if (strcmp(name, "export")    == 0) return cmd_export(args);
    if (strcmp(name, "insert_scad") == 0) return cmd_insert_scad();

    /* Code editor */
    if (strcmp(name, "get_code")      == 0) return cmd_get_code();
    if (strcmp(name, "get_code_text") == 0) return cmd_get_code_text();
    if (strcmp(name, "set_code")      == 0) return cmd_set_code(args);
    if (strcmp(name, "open_file")     == 0) return cmd_open_file(args);
    if (strcmp(name, "save_file")     == 0) return cmd_save_file(args);
    if (strcmp(name, "select_lines")  == 0) return cmd_select_lines(args);
    if (strcmp(name, "insert_text")   == 0) return cmd_insert_text(args);

    /* SCAD */
    if (strcmp(name, "render_scad")   == 0) return cmd_render_scad(args);
    if (strcmp(name, "open_scad")     == 0) return cmd_open_scad(args);
    if (strcmp(name, "preview_render") == 0) return cmd_preview_render();

    /* GL viewport */
    if (strcmp(name, "gl_state")  == 0) return cmd_gl_state();
    if (strcmp(name, "gl_camera") == 0) return cmd_gl_camera(args);
    if (strcmp(name, "gl_reset")  == 0) return cmd_gl_reset();
    if (strcmp(name, "gl_ortho")  == 0) return cmd_gl_ortho();
    if (strcmp(name, "gl_grid")   == 0) return cmd_gl_grid();
    if (strcmp(name, "gl_axes")   == 0) return cmd_gl_axes();
    if (strcmp(name, "gl_select") == 0) return cmd_gl_select(args);
    if (strcmp(name, "gl_load")   == 0) return cmd_gl_load(args);
    if (strcmp(name, "gl_clear")  == 0) return cmd_gl_clear();

    /* Transform panel */
    if (strcmp(name, "transform_show") == 0) return cmd_transform_show(args);
    if (strcmp(name, "transform_hide") == 0) return cmd_transform_hide();

    /* Window */
    if (strcmp(name, "window_title")  == 0) return cmd_window_title(args);
    if (strcmp(name, "window_status") == 0) return cmd_window_status(args);
    if (strcmp(name, "window_size")   == 0) return cmd_window_size();

    /* Meta */
    if (strcmp(name, "help") == 0) return cmd_help();

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
dc_inspect_start(GtkWidget *window)
{
    if (!window) return -1;
    if (s_service) return -1;  /* already running */

    s_window = window;

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
    s_window  = NULL;
    unlink(DC_INSPECT_SOCK_PATH);
    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "inspect: stopped");
}
