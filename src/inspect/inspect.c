#define _POSIX_C_SOURCE 200809L
#include "inspect/inspect.h"
#include "ui/app_window.h"
#include "ui/code_editor.h"
#include "ui/scad_preview.h"
#include "ui/transform_panel.h"
#include "ui/eda_view.h"
#include "gl/gl_viewport.h"
#include "bezier/bezier_editor.h"
#include "bezier/bezier_canvas.h"
#include "scad/scad_runner.h"
#include "cubeiform/cubeiform_eda.h"
#include "eda/eda_schematic.h"
#include "eda/eda_pcb.h"
#include "eda/eda_cubeiform_export.h"
#include "eda_ui/sch_editor.h"
#include "eda_ui/sch_canvas.h"
#include "eda_ui/pcb_editor.h"
#include "eda_ui/pcb_canvas.h"
#include "eda_ui/sch_symbol_render.h"
#include "eda_ui/pcb_footprint_render.h"
#include "eda/eda_library.h"
#include "voxel/voxel.h"
#include "voxel/sdf.h"
#include "voxel/voxelize_stl.h"
#include "eda/eda_ratsnest.h"
#include "core/string_builder.h"
#include "core/error.h"
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
static DC_VoxelGrid    *s_voxel_grid = NULL;

/* Convenience accessors — all borrowed pointers, may be NULL */
static DC_BezierEditor  *get_editor(void)  { return dc_app_window_get_editor(s_window); }
static DC_CodeEditor    *get_code_ed(void) { return dc_app_window_get_code_editor(s_window); }
static DC_ScadPreview   *get_preview(void) { return dc_app_window_get_scad_preview(s_window); }
static DC_EdaView       *get_eda_view(void){ return dc_app_window_get_eda_view(s_window); }

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

static char *
cmd_render_status(void)
{
    DC_ScadPreview *pv = get_preview();
    if (!pv) return strdup("{\"error\":\"no scad preview\"}\n");

    const char *status = dc_scad_preview_get_status(pv);
    int busy = dc_scad_preview_is_rendering(pv);

    /* Build JSON response */
    size_t len = strlen(status) + 128;
    char *buf = malloc(len);
    snprintf(buf, len, "{\"rendering\":%s,\"status\":\"%s\"}\n",
             busy ? "true" : "false", status);
    return buf;
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
    DC_SelectMode smode = dc_gl_viewport_get_select_mode(vp);
    int sel_face = dc_gl_viewport_get_selected_face(vp);
    int sel_edge = dc_gl_viewport_get_selected_edge(vp);
    int wireframe = dc_gl_viewport_get_wireframe(vp);
    static const char *mode_names[] = {"object", "face", "edge"};

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) return strdup("{\"error\":\"alloc\"}\n");

    dc_sb_appendf(sb,
        "{\"camera\":{\"center\":[%.3f,%.3f,%.3f],"
        "\"dist\":%.3f,\"theta\":%.3f,\"phi\":%.3f,"
        "\"ortho\":%s},",
        cx, cy, cz, dist, theta, phi,
        ortho ? "true" : "false");

    dc_sb_appendf(sb,
        "\"display\":{\"grid\":%s,\"axes\":%s,\"wireframe\":%s},",
        grid ? "true" : "false",
        axes ? "true" : "false",
        wireframe ? "true" : "false");

    dc_sb_appendf(sb,
        "\"selection\":{\"mode\":\"%s\",\"object\":%d,"
        "\"face\":%d,\"edge\":%d},",
        mode_names[smode], sel, sel_face, sel_edge);

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
cmd_gl_select_mode(const char *args)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    if (!args || !*args) {
        /* No args = cycle mode */
        dc_gl_viewport_cycle_select_mode(vp);
    } else {
        char mode[16] = {0};
        sscanf(args, "%15s", mode);
        if (strcmp(mode, "object") == 0)
            dc_gl_viewport_set_select_mode(vp, DC_SEL_OBJECT);
        else if (strcmp(mode, "face") == 0)
            dc_gl_viewport_set_select_mode(vp, DC_SEL_FACE);
        else if (strcmp(mode, "edge") == 0)
            dc_gl_viewport_set_select_mode(vp, DC_SEL_EDGE);
        else
            return strdup("{\"error\":\"usage: gl_select_mode [object|face|edge]\"}\n");
    }

    static const char *names[] = {"object", "face", "edge"};
    DC_SelectMode m = dc_gl_viewport_get_select_mode(vp);
    char *buf = malloc(128);
    snprintf(buf, 128, "{\"ok\":true,\"mode\":\"%s\"}\n", names[m]);
    return buf;
}

static char *
cmd_gl_wireframe(void)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    dc_gl_viewport_toggle_wireframe(vp);
    int on = dc_gl_viewport_get_wireframe(vp);
    char *buf = malloc(64);
    snprintf(buf, 64, "{\"ok\":true,\"wireframe\":%s}\n", on ? "true" : "false");
    return buf;
}

static char *
cmd_gl_blocky(void)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    int cur = dc_gl_viewport_get_voxel_blocky(vp);
    dc_gl_viewport_set_voxel_blocky(vp, !cur);
    int on = dc_gl_viewport_get_voxel_blocky(vp);
    char *buf = malloc(64);
    snprintf(buf, 64, "{\"ok\":true,\"blocky\":%s}\n", on ? "true" : "false");
    return buf;
}

static char *
cmd_gl_topo(const char *args)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    int idx;
    if (!args || sscanf(args, "%d", &idx) != 1) {
        idx = dc_gl_viewport_get_selected(vp);
        if (idx < 0)
            return strdup("{\"error\":\"no object selected — usage: gl_topo [index]\"}\n");
    }

    int faces = dc_gl_viewport_get_face_count(vp, idx);
    int edges = dc_gl_viewport_get_edge_count(vp, idx);
    char *buf = malloc(128);
    snprintf(buf, 128, "{\"object\":%d,\"faces\":%d,\"edges\":%d}\n", idx, faces, edges);
    return buf;
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

static char *
cmd_gl_capture(const char *args)
{
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no gl viewport\"}\n");

    char path[512];
    if (!args || sscanf(args, "%511s", path) != 1)
        return strdup("{\"error\":\"usage: gl_capture <png_path>\"}\n");

    int rc = dc_gl_viewport_capture_png(vp, path);
    char *resp = malloc(640);
    if (!resp) return NULL;
    if (rc == 0)
        snprintf(resp, 640, "{\"ok\":true,\"path\":\"%s\"}\n", path);
    else
        snprintf(resp, 640, "{\"ok\":false,\"error\":\"capture failed\"}\n");
    return resp;
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
        "\"preview_render\","
        "\"render_status\""
        "],"

        "\"gl\":["
        "\"gl_state\","
        "\"gl_camera <cx> <cy> <cz> <dist> <theta> <phi>\","
        "\"gl_reset\","
        "\"gl_ortho\","
        "\"gl_grid\","
        "\"gl_axes\","
        "\"gl_select <index>\","
        "\"gl_select_mode [object|face|edge]\","
        "\"gl_wireframe\","
        "\"gl_topo [index]\","
        "\"gl_load <stl_path>\","
        "\"gl_clear\","
        "\"gl_capture <png_path>\""
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

/* =========================================================================
 * EDA / Cubeiform commands
 * ========================================================================= */

/* tab <name> — switch active tab */
static char *cmd_tab(const char *args) {
    if (!args || !*args)
        return strdup("{\"error\":\"usage: tab <3d_cad|eda|assembly>\"}\n");
    dc_app_window_set_tab(s_window, args);
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_appendf(sb, "{\"ok\":true,\"tab\":\"%s\"}\n", args);
    return dc_sb_take(sb);
}

/* tab_state — which tab is active */
static char *cmd_tab_state(void) {
    GtkWidget *stack = s_window ? g_object_get_data(G_OBJECT(s_window), "dc-stack") : NULL;
    if (!stack) return strdup("{\"error\":\"no stack\"}\n");
    const char *name = gtk_stack_get_visible_child_name(GTK_STACK(stack));
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_appendf(sb, "{\"tab\":\"%s\"}\n", name ? name : "unknown");
    return dc_sb_take(sb);
}

/* cubeiform_exec <source> — execute inline Cubeiform (any domain) */
static char *cmd_cubeiform_exec(const char *args) {
    if (!args || !*args)
        return strdup("{\"error\":\"usage: cubeiform_exec <source>\"}\n");

    DC_EdaView *ev = get_eda_view();
    DC_ESchematic *sch = NULL;
    if (ev) {
        DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
        sch = dc_sch_editor_get_schematic(ed);
    }

    DC_Error err = {0};
    DC_VoxelGrid *vox_grid = NULL;
    int rc = dc_cubeiform_execute_full(args, sch, NULL, &vox_grid, NULL, &err);
    if (rc != 0) {
        DC_StringBuilder *sb = dc_sb_new();
        dc_sb_appendf(sb, "{\"error\":\"%s\"}\n", err.message);
        dc_voxel_grid_free(vox_grid);
        return dc_sb_take(sb);
    }

    /* Refresh canvas */
    if (ev) {
        DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
        DC_SchCanvas *canvas = dc_sch_editor_get_canvas(ed);
        dc_sch_canvas_queue_redraw(canvas);
    }

    /* Display voxel grid in viewport */
    if (vox_grid) {
        DC_GlViewport *vp = get_viewport();
        if (vp) dc_gl_viewport_set_voxel_grid(vp, vox_grid);
        /* Store globally for lifetime */
        dc_voxel_grid_free(s_voxel_grid);
        s_voxel_grid = vox_grid;
    }

    return strdup("{\"ok\":true}\n");
}

/* cubeiform_validate <source> — parse without executing */
static char *cmd_cubeiform_validate(const char *args) {
    if (!args || !*args)
        return strdup("{\"error\":\"usage: cubeiform_validate <source>\"}\n");

    DC_Error err = {0};
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(args, &err);
    if (!eda) {
        DC_StringBuilder *sb = dc_sb_new();
        dc_sb_appendf(sb, "{\"valid\":false,\"error\":\"%s\"}\n", err.message);
        return dc_sb_take(sb);
    }

    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_appendf(sb, "{\"valid\":true,\"sch_ops\":%zu,\"pcb_ops\":%zu}\n",
                   dc_cubeiform_eda_sch_op_count(eda),
                   dc_cubeiform_eda_pcb_op_count(eda));
    dc_cubeiform_eda_free(eda);
    return dc_sb_take(sb);
}

/* sch_state — symbol/wire count, selected, zoom/pan */
static char *cmd_sch_state(void) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
    DC_ESchematic *sch = dc_sch_editor_get_schematic(ed);
    DC_SchCanvas *canvas = dc_sch_editor_get_canvas(ed);

    double px = 0, py = 0;
    dc_sch_canvas_get_pan(canvas, &px, &py);

    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_appendf(sb,
        "{\"symbols\":%zu,\"wires\":%zu,\"labels\":%zu,"
        "\"junctions\":%zu,\"power_ports\":%zu,"
        "\"selected\":%d,\"zoom\":%.3f,\"pan\":[%.1f,%.1f]}\n",
        dc_eschematic_symbol_count(sch),
        dc_eschematic_wire_count(sch),
        dc_eschematic_label_count(sch),
        dc_eschematic_junction_count(sch),
        dc_eschematic_power_port_count(sch),
        dc_sch_canvas_get_selected_symbol(canvas),
        dc_sch_canvas_get_zoom(canvas),
        px, py);
    return dc_sb_take(sb);
}

/* sch_load <path> — load .kicad_sch */
static char *cmd_sch_load(const char *args) {
    if (!args || !*args)
        return strdup("{\"error\":\"usage: sch_load <path>\"}\n");
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
    int rc = dc_sch_editor_load(ed, args);
    if (rc != 0) return strdup("{\"error\":\"load failed\"}\n");
    return strdup("{\"ok\":true}\n");
}

/* sch_save [path] — save current schematic */
static char *cmd_sch_save(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
    const char *path = (args && *args) ? args : NULL;
    int rc = dc_sch_editor_save(ed, path);
    if (rc != 0) return strdup("{\"error\":\"save failed\"}\n");
    return strdup("{\"ok\":true}\n");
}

/* sch_export_dcad [path] — export as Cubeiform source */
static char *cmd_sch_export_dcad(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
    DC_ESchematic *sch = dc_sch_editor_get_schematic(ed);

    DC_Error err = {0};
    char *dcad = dc_eschematic_to_cubeiform(sch, &err);
    if (!dcad) {
        DC_StringBuilder *sb = dc_sb_new();
        dc_sb_appendf(sb, "{\"error\":\"%s\"}\n", err.message);
        return dc_sb_take(sb);
    }

    if (args && *args) {
        /* Write to file */
        FILE *f = fopen(args, "w");
        if (!f) { free(dcad); return strdup("{\"error\":\"file write failed\"}\n"); }
        fputs(dcad, f);
        fclose(f);
        free(dcad);
        return strdup("{\"ok\":true}\n");
    }

    /* Return inline */
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_append(sb, "{\"dcad\":");
    sb_append_json_str(sb, dcad);
    dc_sb_append(sb, "}\n");
    free(dcad);
    return dc_sb_take(sb);
}

/* sch_add_symbol <lib_id> <x> <y> [angle] */
static char *cmd_sch_add_symbol(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    char lib_id[256];
    double x, y;
    int n = sscanf(args, "%255s %lf %lf", lib_id, &x, &y);
    if (n < 3) return strdup("{\"error\":\"usage: sch_add_symbol <lib_id> <x> <y>\"}\n");

    DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
    DC_ESchematic *sch = dc_sch_editor_get_schematic(ed);

    /* Auto-generate reference from lib_id */
    char ref[32];
    snprintf(ref, sizeof(ref), "U%zu", dc_eschematic_symbol_count(sch) + 1);

    size_t idx = dc_eschematic_add_symbol(sch, lib_id, ref, x, y);
    if (idx == (size_t)-1) return strdup("{\"error\":\"add failed\"}\n");

    dc_sch_canvas_queue_redraw(dc_sch_editor_get_canvas(ed));

    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_appendf(sb, "{\"ok\":true,\"ref\":\"%s\",\"index\":%zu}\n", ref, idx);
    return dc_sb_take(sb);
}

/* sch_add_wire <x1> <y1> <x2> <y2> */
static char *cmd_sch_add_wire(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    double x1, y1, x2, y2;
    if (sscanf(args, "%lf %lf %lf %lf", &x1, &y1, &x2, &y2) != 4)
        return strdup("{\"error\":\"usage: sch_add_wire <x1> <y1> <x2> <y2>\"}\n");

    DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
    dc_eschematic_add_wire(dc_sch_editor_get_schematic(ed), x1, y1, x2, y2);
    dc_sch_canvas_queue_redraw(dc_sch_editor_get_canvas(ed));
    return strdup("{\"ok\":true}\n");
}

/* sch_add_label <name> <x> <y> */
static char *cmd_sch_add_label(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    char name[256];
    double x, y;
    if (sscanf(args, "%255s %lf %lf", name, &x, &y) != 3)
        return strdup("{\"error\":\"usage: sch_add_label <name> <x> <y>\"}\n");

    DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
    dc_eschematic_add_label(dc_sch_editor_get_schematic(ed), name, x, y);
    dc_sch_canvas_queue_redraw(dc_sch_editor_get_canvas(ed));
    return strdup("{\"ok\":true}\n");
}

/* sch_select <type> <index> */
static char *cmd_sch_select(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    int index;
    if (sscanf(args, "%*s %d", &index) < 1)
        return strdup("{\"error\":\"usage: sch_select <type> <index>\"}\n");

    DC_SchEditor *ed = dc_eda_view_get_sch_editor(ev);
    dc_sch_canvas_set_selected_symbol(dc_sch_editor_get_canvas(ed), index);
    return strdup("{\"ok\":true}\n");
}

/* sch_zoom <level> */
static char *cmd_sch_zoom(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    double z;
    if (sscanf(args, "%lf", &z) != 1)
        return strdup("{\"error\":\"usage: sch_zoom <level>\"}\n");

    dc_sch_canvas_set_zoom(dc_sch_editor_get_canvas(dc_eda_view_get_sch_editor(ev)), z);
    return strdup("{\"ok\":true}\n");
}

/* sch_pan <x> <y> */
static char *cmd_sch_pan(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    double x, y;
    if (sscanf(args, "%lf %lf", &x, &y) != 2)
        return strdup("{\"error\":\"usage: sch_pan <x> <y>\"}\n");

    dc_sch_canvas_set_pan(dc_sch_editor_get_canvas(dc_eda_view_get_sch_editor(ev)), x, y);
    return strdup("{\"ok\":true}\n");
}

/* sch_render <path> [width] [height] */
static char *cmd_sch_render(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    char path[512];
    int w = 0, h = 0;
    sscanf(args, "%511s %d %d", path, &w, &h);

    DC_SchCanvas *canvas = dc_sch_editor_get_canvas(dc_eda_view_get_sch_editor(ev));
    int rc = dc_sch_canvas_render_to_png(canvas, path, w, h);
    if (rc != 0) return strdup("{\"error\":\"render failed\"}\n");
    return strdup("{\"ok\":true}\n");
}

/* =========================================================================
 * PCB commands
 * ========================================================================= */

static char *cmd_pcb_state(void) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");
    DC_PcbEditor *ed = dc_eda_view_get_pcb_editor(ev);
    DC_EPcb *pcb = dc_pcb_editor_get_pcb(ed);
    DC_PcbCanvas *canvas = dc_pcb_editor_get_canvas(ed);
    double px = 0, py = 0;
    dc_pcb_canvas_get_pan(canvas, &px, &py);
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_appendf(sb,
        "{\"footprints\":%zu,\"tracks\":%zu,\"vias\":%zu,"
        "\"zones\":%zu,\"nets\":%zu,"
        "\"selected\":%d,\"zoom\":%.3f,\"pan\":[%.2f,%.2f],"
        "\"active_layer\":%d}\n",
        dc_epcb_footprint_count(pcb), dc_epcb_track_count(pcb),
        dc_epcb_via_count(pcb), dc_epcb_zone_count(pcb),
        dc_epcb_net_count(pcb),
        dc_pcb_canvas_get_selected_footprint(canvas),
        dc_pcb_canvas_get_zoom(canvas), px, py,
        dc_pcb_canvas_get_active_layer(canvas));
    return dc_sb_take(sb);
}

static char *cmd_pcb_load(const char *args) {
    if (!args || !*args) return strdup("{\"error\":\"usage: pcb_load <path>\"}\n");
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");
    int rc = dc_pcb_editor_load(dc_eda_view_get_pcb_editor(ev), args);
    return rc == 0 ? strdup("{\"ok\":true}\n") : strdup("{\"error\":\"load failed\"}\n");
}

static char *cmd_pcb_save(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");
    int rc = dc_pcb_editor_save(dc_eda_view_get_pcb_editor(ev),
                                  (args && *args) ? args : NULL);
    return rc == 0 ? strdup("{\"ok\":true}\n") : strdup("{\"error\":\"save failed\"}\n");
}

static char *cmd_pcb_add_track(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");
    double x1, y1, x2, y2, w;
    int layer, net;
    if (sscanf(args, "%lf %lf %lf %lf %lf %d %d", &x1, &y1, &x2, &y2, &w, &layer, &net) < 7)
        return strdup("{\"error\":\"usage: pcb_add_track <x1> <y1> <x2> <y2> <width> <layer> <net_id>\"}\n");
    DC_EPcb *pcb = dc_pcb_editor_get_pcb(dc_eda_view_get_pcb_editor(ev));
    dc_epcb_add_track(pcb, x1, y1, x2, y2, w, layer, net);
    dc_pcb_canvas_queue_redraw(dc_pcb_editor_get_canvas(dc_eda_view_get_pcb_editor(ev)));
    return strdup("{\"ok\":true}\n");
}

static char *cmd_pcb_add_via(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");
    double x, y, sz, dr;
    int net;
    if (sscanf(args, "%lf %lf %lf %lf %d", &x, &y, &sz, &dr, &net) < 5)
        return strdup("{\"error\":\"usage: pcb_add_via <x> <y> <size> <drill> <net_id>\"}\n");
    DC_EPcb *pcb = dc_pcb_editor_get_pcb(dc_eda_view_get_pcb_editor(ev));
    dc_epcb_add_via(pcb, x, y, sz, dr, net);
    dc_pcb_canvas_queue_redraw(dc_pcb_editor_get_canvas(dc_eda_view_get_pcb_editor(ev)));
    return strdup("{\"ok\":true}\n");
}

static char *cmd_pcb_add_footprint(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");
    char lib_id[256], ref[64];
    double x, y;
    int layer;
    if (sscanf(args, "%255s %63s %lf %lf %d", lib_id, ref, &x, &y, &layer) < 5)
        return strdup("{\"error\":\"usage: pcb_add_footprint <lib_id> <ref> <x> <y> <layer>\"}\n");
    DC_EPcb *pcb = dc_pcb_editor_get_pcb(dc_eda_view_get_pcb_editor(ev));
    dc_epcb_add_footprint(pcb, lib_id, ref, x, y, layer);
    dc_pcb_canvas_queue_redraw(dc_pcb_editor_get_canvas(dc_eda_view_get_pcb_editor(ev)));
    return strdup("{\"ok\":true}\n");
}

static char *cmd_pcb_layer(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");
    int layer;
    if (sscanf(args, "%d", &layer) != 1)
        return strdup("{\"error\":\"usage: pcb_layer <layer_id>\"}\n");
    dc_pcb_canvas_set_active_layer(dc_pcb_editor_get_canvas(dc_eda_view_get_pcb_editor(ev)), layer);
    return strdup("{\"ok\":true}\n");
}

static char *cmd_pcb_layer_toggle(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");
    int layer;
    if (sscanf(args, "%d", &layer) != 1)
        return strdup("{\"error\":\"usage: pcb_layer_toggle <layer_id>\"}\n");
    DC_PcbCanvas *canvas = dc_pcb_editor_get_canvas(dc_eda_view_get_pcb_editor(ev));
    int cur = dc_pcb_canvas_get_layer_visible(canvas, layer);
    dc_pcb_canvas_set_layer_visible(canvas, layer, !cur);
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_appendf(sb, "{\"ok\":true,\"layer\":%d,\"visible\":%s}\n", layer, !cur ? "true" : "false");
    return dc_sb_take(sb);
}

static char *cmd_pcb_ratsnest(void) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");
    DC_PcbEditor *ed = dc_eda_view_get_pcb_editor(ev);
    dc_pcb_editor_update_ratsnest(ed);

    /* Build JSON response with ratsnest lines */
    DC_EPcb *pcb = dc_pcb_editor_get_pcb(ed);
    DC_Ratsnest *rn = dc_ratsnest_compute(pcb);
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_appendf(sb, "{\"lines\":%zu,\"incomplete_nets\":%zu}\n",
                   dc_ratsnest_line_count(rn),
                   dc_ratsnest_incomplete_net_count(rn));
    dc_ratsnest_free(rn);
    return dc_sb_take(sb);
}

static char *cmd_pcb_import_netlist(void) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    DC_SchEditor *sch_ed = dc_eda_view_get_sch_editor(ev);
    DC_ESchematic *sch = dc_sch_editor_get_schematic(sch_ed);

    DC_Error err = {0};
    DC_Netlist *nl = dc_eschematic_generate_netlist(sch, &err);
    if (!nl) {
        DC_StringBuilder *sb = dc_sb_new();
        dc_sb_appendf(sb, "{\"error\":\"%s\"}\n", err.message);
        return dc_sb_take(sb);
    }

    DC_PcbEditor *pcb_ed = dc_eda_view_get_pcb_editor(ev);
    DC_EPcb *pcb = dc_pcb_editor_get_pcb(pcb_ed);
    dc_epcb_import_netlist(pcb, nl, &err);
    dc_netlist_free(nl);

    dc_pcb_canvas_queue_redraw(dc_pcb_editor_get_canvas(pcb_ed));
    dc_pcb_editor_update_ratsnest(pcb_ed);

    return strdup("{\"ok\":true}\n");
}

static char *cmd_pcb_export_dcad(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");
    DC_EPcb *pcb = dc_pcb_editor_get_pcb(dc_eda_view_get_pcb_editor(ev));
    DC_Error err = {0};
    char *dcad = dc_epcb_to_cubeiform(pcb, &err);
    if (!dcad) return strdup("{\"error\":\"export failed\"}\n");

    if (args && *args) {
        FILE *f = fopen(args, "w");
        if (!f) { free(dcad); return strdup("{\"error\":\"write failed\"}\n"); }
        fputs(dcad, f);
        fclose(f);
        free(dcad);
        return strdup("{\"ok\":true}\n");
    }
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_append(sb, "{\"dcad\":");
    sb_append_json_str(sb, dcad);
    dc_sb_append(sb, "}\n");
    free(dcad);
    return dc_sb_take(sb);
}

/* =========================================================================
 * EDA Library / Browser / Preview inspect commands
 * ========================================================================= */

/* pcb_render <path> [width] [height] */
static char *cmd_pcb_render(const char *args) {
    DC_EdaView *ev = get_eda_view();
    if (!ev) return strdup("{\"error\":\"no eda view\"}\n");

    char path[512];
    int w = 0, h = 0;
    sscanf(args, "%511s %d %d", path, &w, &h);

    DC_PcbCanvas *canvas = dc_pcb_editor_get_canvas(dc_eda_view_get_pcb_editor(ev));
    int rc = dc_pcb_canvas_render_to_png(canvas, path, w, h);
    if (rc != 0) return strdup("{\"error\":\"pcb render failed\"}\n");

    char *resp = malloc(640);
    snprintf(resp, 640, "{\"ok\":true,\"path\":\"%s\"}\n", path);
    return resp;
}

/* eda_lib_list — JSON array of available library names */
static char *cmd_eda_lib_list(void) {
    DC_ELibrary *lib = dc_app_window_get_library();
    if (!lib) return strdup("{\"error\":\"no library\"}\n");

    size_t count = dc_elibrary_lib_count(lib);
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_append(sb, "{\"libraries\":[");
    for (size_t i = 0; i < count; i++) {
        if (i > 0) dc_sb_append(sb, ",");
        const char *name = dc_elibrary_lib_name(lib, i);
        sb_append_json_str(sb, name ? name : "");
    }
    dc_sb_appendf(sb, "],\"count\":%zu}\n", count);
    return dc_sb_take(sb);
}

/* eda_lib_symbols <lib_name> — JSON array of symbol names in a library */
static char *cmd_eda_lib_symbols(const char *args) {
    DC_ELibrary *lib = dc_app_window_get_library();
    if (!lib) return strdup("{\"error\":\"no library\"}\n");
    if (!args || !*args) return strdup("{\"error\":\"usage: eda_lib_symbols <lib_name>\"}\n");

    char lib_name[256];
    sscanf(args, "%255s", lib_name);

    size_t count = dc_elibrary_lib_symbol_count(lib, lib_name);
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_appendf(sb, "{\"library\":\"%s\",\"symbols\":[", lib_name);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) dc_sb_append(sb, ",");
        const char *name = dc_elibrary_lib_symbol_name(lib, lib_name, i);
        sb_append_json_str(sb, name ? name : "");
    }
    dc_sb_appendf(sb, "],\"count\":%zu}\n", count);
    return dc_sb_take(sb);
}

/* eda_sym_preview <lib_id> [path] [width] [height]
 * Render a symbol to PNG using standalone preview renderer. */
static char *cmd_eda_sym_preview(const char *args) {
    DC_ELibrary *lib = dc_app_window_get_library();
    if (!lib) return strdup("{\"error\":\"no library\"}\n");

    char lib_id[256];
    char path[512] = "/tmp/duncad-sym-preview.png";
    int w = 400, h = 300;
    int n = sscanf(args, "%255s %511s %d %d", lib_id, path, &w, &h);
    if (n < 1 || !*lib_id)
        return strdup("{\"error\":\"usage: eda_sym_preview <lib_id> [path] [w] [h]\"}\n");

    const DC_Sexpr *sym = dc_elibrary_find_symbol(lib, lib_id);
    if (!sym) {
        char *resp = malloc(512);
        snprintf(resp, 512, "{\"error\":\"symbol not found: %s\"}\n", lib_id);
        return resp;
    }

    /* Render to Cairo image surface (RGB24 — no alpha complications) */
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t *cr = cairo_create(surf);

    /* Dark background */
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.14);
    cairo_paint(cr);

    dc_sch_symbol_render_preview_ex(cr, sym, lib, 0, 0, (double)w, (double)h);

    cairo_destroy(cr);
    cairo_surface_write_to_png(surf, path);
    cairo_surface_destroy(surf);

    char *resp = malloc(1024);
    snprintf(resp, 1024, "{\"ok\":true,\"path\":\"%s\",\"lib_id\":\"%s\"}\n", path, lib_id);
    return resp;
}

/* eda_fp_preview <lib_id> [path] [width] [height]
 * Render a footprint to PNG using standalone preview renderer. */
static char *cmd_eda_fp_preview(const char *args) {
    DC_ELibrary *lib = dc_app_window_get_library();
    if (!lib) return strdup("{\"error\":\"no library\"}\n");

    char lib_id[256];
    char path[512] = "/tmp/duncad-fp-preview.png";
    int w = 400, h = 300;
    int n = sscanf(args, "%255s %511s %d %d", lib_id, path, &w, &h);
    if (n < 1 || !*lib_id)
        return strdup("{\"error\":\"usage: eda_fp_preview <lib_id> [path] [w] [h]\"}\n");

    const DC_Sexpr *fp = dc_elibrary_find_footprint(lib, lib_id);
    if (!fp) {
        char *resp = malloc(512);
        snprintf(resp, 512, "{\"error\":\"footprint not found: %s\"}\n", lib_id);
        return resp;
    }

    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t *cr = cairo_create(surf);

    cairo_set_source_rgb(cr, 0.1, 0.15, 0.1);
    cairo_paint(cr);

    dc_pcb_footprint_render_preview(cr, fp, 0, 0, (double)w, (double)h);

    cairo_destroy(cr);
    cairo_surface_write_to_png(surf, path);
    cairo_surface_destroy(surf);

    char *resp = malloc(1024);
    snprintf(resp, 1024, "{\"ok\":true,\"path\":\"%s\",\"lib_id\":\"%s\"}\n", path, lib_id);
    return resp;
}

/* eda_sym_info <lib_id> — symbol properties + pin count */
static char *cmd_eda_sym_info(const char *args) {
    DC_ELibrary *lib = dc_app_window_get_library();
    if (!lib) return strdup("{\"error\":\"no library\"}\n");

    char lib_id[256];
    if (!args || sscanf(args, "%255s", lib_id) != 1)
        return strdup("{\"error\":\"usage: eda_sym_info <lib_id>\"}\n");

    const DC_Sexpr *sym = dc_elibrary_find_symbol(lib, lib_id);
    if (!sym) return strdup("{\"error\":\"symbol not found\"}\n");

    const char *desc = dc_elibrary_symbol_property(sym, "Description");
    const char *ref  = dc_elibrary_symbol_property(sym, "Reference");
    const char *fp   = dc_elibrary_symbol_property(sym, "Footprint");
    const char *val  = dc_elibrary_symbol_property(sym, "Value");
    size_t pins = dc_elibrary_symbol_pin_count(sym);

    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_append(sb, "{");
    dc_sb_append(sb, "\"lib_id\":"); sb_append_json_str(sb, lib_id);
    dc_sb_appendf(sb, ",\"pins\":%zu", pins);
    dc_sb_append(sb, ",\"reference\":"); sb_append_json_str(sb, ref ? ref : "");
    dc_sb_append(sb, ",\"value\":"); sb_append_json_str(sb, val ? val : "");
    dc_sb_append(sb, ",\"footprint\":"); sb_append_json_str(sb, fp ? fp : "");
    dc_sb_append(sb, ",\"description\":"); sb_append_json_str(sb, desc ? desc : "");
    dc_sb_append(sb, "}\n");
    return dc_sb_take(sb);
}

/* eda_fp_lib_list — JSON array of footprint library names (loaded + pending) */
static char *cmd_eda_fp_lib_list(void) {
    DC_ELibrary *lib = dc_app_window_get_library();
    if (!lib) return strdup("{\"error\":\"no library\"}\n");

    size_t count = dc_elibrary_fp_lib_count(lib);
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_append(sb, "{\"libraries\":[");
    for (size_t i = 0; i < count; i++) {
        if (i > 0) dc_sb_append(sb, ",");
        const char *name = dc_elibrary_fp_lib_name(lib, i);
        sb_append_json_str(sb, name ? name : "");
    }
    dc_sb_appendf(sb, "],\"count\":%zu}\n", count);
    return dc_sb_take(sb);
}

/* eda_fp_lib_footprints <lib_name> — list footprints in a library (triggers lazy load) */
static char *cmd_eda_fp_lib_footprints(const char *args) {
    DC_ELibrary *lib = dc_app_window_get_library();
    if (!lib) return strdup("{\"error\":\"no library\"}\n");
    if (!args || !*args) return strdup("{\"error\":\"usage: eda_fp_lib_footprints <lib_name>\"}\n");

    char lib_name[256];
    sscanf(args, "%255s", lib_name);

    /* Trigger lazy load by looking up a dummy footprint in this lib */
    char dummy_id[512];
    snprintf(dummy_id, sizeof(dummy_id), "%s:__trigger_load__", lib_name);
    dc_elibrary_find_footprint(lib, dummy_id);

    size_t total = dc_elibrary_footprint_count(lib);
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_appendf(sb, "{\"library\":\"%s\",\"footprints\":[", lib_name);
    int added = 0;
    for (size_t i = 0; i < total; i++) {
        const char *lname = dc_elibrary_footprint_lib_name(lib, i);
        if (!lname || strcmp(lname, lib_name) != 0) continue;
        if (added > 0) dc_sb_append(sb, ",");
        const char *name = dc_elibrary_footprint_name(lib, i);
        sb_append_json_str(sb, name ? name : "");
        added++;
    }
    dc_sb_appendf(sb, "],\"count\":%d}\n", added);
    return dc_sb_take(sb);
}

/* eda_fp_list — JSON array of loaded footprint lib:name entries */
static char *cmd_eda_fp_list(void) {
    DC_ELibrary *lib = dc_app_window_get_library();
    if (!lib) return strdup("{\"error\":\"no library\"}\n");

    size_t count = dc_elibrary_footprint_count(lib);
    DC_StringBuilder *sb = dc_sb_new();
    dc_sb_append(sb, "{\"footprints\":[");
    for (size_t i = 0; i < count; i++) {
        if (i > 0) dc_sb_append(sb, ",");
        const char *name = dc_elibrary_footprint_name(lib, i);
        const char *lname = dc_elibrary_footprint_lib_name(lib, i);
        dc_sb_append(sb, "{\"name\":");
        sb_append_json_str(sb, name ? name : "");
        dc_sb_append(sb, ",\"lib\":");
        sb_append_json_str(sb, lname ? lname : "");
        dc_sb_append(sb, "}");
    }
    dc_sb_appendf(sb, "],\"count\":%zu}\n", count);
    return dc_sb_take(sb);
}

/* =========================================================================
 * Voxel commands
 * ========================================================================= */

/* voxel_sphere <cx> <cy> <cz> <radius> [resolution] [cell_size]
 * Create a sphere SDF, activate, color by normal, display in viewport. */
static char *cmd_voxel_sphere(const char *args) {
    float cx = 0, cy = 0, cz = 0, radius = 10;
    int res = 32;
    float cs = 1.0f;
    sscanf(args, "%f %f %f %f %d %f", &cx, &cy, &cz, &radius, &res, &cs);

    dc_voxel_grid_free(s_voxel_grid);
    s_voxel_grid = dc_voxel_grid_new(res, res, res, cs);
    if (!s_voxel_grid) return strdup("{\"error\":\"grid alloc failed\"}\n");

    dc_sdf_sphere(s_voxel_grid, cx, cy, cz, radius);
    dc_sdf_activate_color(s_voxel_grid, 200, 80, 80);
    dc_sdf_color_by_normal(s_voxel_grid);

    /* Display in viewport */
    DC_GlViewport *vp = get_viewport();
    if (vp)
        dc_gl_viewport_set_voxel_grid(vp, s_voxel_grid);

    size_t active = dc_voxel_grid_active_count(s_voxel_grid);
    char *resp = malloc(256);
    snprintf(resp, 256, "{\"ok\":true,\"active\":%zu,\"resolution\":%d,\"cell_size\":%.2f}\n",
             active, res, cs);
    return resp;
}

/* voxel_box <x0> <y0> <z0> <x1> <y1> <z1> [resolution] [cell_size] */
static char *cmd_voxel_box(const char *args) {
    float x0 = 0, y0 = 0, z0 = 0, x1 = 10, y1 = 10, z1 = 10;
    int res = 32;
    float cs = 1.0f;
    sscanf(args, "%f %f %f %f %f %f %d %f", &x0, &y0, &z0, &x1, &y1, &z1, &res, &cs);

    dc_voxel_grid_free(s_voxel_grid);
    s_voxel_grid = dc_voxel_grid_new(res, res, res, cs);
    if (!s_voxel_grid) return strdup("{\"error\":\"grid alloc failed\"}\n");

    dc_sdf_box(s_voxel_grid, x0, y0, z0, x1, y1, z1);
    dc_sdf_activate_color(s_voxel_grid, 80, 80, 200);
    dc_sdf_color_by_normal(s_voxel_grid);

    DC_GlViewport *vp = get_viewport();
    if (vp)
        dc_gl_viewport_set_voxel_grid(vp, s_voxel_grid);

    size_t active = dc_voxel_grid_active_count(s_voxel_grid);
    char *resp = malloc(256);
    snprintf(resp, 256, "{\"ok\":true,\"active\":%zu}\n", active);
    return resp;
}

/* voxel_csg <op> — combine current grid with a sphere.
 * op: "union", "subtract", "intersect"
 * Adds a sphere at grid center with radius = grid_size/4 */
static char *cmd_voxel_csg(const char *args) {
    if (!s_voxel_grid) return strdup("{\"error\":\"no voxel grid — create one first\"}\n");
    if (!args || !*args) return strdup("{\"error\":\"usage: voxel_csg union|subtract|intersect\"}\n");

    char op[32];
    sscanf(args, "%31s", op);

    int res = dc_voxel_grid_size_x(s_voxel_grid);
    float cs = dc_voxel_grid_cell_size(s_voxel_grid);
    float center = res * cs * 0.5f;
    float radius = res * cs * 0.25f;

    DC_VoxelGrid *b = dc_voxel_grid_new(res, res, res, cs);
    if (!b) return strdup("{\"error\":\"alloc failed\"}\n");
    dc_sdf_sphere(b, center, center, center, radius);

    DC_VoxelGrid *out = dc_voxel_grid_new(res, res, res, cs);
    if (!out) { dc_voxel_grid_free(b); return strdup("{\"error\":\"alloc failed\"}\n"); }

    int rc;
    if (strcmp(op, "union") == 0)          rc = dc_sdf_union(s_voxel_grid, b, out);
    else if (strcmp(op, "subtract") == 0)  rc = dc_sdf_subtract(s_voxel_grid, b, out);
    else if (strcmp(op, "intersect") == 0) rc = dc_sdf_intersect(s_voxel_grid, b, out);
    else { dc_voxel_grid_free(b); dc_voxel_grid_free(out);
           return strdup("{\"error\":\"unknown op — use union|subtract|intersect\"}\n"); }

    dc_voxel_grid_free(b);
    if (rc != 0) { dc_voxel_grid_free(out); return strdup("{\"error\":\"CSG failed\"}\n"); }

    dc_sdf_activate(out);
    dc_sdf_color_by_normal(out);

    dc_voxel_grid_free(s_voxel_grid);
    s_voxel_grid = out;

    DC_GlViewport *vp = get_viewport();
    if (vp) dc_gl_viewport_set_voxel_grid(vp, s_voxel_grid);

    size_t active = dc_voxel_grid_active_count(s_voxel_grid);
    char *resp = malloc(256);
    snprintf(resp, 256, "{\"ok\":true,\"op\":\"%s\",\"active\":%zu}\n", op, active);
    return resp;
}

/* voxel_clear — remove voxels from viewport */
static char *cmd_voxel_clear(void) {
    DC_GlViewport *vp = get_viewport();
    if (vp) dc_gl_viewport_set_voxel_grid(vp, NULL);
    dc_voxel_grid_free(s_voxel_grid);
    s_voxel_grid = NULL;
    return strdup("{\"ok\":true}\n");
}

/* voxel_resolution [value] — get or set voxel resolution */
static char *cmd_voxel_resolution(const char *args) {
    DC_ScadPreview *pv = get_preview();
    if (!pv) return strdup("{\"error\":\"no scad preview\"}\n");

    if (args && *args) {
        int res = atoi(args);
        if (res >= 8 && res <= 512) {
            dc_scad_preview_set_voxel_resolution(pv, res);
            char *resp = malloc(128);
            snprintf(resp, 128, "{\"ok\":true,\"resolution\":%d}\n", res);
            return resp;
        }
        return strdup("{\"error\":\"resolution must be 8-512\"}\n");
    }

    int res = dc_scad_preview_get_voxel_resolution(pv);
    char *resp = malloc(128);
    snprintf(resp, 128, "{\"resolution\":%d}\n", res);
    return resp;
}

/* voxel_blocky [0|1] — toggle blocky/smooth rendering */
static char *cmd_voxel_blocky(const char *args) {
    DC_GlViewport *vp = get_viewport();
    if (!vp) return strdup("{\"error\":\"no viewport\"}\n");

    /* Get the voxel buf from the viewport — we need to expose it or
     * store a reference. For now, use the global s_voxel_grid approach:
     * the voxel_buf is managed by the viewport. We need a direct accessor. */

    /* Simpler: store blocky state, apply on next upload */
    if (args && *args) {
        int val = atoi(args);
        /* We need access to the DC_GlVoxelBuf. Access via viewport internals
         * is not clean. Let's add a viewport-level API. */
        dc_gl_viewport_set_voxel_blocky(vp, val);
        char *resp = malloc(128);
        snprintf(resp, 128, "{\"ok\":true,\"blocky\":%d}\n", val ? 1 : 0);
        return resp;
    }
    int val = dc_gl_viewport_get_voxel_blocky(vp);
    char *resp = malloc(128);
    snprintf(resp, 128, "{\"blocky\":%d}\n", val);
    return resp;
}

/* voxel_state — info about current voxel grid */
static char *cmd_voxel_state(void) {
    if (!s_voxel_grid) return strdup("{\"loaded\":false}\n");
    int sx = dc_voxel_grid_size_x(s_voxel_grid);
    int sy = dc_voxel_grid_size_y(s_voxel_grid);
    int sz = dc_voxel_grid_size_z(s_voxel_grid);
    float cs = dc_voxel_grid_cell_size(s_voxel_grid);
    size_t active = dc_voxel_grid_active_count(s_voxel_grid);
    char *resp = malloc(256);
    snprintf(resp, 256,
             "{\"loaded\":true,\"size\":[%d,%d,%d],\"cell_size\":%.3f,"
             "\"active\":%zu,\"total\":%d}\n",
             sx, sy, sz, cs, active, sx*sy*sz);
    return resp;
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
    if (strcmp(name, "render_status")  == 0) return cmd_render_status();

    /* GL viewport */
    if (strcmp(name, "gl_state")  == 0) return cmd_gl_state();
    if (strcmp(name, "gl_camera") == 0) return cmd_gl_camera(args);
    if (strcmp(name, "gl_reset")  == 0) return cmd_gl_reset();
    if (strcmp(name, "gl_ortho")  == 0) return cmd_gl_ortho();
    if (strcmp(name, "gl_grid")   == 0) return cmd_gl_grid();
    if (strcmp(name, "gl_axes")   == 0) return cmd_gl_axes();
    if (strcmp(name, "gl_select") == 0) return cmd_gl_select(args);
    if (strcmp(name, "gl_select_mode") == 0) return cmd_gl_select_mode(args);
    if (strcmp(name, "gl_wireframe") == 0) return cmd_gl_wireframe();
    if (strcmp(name, "gl_blocky") == 0) return cmd_gl_blocky();
    if (strcmp(name, "gl_topo")   == 0) return cmd_gl_topo(args);
    if (strcmp(name, "gl_load")   == 0) return cmd_gl_load(args);
    if (strcmp(name, "gl_clear")  == 0) return cmd_gl_clear();
    if (strcmp(name, "gl_capture") == 0) return cmd_gl_capture(args);

    /* Transform panel */
    if (strcmp(name, "transform_show") == 0) return cmd_transform_show(args);
    if (strcmp(name, "transform_hide") == 0) return cmd_transform_hide();

    /* Window */
    if (strcmp(name, "window_title")  == 0) return cmd_window_title(args);
    if (strcmp(name, "window_status") == 0) return cmd_window_status(args);
    if (strcmp(name, "window_size")   == 0) return cmd_window_size();

    /* Shape menu test */
    if (strcmp(name, "shape_action") == 0) {
        DC_GlViewport *vp = get_viewport();
        if (!vp) return strdup("{\"error\":\"no viewport\"}\n");
        GtkWidget *gl_w = dc_gl_viewport_widget(vp);
        GActionGroup *sg = g_object_get_data(G_OBJECT(gl_w), "dc-shape-action-group");
        if (!sg) return strdup("{\"error\":\"no shape action group\"}\n");
        if (!args || !*args) return strdup("{\"error\":\"usage: shape_action <name>\"}\n");
        g_action_group_activate_action(sg, args, NULL);
        char *r = malloc(128);
        snprintf(r, 128, "{\"ok\":true,\"action\":\"%s\"}\n", args);
        return r;
    }

    /* Tab system */
    if (strcmp(name, "tab")       == 0) return cmd_tab(args);
    if (strcmp(name, "tab_state") == 0) return cmd_tab_state();

    /* Cubeiform */
    if (strcmp(name, "cubeiform_exec")     == 0) return cmd_cubeiform_exec(args);
    if (strcmp(name, "cubeiform_validate") == 0) return cmd_cubeiform_validate(args);

    /* Schematic */
    if (strcmp(name, "sch_state")       == 0) return cmd_sch_state();
    if (strcmp(name, "sch_load")        == 0) return cmd_sch_load(args);
    if (strcmp(name, "sch_save")        == 0) return cmd_sch_save(args);
    if (strcmp(name, "sch_export_dcad") == 0) return cmd_sch_export_dcad(args);
    if (strcmp(name, "sch_add_symbol")  == 0) return cmd_sch_add_symbol(args);
    if (strcmp(name, "sch_add_wire")    == 0) return cmd_sch_add_wire(args);
    if (strcmp(name, "sch_add_label")   == 0) return cmd_sch_add_label(args);
    if (strcmp(name, "sch_select")      == 0) return cmd_sch_select(args);
    if (strcmp(name, "sch_zoom")        == 0) return cmd_sch_zoom(args);
    if (strcmp(name, "sch_pan")         == 0) return cmd_sch_pan(args);
    if (strcmp(name, "sch_render")      == 0) return cmd_sch_render(args);

    /* PCB */
    if (strcmp(name, "pcb_state")          == 0) return cmd_pcb_state();
    if (strcmp(name, "pcb_load")           == 0) return cmd_pcb_load(args);
    if (strcmp(name, "pcb_save")           == 0) return cmd_pcb_save(args);
    if (strcmp(name, "pcb_add_track")      == 0) return cmd_pcb_add_track(args);
    if (strcmp(name, "pcb_add_via")        == 0) return cmd_pcb_add_via(args);
    if (strcmp(name, "pcb_add_footprint")  == 0) return cmd_pcb_add_footprint(args);
    if (strcmp(name, "pcb_layer")          == 0) return cmd_pcb_layer(args);
    if (strcmp(name, "pcb_layer_toggle")   == 0) return cmd_pcb_layer_toggle(args);
    if (strcmp(name, "pcb_ratsnest")       == 0) return cmd_pcb_ratsnest();
    if (strcmp(name, "pcb_import_netlist") == 0) return cmd_pcb_import_netlist();
    if (strcmp(name, "pcb_export_dcad")    == 0) return cmd_pcb_export_dcad(args);
    if (strcmp(name, "pcb_render")         == 0) return cmd_pcb_render(args);

    /* EDA Library / Browser / Preview */
    if (strcmp(name, "eda_lib_list")       == 0) return cmd_eda_lib_list();
    if (strcmp(name, "eda_lib_symbols")    == 0) return cmd_eda_lib_symbols(args);
    if (strcmp(name, "eda_sym_preview")    == 0) return cmd_eda_sym_preview(args);
    if (strcmp(name, "eda_sym_info")       == 0) return cmd_eda_sym_info(args);
    if (strcmp(name, "eda_fp_preview")     == 0) return cmd_eda_fp_preview(args);
    if (strcmp(name, "eda_fp_list")        == 0) return cmd_eda_fp_list();
    if (strcmp(name, "eda_fp_lib_list")    == 0) return cmd_eda_fp_lib_list();
    if (strcmp(name, "eda_fp_lib_footprints") == 0) return cmd_eda_fp_lib_footprints(args);

    /* Voxel */
    if (strcmp(name, "voxel_sphere")       == 0) return cmd_voxel_sphere(args);
    if (strcmp(name, "voxel_box")          == 0) return cmd_voxel_box(args);
    if (strcmp(name, "voxel_csg")          == 0) return cmd_voxel_csg(args);
    if (strcmp(name, "voxel_clear")        == 0) return cmd_voxel_clear();
    if (strcmp(name, "voxel_state")        == 0) return cmd_voxel_state();
    if (strcmp(name, "voxel_resolution")   == 0) return cmd_voxel_resolution(args);
    if (strcmp(name, "voxel_blocky")      == 0) return cmd_voxel_blocky(args);

    /* Meta */
    if (strcmp(name, "help") == 0) return cmd_help();

    char *err = malloc(256);
    if (!err) return NULL;
    snprintf(err, 256, "{\"error\":\"unknown command: %s\"}\n", name);
    return err;
}

/* Public in-process dispatch (for embedded terminal). */
char *
dc_inspect_dispatch(const char *cmd)
{
    return dispatch(cmd);
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

    /* Read full command (may exceed 4K for large set_code payloads) */
    gsize buf_cap = 8192;
    gsize buf_len = 0;
    char *buf = malloc(buf_cap);
    if (!buf) return TRUE;

    for (;;) {
        gssize n = g_input_stream_read(in, buf + buf_len,
                                       buf_cap - buf_len - 1, NULL, NULL);
        if (n <= 0) break;
        buf_len += (gsize)n;
        if ((gsize)n < buf_cap - buf_len - 1) break; /* got less than asked = done */
        buf_cap *= 2;
        char *tmp = realloc(buf, buf_cap);
        if (!tmp) break;
        buf = tmp;
    }
    if (buf_len == 0) { free(buf); return TRUE; }
    buf[buf_len] = '\0';
    gssize n = (gssize)buf_len;

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

    free(buf);
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
