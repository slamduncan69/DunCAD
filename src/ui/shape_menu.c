#include "ui/shape_menu.h"
#include "ui/code_editor.h"
#include "ui/transform_panel.h"
#include "ui/scad_preview.h"
#include "bezier/bezier_editor.h"
#include "gl/gl_viewport.h"
#include "gl/edge_profile.h"
#include "core/log.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

/* -------------------------------------------------------------------------
 * Context state — attached to the viewport widget via g_object_set_data
 * ---------------------------------------------------------------------- */
typedef struct {
    DC_CodeEditor     *code_ed;    /* borrowed */
    DC_GlViewport     *gl_vp;     /* borrowed */
    DC_TransformPanel *transform; /* borrowed, may be NULL */
    DC_ScadPreview    *preview;   /* borrowed, may be NULL */
    DC_BezierEditor   *bez_ed;   /* borrowed, may be NULL */
    GtkWidget         *popover;   /* owned by GTK (attached to widget) */
    double             click_x;   /* right-click position */
    double             click_y;
    int                sel_obj;        /* captured at right-click time */
    int                sel_line_start;
    int                sel_line_end;
} ShapeMenuCtx;

/* -------------------------------------------------------------------------
 * Shape insertion — appends SCAD code at end of editor
 * ---------------------------------------------------------------------- */

static void
insert_shape(ShapeMenuCtx *ctx, const char *scad)
{
    if (!ctx->code_ed) return;

    /* Get existing text */
    char *text = dc_code_editor_get_text(ctx->code_ed);
    size_t old_len = text ? strlen(text) : 0;

    /* Build new text: existing + newline + shape */
    size_t shape_len = strlen(scad);
    char *buf = malloc(old_len + shape_len + 3);
    if (!buf) { free(text); return; }

    if (text && old_len > 0) {
        memcpy(buf, text, old_len);
        /* Ensure trailing newline */
        if (buf[old_len - 1] != '\n')
            buf[old_len++] = '\n';
        buf[old_len++] = '\n';
    }
    memcpy(buf + old_len, scad, shape_len);
    buf[old_len + shape_len] = '\n';
    buf[old_len + shape_len + 1] = '\0';

    dc_code_editor_set_text(ctx->code_ed, buf);
    free(buf);
    free(text);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "shape_menu: inserted shape");
}

/* ---- Action callbacks for Insert Shape ---- */

static void
on_insert_cube(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    insert_shape(data, "cube([10, 10, 10], center=true);");
}

static void
on_insert_sphere(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    insert_shape(data, "sphere(d=10);");
}

static void
on_insert_cylinder(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    insert_shape(data, "cylinder(d=10, h=20, center=true);");
}

static void
on_insert_tetrahedron(GSimpleAction *action, GVariant *param, gpointer data)
{ (void)action; (void)param; insert_shape(data, "tetrahedron(r=10);"); }

static void
on_insert_octahedron(GSimpleAction *action, GVariant *param, gpointer data)
{ (void)action; (void)param; insert_shape(data, "octahedron(r=10);"); }

static void
on_insert_dodecahedron(GSimpleAction *action, GVariant *param, gpointer data)
{ (void)action; (void)param; insert_shape(data, "dodecahedron(r=10);"); }

static void
on_insert_icosahedron(GSimpleAction *action, GVariant *param, gpointer data)
{ (void)action; (void)param; insert_shape(data, "icosahedron(r=10);"); }

/* -------------------------------------------------------------------------
 * Shape modification — wraps selected object's code
 * ---------------------------------------------------------------------- */

static int
get_selected_line_range(ShapeMenuCtx *ctx, int *line_start, int *line_end)
{
    /* Use captured selection from right-click if available */
    if (ctx->sel_obj >= 0) {
        *line_start = ctx->sel_line_start;
        *line_end = ctx->sel_line_end;
        dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP,
               "shape_menu: using captured sel=%d lines=%d..%d",
               ctx->sel_obj, *line_start, *line_end);
        return 1;
    }

    /* Fall back to querying viewport directly */
    if (!ctx->gl_vp) return 0;
    int sel = dc_gl_viewport_get_selected(ctx->gl_vp);
    if (sel < 0) {
        dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP,
               "shape_menu: no selection (sel=%d)", sel);
        return 0;
    }
    int rc = dc_gl_viewport_get_object_lines(ctx->gl_vp, sel, line_start, line_end);
    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP,
           "shape_menu: viewport sel=%d lines=%d..%d rc=%d",
           sel, *line_start, *line_end, rc);
    if (rc == 0) {
        /* Capture so wrap_selected's line_end update persists across calls */
        ctx->sel_obj = sel;
        ctx->sel_line_start = *line_start;
        ctx->sel_line_end = *line_end;
    }
    return rc == 0;
}

static void
wrap_selected(ShapeMenuCtx *ctx, const char *prefix, const char *suffix)
{
    if (!ctx->code_ed) return;

    int ls, le;
    if (!get_selected_line_range(ctx, &ls, &le)) {
        dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "shape_menu: wrap_selected — no line range");
        return;
    }

    char *text = dc_code_editor_get_text(ctx->code_ed);
    if (!text) return;

    /* Find the start/end byte offsets for line_start..line_end */
    int line = 1;
    const char *p = text;
    const char *start_ptr = NULL;
    const char *end_ptr = NULL;

    while (*p) {
        if (line == ls && !start_ptr) start_ptr = p;
        if (*p == '\n') {
            if (line == le) { end_ptr = p + 1; break; }
            line++;
        }
        p++;
    }
    if (!start_ptr) start_ptr = text;
    if (!end_ptr) end_ptr = text + strlen(text);

    /* Indent the selected block */
    size_t sel_len = (size_t)(end_ptr - start_ptr);
    char *indented = malloc(sel_len * 5 + 8);
    if (!indented) { free(text); return; }

    size_t j = 0;
    indented[j++] = ' '; indented[j++] = ' ';
    indented[j++] = ' '; indented[j++] = ' ';
    for (size_t i = 0; i < sel_len; i++) {
        indented[j++] = start_ptr[i];
        if (start_ptr[i] == '\n' && i + 1 < sel_len) {
            indented[j++] = ' '; indented[j++] = ' ';
            indented[j++] = ' '; indented[j++] = ' ';
        }
    }
    indented[j] = '\0';

    /* Build: before + prefix + indented + suffix + after */
    size_t before_len = (size_t)(start_ptr - text);
    size_t after_len = strlen(end_ptr);
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);

    char *buf = malloc(before_len + prefix_len + j + suffix_len + after_len + 4);
    if (!buf) { free(indented); free(text); return; }

    size_t pos = 0;
    memcpy(buf + pos, text, before_len); pos += before_len;
    memcpy(buf + pos, prefix, prefix_len); pos += prefix_len;
    buf[pos++] = '\n';
    memcpy(buf + pos, indented, j); pos += j;
    if (buf[pos - 1] != '\n') buf[pos++] = '\n';
    memcpy(buf + pos, suffix, suffix_len); pos += suffix_len;
    buf[pos++] = '\n';
    memcpy(buf + pos, end_ptr, after_len); pos += after_len;
    buf[pos] = '\0';

    dc_code_editor_set_text(ctx->code_ed, buf);

    /* Count lines in the new statement to update the captured line range.
     * This is critical: after wrapping, the statement spans more lines.
     * Without this update, a second modify (e.g., translate after difference)
     * would use the stale line range and only wrap the first line. */
    int new_end = ls;
    for (size_t k = before_len; k < pos - after_len; k++) {
        if (buf[k] == '\n') new_end++;
    }
    /* Discount trailing newline that's part of the separator, not the statement */
    if (pos > after_len && buf[pos - after_len - 1] == '\n')
        new_end--;
    ctx->sel_line_end = new_end;

    /* Show transform panel if we just added translate or rotate */
    if (ctx->transform &&
        (strstr(prefix, "translate") || strstr(prefix, "rotate"))) {
        size_t stmt_len = pos - before_len - after_len;
        char *stmt = malloc(stmt_len + 1);
        if (stmt) {
            memcpy(stmt, buf + before_len, stmt_len);
            stmt[stmt_len] = '\0';
            dc_transform_panel_show(ctx->transform, stmt, ls, new_end);
            free(stmt);
        }
    }

    free(buf);
    free(indented);
    free(text);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "shape_menu: modified shape");
}

/* ---- Action callbacks for Modify Shape ---- */

static void
on_modify_difference(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    ShapeMenuCtx *ctx = data;
    wrap_selected(ctx,
        "difference() {",
        "    cylinder(d=5, h=50, center=true);\n}");
}

static void
on_modify_translate(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    wrap_selected(data, "translate([0, 0, 0])", "");
}

static void
on_modify_rotate(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    wrap_selected(data, "rotate([0, 0, 0])", "");
}

static void
on_modify_scale(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    wrap_selected(data, "scale([1, 1, 1])", "");
}

static void
on_modify_minkowski(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    ShapeMenuCtx *ctx = data;
    wrap_selected(ctx,
        "minkowski() {",
        "    sphere(r=1);\n}");
}

/* Forward declaration (defined below, needed by on_btn_extrude_face) */
static void close_popover(ShapeMenuCtx *ctx);

/* -------------------------------------------------------------------------
 * Face extrude — dialog + code generation
 * ---------------------------------------------------------------------- */

typedef struct {
    ShapeMenuCtx *ctx;
    GtkWidget    *popover;      /* the extrude parameter popover */
    GtkWidget    *spin_height;
    GtkWidget    *spin_taper;
    GtkWidget    *spin_twist;
    GtkWidget    *chk_center;
    GtkWidget    *chk_inward;
    /* Face geometry */
    float        *boundary_2d;   /* malloc'd [x,y] pairs */
    int           boundary_count;
    float         centroid[3];   /* SCAD space */
    float         rot_angles[3]; /* Euler angles (degrees) */
} ExtrudeCtx;

static void
extrude_ctx_free(ExtrudeCtx *ec)
{
    if (!ec) return;
    free(ec->boundary_2d);
    free(ec);
}

static void
on_extrude_cancel(GtkButton *btn, gpointer data)
{
    (void)btn;
    ExtrudeCtx *ec = data;
    if (ec->popover)
        gtk_popover_popdown(GTK_POPOVER(ec->popover));
}

static void
on_extrude_confirm(GtkButton *btn, gpointer data)
{
    (void)btn;
    ExtrudeCtx *ec = data;
    ShapeMenuCtx *ctx = ec->ctx;

    double height = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ec->spin_height));
    double taper  = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ec->spin_taper));
    double twist  = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ec->spin_twist));
    int center    = gtk_check_button_get_active(GTK_CHECK_BUTTON(ec->chk_center));
    int inward    = gtk_check_button_get_active(GTK_CHECK_BUTTON(ec->chk_inward));

    if (height <= 0) height = 1.0;

    /* Build the polygon points string */
    char *pts_buf = malloc((size_t)ec->boundary_count * 40 + 64);
    if (!pts_buf) return;
    int pos = 0;
    pos += sprintf(pts_buf + pos, "[");
    for (int i = 0; i < ec->boundary_count; i++) {
        if (i > 0) pos += sprintf(pts_buf + pos, ", ");
        pos += sprintf(pts_buf + pos, "[%.3f, %.3f]",
                       (double)ec->boundary_2d[i*2+0],
                       (double)ec->boundary_2d[i*2+1]);
    }
    pos += sprintf(pts_buf + pos, "]");

    /* Build the extrude SCAD code */
    /* Max: translate + rotate + linear_extrude + polygon — generous buffer */
    size_t code_sz = (size_t)pos + 512;
    char *code = malloc(code_sz);
    if (!code) { free(pts_buf); return; }

    int cp = 0;

    /* Wrapping: if inward, use difference(); if outward, use union() */
    int ls, le;
    int have_sel = get_selected_line_range(ctx, &ls, &le);
    const char *csg_open = inward ? "difference() {\n" : "union() {\n";
    const char *csg_close = "}";

    /* Build the extrude statement itself */
    char extrude_stmt[1024];
    int ep = 0;
    ep += sprintf(extrude_stmt + ep, "translate([%.3f, %.3f, %.3f])\n",
                  (double)ec->centroid[0], (double)ec->centroid[1], (double)ec->centroid[2]);

    /* Only add rotate if angles are non-trivial */
    if (fabsf(ec->rot_angles[0]) > 0.01f || fabsf(ec->rot_angles[1]) > 0.01f) {
        ep += sprintf(extrude_stmt + ep, "rotate([%.1f, %.1f, %.1f])\n",
                      (double)ec->rot_angles[0], (double)ec->rot_angles[1],
                      (double)ec->rot_angles[2]);
    }

    ep += sprintf(extrude_stmt + ep, "linear_extrude(height=%.3f", height);
    if (fabs(taper - 1.0) > 0.001) ep += sprintf(extrude_stmt + ep, ", scale=%.3f", taper);
    if (fabs(twist) > 0.01)        ep += sprintf(extrude_stmt + ep, ", twist=%.1f", twist);
    if (center)                     ep += sprintf(extrude_stmt + ep, ", center=true");
    ep += sprintf(extrude_stmt + ep, ")\n");

    if (inward) {
        /* Mirror the extrude direction inward (negate the normal) */
        ep += sprintf(extrude_stmt + ep, "mirror([0, 0, 1])\n");
    }

    ep += sprintf(extrude_stmt + ep, "polygon(%s);", pts_buf);

    if (have_sel) {
        /* Wrap existing code with union/difference + extrude */
        cp += sprintf(code + cp, "%s", csg_open);

        /* We'll use wrap_selected-style insertion. But simpler: just
         * append the extrude after the selected object's code. */
        char *text = dc_code_editor_get_text(ctx->code_ed);
        if (text) {
            /* Find line positions */
            int line = 1;
            const char *p = text;
            const char *start_ptr = NULL;
            const char *end_ptr = NULL;
            while (*p) {
                if (line == ls && !start_ptr) start_ptr = p;
                if (*p == '\n') {
                    if (line == le) { end_ptr = p + 1; break; }
                    line++;
                }
                p++;
            }
            if (!start_ptr) start_ptr = text;
            if (!end_ptr) end_ptr = text + strlen(text);

            /* Build: before + csg_open + indented_original + indented_extrude + csg_close + after */
            size_t before_len = (size_t)(start_ptr - text);
            size_t sel_len = (size_t)(end_ptr - start_ptr);
            size_t after_len = strlen(end_ptr);
            size_t csg_open_len = strlen(csg_open);
            size_t csg_close_len = strlen(csg_close);

            /* Indent original and extrude */
            char *full = malloc(before_len + csg_open_len + sel_len * 2 + (size_t)ep * 2 + csg_close_len + after_len + 256);
            if (full) {
                size_t fp = 0;
                memcpy(full + fp, text, before_len); fp += before_len;
                memcpy(full + fp, csg_open, csg_open_len); fp += csg_open_len;

                /* Indent original code */
                full[fp++] = ' '; full[fp++] = ' '; full[fp++] = ' '; full[fp++] = ' ';
                for (size_t i = 0; i < sel_len; i++) {
                    full[fp++] = start_ptr[i];
                    if (start_ptr[i] == '\n' && i + 1 < sel_len) {
                        full[fp++] = ' '; full[fp++] = ' ';
                        full[fp++] = ' '; full[fp++] = ' ';
                    }
                }
                if (fp > 0 && full[fp-1] != '\n') full[fp++] = '\n';

                /* Indent extrude statement */
                full[fp++] = ' '; full[fp++] = ' '; full[fp++] = ' '; full[fp++] = ' ';
                for (int i = 0; i < ep; i++) {
                    full[fp++] = extrude_stmt[i];
                    if (extrude_stmt[i] == '\n' && i + 1 < ep) {
                        full[fp++] = ' '; full[fp++] = ' ';
                        full[fp++] = ' '; full[fp++] = ' ';
                    }
                }
                if (fp > 0 && full[fp-1] != '\n') full[fp++] = '\n';

                memcpy(full + fp, csg_close, csg_close_len); fp += csg_close_len;
                full[fp++] = '\n';
                memcpy(full + fp, end_ptr, after_len); fp += after_len;
                full[fp] = '\0';

                dc_code_editor_set_text(ctx->code_ed, full);
                free(full);
            }
            free(text);
        }
    } else {
        /* No selection — just append the extrude code */
        insert_shape(ctx, extrude_stmt);
    }

    free(code);
    free(pts_buf);

    /* Trigger re-render */
    if (ctx->preview)
        dc_scad_preview_render(ctx->preview);

    /* Close the popover */
    if (ec->popover)
        gtk_popover_popdown(GTK_POPOVER(ec->popover));

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "shape_menu: face extrude applied (h=%.1f, taper=%.2f, twist=%.1f, %s)",
           height, taper, twist, inward ? "inward" : "outward");
}

static void
on_extrude_popover_closed(GtkPopover *popover, gpointer data)
{
    ExtrudeCtx *ec = data;
    gtk_widget_unparent(GTK_WIDGET(popover));
    ec->popover = NULL;
    extrude_ctx_free(ec);
}

static void
show_extrude_dialog(ShapeMenuCtx *ctx, double click_x, double click_y)
{
    if (!ctx->gl_vp || !ctx->code_ed) return;

    int obj_idx = dc_gl_viewport_get_selected(ctx->gl_vp);
    int face_idx = dc_gl_viewport_get_selected_face(ctx->gl_vp);
    if (obj_idx < 0 || face_idx < 0) return;

    ExtrudeCtx *ec = calloc(1, sizeof(*ec));
    if (!ec) return;
    ec->ctx = ctx;

    /* Get face boundary polygon */
    ec->boundary_2d = dc_gl_viewport_get_face_boundary(
        ctx->gl_vp, obj_idx, face_idx, &ec->boundary_count,
        ec->centroid, ec->rot_angles);

    if (!ec->boundary_2d || ec->boundary_count < 3) {
        dc_log(DC_LOG_WARN, DC_LOG_EVENT_APP,
               "shape_menu: cannot extrude face — no boundary polygon (%d verts)",
               ec->boundary_count);
        extrude_ctx_free(ec);
        return;
    }

    /* Build the parameter popover */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_start(grid, 10);
    gtk_widget_set_margin_end(grid, 10);
    gtk_widget_set_margin_top(grid, 10);
    gtk_widget_set_margin_bottom(grid, 10);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Extrude Face</b>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), title, 0, 0, 2, 1);

    int row = 1;

    /* Height */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Height:"), 0, row, 1, 1);
    ec->spin_height = gtk_spin_button_new_with_range(0.1, 1000.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ec->spin_height), 10.0);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(ec->spin_height), 2);
    gtk_grid_attach(GTK_GRID(grid), ec->spin_height, 1, row++, 1, 1);

    /* Taper */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Taper:"), 0, row, 1, 1);
    ec->spin_taper = gtk_spin_button_new_with_range(0.0, 10.0, 0.1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ec->spin_taper), 1.0);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(ec->spin_taper), 2);
    gtk_grid_attach(GTK_GRID(grid), ec->spin_taper, 1, row++, 1, 1);

    /* Twist */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Twist:"), 0, row, 1, 1);
    ec->spin_twist = gtk_spin_button_new_with_range(-360.0, 360.0, 15.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ec->spin_twist), 0.0);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(ec->spin_twist), 1);
    gtk_grid_attach(GTK_GRID(grid), ec->spin_twist, 1, row++, 1, 1);

    /* Center checkbox */
    ec->chk_center = gtk_check_button_new_with_label("Center");
    gtk_grid_attach(GTK_GRID(grid), ec->chk_center, 0, row++, 2, 1);

    /* Inward (subtraction) checkbox */
    ec->chk_inward = gtk_check_button_new_with_label("Inward (cut)");
    gtk_grid_attach(GTK_GRID(grid), ec->chk_inward, 0, row++, 2, 1);

    /* Info: polygon vertex count */
    char info[64];
    snprintf(info, sizeof(info), "Face: %d boundary vertices", ec->boundary_count);
    GtkWidget *info_label = gtk_label_new(info);
    gtk_widget_set_halign(info_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(info_label, "dim-label");
    gtk_grid_attach(GTK_GRID(grid), info_label, 0, row++, 2, 1);

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_extrude_cancel), ec);
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);

    GtkWidget *ok_btn = gtk_button_new_with_label("Extrude");
    gtk_widget_add_css_class(ok_btn, "suggested-action");
    g_signal_connect(ok_btn, "clicked", G_CALLBACK(on_extrude_confirm), ec);
    gtk_box_append(GTK_BOX(btn_box), ok_btn);

    gtk_grid_attach(GTK_GRID(grid), btn_box, 0, row, 2, 1);

    /* Create popover */
    GtkWidget *gl_widget = dc_gl_viewport_widget(ctx->gl_vp);
    ec->popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(ec->popover), grid);
    gtk_popover_set_autohide(GTK_POPOVER(ec->popover), TRUE);
    gtk_widget_set_parent(ec->popover, gl_widget);

    g_signal_connect(ec->popover, "closed",
                     G_CALLBACK(on_extrude_popover_closed), ec);

    GdkRectangle rect = { (int)click_x, (int)click_y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(ec->popover), &rect);
    gtk_popover_set_has_arrow(GTK_POPOVER(ec->popover), TRUE);

    gtk_popover_popup(GTK_POPOVER(ec->popover));

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "shape_menu: extrude dialog shown (face=%d, %d verts)",
           face_idx, ec->boundary_count);
}

static void on_btn_extrude_face(GtkButton *btn, gpointer data)
{
    (void)btn;
    ShapeMenuCtx *ctx = data;
    close_popover(ctx);
    show_extrude_dialog(ctx, ctx->click_x, ctx->click_y);
}

/* -------------------------------------------------------------------------
 * Edge profile editing — load face boundary into bezier editor
 * ---------------------------------------------------------------------- */

static void
on_profile_applied(DC_BezierEditor *editor, const DC_ProfileMeta *meta,
                   void *userdata)
{
    ShapeMenuCtx *ctx = userdata;
    if (!ctx->code_ed || !meta) return;

    /* Sample the bezier curve to get polygon points */
    int npts = dc_bezier_editor_point_count(editor);
    if (npts < 4) return;

    /* Tessellate the curve: evaluate each quadratic segment.
     * 8 samples per segment — enough for smooth output without bloat. */
    int n_seg = npts / 2;  /* closed: n_seg segments from 2*n_seg points */
    int samples_per_seg = 8;
    int total_samples = n_seg * samples_per_seg;

    char *pts_buf = malloc((size_t)total_samples * 40 + 64);
    if (!pts_buf) return;
    int pos = 0;
    pos += sprintf(pts_buf + pos, "[");

    int sample_count = 0;
    for (int seg = 0; seg < n_seg; seg++) {
        int i0 = seg * 2;
        int i1 = seg * 2 + 1;
        int i2 = (seg * 2 + 2) % npts;

        double p0x, p0y, p1x, p1y, p2x, p2y;
        dc_bezier_editor_get_point(editor, i0, &p0x, &p0y);
        dc_bezier_editor_get_point(editor, i1, &p1x, &p1y);
        dc_bezier_editor_get_point(editor, i2, &p2x, &p2y);

        for (int s = 0; s < samples_per_seg; s++) {
            double t = (double)s / samples_per_seg;
            double u = 1.0 - t;
            double bx = u*u*p0x + 2*u*t*p1x + t*t*p2x;
            double by = u*u*p0y + 2*u*t*p1y + t*t*p2y;

            if (sample_count > 0) pos += sprintf(pts_buf + pos, ", ");
            pos += sprintf(pts_buf + pos, "[%.3f, %.3f]", bx, by);
            sample_count++;
        }
    }
    pos += sprintf(pts_buf + pos, "]");

    /* Build bezier comment: stores original control points for re-editing */
    char *bez_comment = malloc((size_t)npts * 30 + 64);
    if (!bez_comment) { free(pts_buf); return; }
    int bc = 0;
    bc += sprintf(bez_comment + bc, "// dc_bezier: %d", npts);
    for (int i = 0; i < npts; i++) {
        double bx, by;
        dc_bezier_editor_get_point(editor, i, &bx, &by);
        bc += sprintf(bez_comment + bc, " %.4f,%.4f", bx, by);
    }
    bc += sprintf(bez_comment + bc, "\n");

    /* Build SCAD code — dynamic buffer since polygon string can be large */
    size_t pts_len = strlen(pts_buf);
    size_t bez_len = strlen(bez_comment);
    size_t code_sz = pts_len + bez_len + 512;
    char *code = malloc(code_sz);
    if (!code) { free(pts_buf); free(bez_comment); return; }
    int cp = 0;

    /* Emit bezier comment first (parsed on re-edit) */
    memcpy(code, bez_comment, bez_len);
    cp = (int)bez_len;
    free(bez_comment);

    cp += sprintf(code + cp, "translate([%.3f, %.3f, %.3f])\n",
                  (double)meta->centroid[0], (double)meta->centroid[1],
                  (double)meta->centroid[2]);

    if (fabsf(meta->rot_angles[0]) > 0.01f || fabsf(meta->rot_angles[1]) > 0.01f) {
        cp += sprintf(code + cp, "rotate([%.1f, %.1f, %.1f])\n",
                      (double)meta->rot_angles[0], (double)meta->rot_angles[1],
                      (double)meta->rot_angles[2]);
    }

    cp += sprintf(code + cp, "linear_extrude(height=%.3f)\n", (double)meta->height);
    cp += sprintf(code + cp, "polygon(%s);", pts_buf);
    free(pts_buf);

    /* Replace the original object's code with the new profile extrude */
    char *text = dc_code_editor_get_text(ctx->code_ed);
    if (!text) return;

    int ls = meta->line_start, le = meta->line_end;
    int line = 1;
    const char *p = text;
    const char *start_ptr = NULL;
    const char *end_ptr = NULL;
    while (*p) {
        if (line == ls && !start_ptr) start_ptr = p;
        if (*p == '\n') {
            if (line == le) { end_ptr = p + 1; break; }
            line++;
        }
        p++;
    }
    if (!start_ptr) start_ptr = text;
    if (!end_ptr) end_ptr = text + strlen(text);

    size_t before_len = (size_t)(start_ptr - text);
    size_t after_len = strlen(end_ptr);
    size_t code_len = strlen(code);

    char *buf = malloc(before_len + code_len + after_len + 4);
    if (buf) {
        size_t bp = 0;
        memcpy(buf + bp, text, before_len); bp += before_len;
        memcpy(buf + bp, code, code_len); bp += code_len;
        if (bp > 0 && buf[bp-1] != '\n') buf[bp++] = '\n';
        memcpy(buf + bp, end_ptr, after_len); bp += after_len;
        buf[bp] = '\0';
        dc_code_editor_set_text(ctx->code_ed, buf);
        free(buf);
    }
    free(text);
    free(code);

    /* Clear profile mode */
    dc_bezier_editor_clear_profile(editor);

    /* Re-render */
    if (ctx->preview)
        dc_scad_preview_render(ctx->preview);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "shape_menu: profile applied (%d sample points)", sample_count);
}

/* Try to parse a "// dc_bezier: N x0,y0 x1,y1 ..." comment from source code.
 * Returns malloc'd double array of [x,y] pairs, sets *count. NULL if not found. */
static double *
parse_bezier_comment(const char *source, int line_start, int line_end, int *count)
{
    if (!source || line_start <= 0) return NULL;

    /* Find the line range in the source text */
    const char *p = source;
    int line = 1;
    const char *region_start = NULL;
    const char *region_end = NULL;
    while (*p) {
        if (line == line_start && !region_start) region_start = p;
        if (*p == '\n') {
            if (line == line_end) { region_end = p + 1; break; }
            line++;
        }
        p++;
    }
    if (!region_start) return NULL;
    if (!region_end) region_end = source + strlen(source);

    /* Search for "// dc_bezier:" in the region */
    const char *marker = "// dc_bezier:";
    size_t marker_len = strlen(marker);
    const char *found = NULL;
    for (const char *s = region_start; s < region_end - marker_len; s++) {
        if (memcmp(s, marker, marker_len) == 0) { found = s + marker_len; break; }
    }
    if (!found) return NULL;

    /* Parse: " N x0,y0 x1,y1 ..." */
    int n = 0;
    const char *cur = found;
    while (*cur == ' ') cur++;
    n = atoi(cur);
    if (n < 4 || n > 10000) return NULL;
    while (*cur && *cur != ' ' && *cur != '\n') cur++;

    double *pts = malloc((size_t)n * 2 * sizeof(double));
    if (!pts) return NULL;

    for (int i = 0; i < n; i++) {
        while (*cur == ' ') cur++;
        if (!*cur || *cur == '\n') { free(pts); return NULL; }
        char *end;
        pts[i*2] = strtod(cur, &end);
        if (*end != ',') { free(pts); return NULL; }
        cur = end + 1;
        pts[i*2+1] = strtod(cur, &end);
        cur = end;
    }

    *count = n;
    return pts;
}

static void
show_edit_profile(ShapeMenuCtx *ctx)
{
    if (!ctx->gl_vp || !ctx->bez_ed) return;

    int obj_idx = dc_gl_viewport_get_selected(ctx->gl_vp);
    int face_idx = dc_gl_viewport_get_selected_face(ctx->gl_vp);
    if (obj_idx < 0 || face_idx < 0) return;

    /* Get face boundary (needed for centroid/rotation even if we have cached bezier) */
    float centroid[3], rot_angles[3];
    int boundary_count;
    float *boundary = dc_gl_viewport_get_face_boundary(
        ctx->gl_vp, obj_idx, face_idx, &boundary_count,
        centroid, rot_angles);

    if (!boundary || boundary_count < 3) {
        dc_log(DC_LOG_WARN, DC_LOG_EVENT_APP,
               "shape_menu: cannot edit profile — no boundary (%d verts)",
               boundary_count);
        free(boundary);
        return;
    }

    /* Compute object height along face normal */
    float nx, ny, nz;
    dc_gl_viewport_get_face_normal(ctx->gl_vp, obj_idx, face_idx, &nx, &ny, &nz);
    float height = dc_gl_viewport_get_object_extent(ctx->gl_vp, obj_idx, nx, ny, nz);
    if (height < 0.001f) height = 1.0f;

    /* Try to recover original bezier control points from source code comment */
    double *dpts = NULL;
    int bez_count = 0;
    char *text = dc_code_editor_get_text(ctx->code_ed);
    if (text && ctx->sel_line_start > 0) {
        dpts = parse_bezier_comment(text, ctx->sel_line_start,
                                     ctx->sel_line_end, &bez_count);
        if (dpts) {
            dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
                   "shape_menu: recovered %d bezier points from source comment",
                   bez_count);
        }
    }
    free(text);

    /* If no cached bezier, generate from mesh boundary */
    if (!dpts) {
        DC_EdgeProfile prof;
        dc_edge_profile_analyze(boundary, boundary_count, &prof);

        DC_EP_Point *bez_pts = NULL;
        if (prof.type == DC_PROFILE_CIRCLE) {
            bez_pts = dc_edge_profile_circle_bezier(
                prof.circle.cx, prof.circle.cy, prof.circle.radius,
                8, &bez_count);
            dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
                   "shape_menu: detected circle (r=%.2f, err=%.4f)",
                   prof.circle.radius, prof.circle.fit_error);
        } else {
            bez_pts = dc_edge_profile_polygon_bezier(boundary, boundary_count,
                                                      &bez_count);
            dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
                   "shape_menu: freeform profile (%d verts)", boundary_count);
        }

        if (!bez_pts || bez_count < 4) {
            free(bez_pts);
            free(boundary);
            return;
        }

        dpts = malloc((size_t)bez_count * 2 * sizeof(double));
        if (!dpts) { free(bez_pts); free(boundary); return; }
        for (int i = 0; i < bez_count; i++) {
            dpts[i*2] = bez_pts[i].x;
            dpts[i*2+1] = bez_pts[i].y;
        }
        free(bez_pts);
    }
    free(boundary);

    /* Build metadata */
    DC_ProfileMeta meta = {0};
    memcpy(meta.centroid, centroid, sizeof(centroid));
    memcpy(meta.rot_angles, rot_angles, sizeof(rot_angles));
    meta.height = height;
    meta.obj_idx = obj_idx;
    meta.face_idx = face_idx;
    meta.line_start = ctx->sel_line_start;
    meta.line_end = ctx->sel_line_end;

    /* Set apply callback */
    dc_bezier_editor_set_profile_apply_cb(ctx->bez_ed,
                                           on_profile_applied, ctx);

    /* Load into bezier editor */
    dc_bezier_editor_load_profile(ctx->bez_ed, dpts, bez_count, 1, &meta);
    free(dpts);
}

static void on_btn_edit_profile(GtkButton *btn, gpointer data)
{
    (void)btn;
    ShapeMenuCtx *ctx = data;
    close_popover(ctx);
    show_edit_profile(ctx);
}

/* -------------------------------------------------------------------------
 * Right-click gesture handler — builds and shows popover
 * ---------------------------------------------------------------------- */

static void
on_popover_closed(GtkPopover *popover, gpointer data)
{
    ShapeMenuCtx *ctx = data;
    if (ctx->popover == GTK_WIDGET(popover)) {
        gtk_widget_unparent(ctx->popover);
        ctx->popover = NULL;
    }
}

/* ---- Button callbacks for popover menu ---- */

static void close_popover(ShapeMenuCtx *ctx)
{
    if (ctx->popover)
        gtk_popover_popdown(GTK_POPOVER(ctx->popover));
}

static void on_btn_insert_cube(GtkButton *btn, gpointer data)
{ (void)btn; ShapeMenuCtx *c = data; insert_shape(c, "cube([10, 10, 10], center=true);"); close_popover(c); }

static void on_btn_insert_sphere(GtkButton *btn, gpointer data)
{ (void)btn; ShapeMenuCtx *c = data; insert_shape(c, "sphere(d=10);"); close_popover(c); }

static void on_btn_insert_cylinder(GtkButton *btn, gpointer data)
{ (void)btn; ShapeMenuCtx *c = data; insert_shape(c, "cylinder(d=10, h=20, center=true);"); close_popover(c); }

static void on_btn_insert_tetra(GtkButton *btn, gpointer data)
{ (void)btn; ShapeMenuCtx *c = data; insert_shape(c, "tetrahedron(r=10);"); close_popover(c); }

static void on_btn_insert_octa(GtkButton *btn, gpointer data)
{ (void)btn; ShapeMenuCtx *c = data; insert_shape(c, "octahedron(r=10);"); close_popover(c); }

static void on_btn_insert_dodeca(GtkButton *btn, gpointer data)
{ (void)btn; ShapeMenuCtx *c = data; insert_shape(c, "dodecahedron(r=10);"); close_popover(c); }

static void on_btn_insert_icosa(GtkButton *btn, gpointer data)
{ (void)btn; ShapeMenuCtx *c = data; insert_shape(c, "icosahedron(r=10);"); close_popover(c); }

static void on_btn_modify_difference(GtkButton *btn, gpointer data)
{
    (void)btn;
    ShapeMenuCtx *ctx = data;
    wrap_selected(ctx, "difference() {",
                  "    cylinder(d=5, h=50, center=true);\n}");
    close_popover(ctx);
}

static void on_btn_modify_translate(GtkButton *btn, gpointer data)
{ (void)btn; ShapeMenuCtx *c = data; wrap_selected(c, "translate([0, 0, 0])", ""); close_popover(c); }

static void on_btn_modify_rotate(GtkButton *btn, gpointer data)
{ (void)btn; ShapeMenuCtx *c = data; wrap_selected(c, "rotate([0, 0, 0])", ""); close_popover(c); }

static void on_btn_modify_scale(GtkButton *btn, gpointer data)
{ (void)btn; ShapeMenuCtx *c = data; wrap_selected(c, "scale([1, 1, 1])", ""); close_popover(c); }

static void on_btn_modify_minkowski(GtkButton *btn, gpointer data)
{
    (void)btn;
    ShapeMenuCtx *ctx = data;
    wrap_selected(ctx, "minkowski() {", "    sphere(r=1);\n}");
    close_popover(ctx);
}

static GtkWidget *
menu_button(const char *label, GCallback cb, gpointer data)
{
    GtkWidget *btn = gtk_button_new_with_label(label);
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    gtk_widget_set_halign(btn, GTK_ALIGN_START);
    g_signal_connect(btn, "clicked", cb, data);
    return btn;
}

static void
on_right_click(GtkGestureClick *gesture, int n_press,
               double x, double y, gpointer data)
{
    (void)gesture; (void)n_press;
    ShapeMenuCtx *ctx = data;
    ctx->click_x = x;
    ctx->click_y = y;

    /* Capture selection state NOW */
    ctx->sel_obj = ctx->gl_vp ? dc_gl_viewport_get_selected(ctx->gl_vp) : -1;
    ctx->sel_line_start = 0;
    ctx->sel_line_end = 0;
    if (ctx->sel_obj >= 0) {
        dc_gl_viewport_get_object_lines(ctx->gl_vp, ctx->sel_obj,
                                         &ctx->sel_line_start, &ctx->sel_line_end);
        dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP,
               "shape_menu: captured sel=%d lines=%d..%d",
               ctx->sel_obj, ctx->sel_line_start, ctx->sel_line_end);
    }

    /* Destroy old popover if any */
    if (ctx->popover) {
        gtk_widget_unparent(ctx->popover);
        ctx->popover = NULL;
    }

    /* Build popover with direct button callbacks */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(vbox, 4);
    gtk_widget_set_margin_end(vbox, 4);
    gtk_widget_set_margin_top(vbox, 4);
    gtk_widget_set_margin_bottom(vbox, 4);

    /* Section: Insert Shape */
    GtkWidget *insert_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(insert_label), "<b>Insert Shape</b>");
    gtk_widget_set_halign(insert_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), insert_label);

    gtk_box_append(GTK_BOX(vbox), menu_button("  Cube", G_CALLBACK(on_btn_insert_cube), ctx));
    gtk_box_append(GTK_BOX(vbox), menu_button("  Sphere", G_CALLBACK(on_btn_insert_sphere), ctx));
    gtk_box_append(GTK_BOX(vbox), menu_button("  Cylinder", G_CALLBACK(on_btn_insert_cylinder), ctx));
    gtk_box_append(GTK_BOX(vbox), menu_button("  Tetrahedron", G_CALLBACK(on_btn_insert_tetra), ctx));
    gtk_box_append(GTK_BOX(vbox), menu_button("  Octahedron", G_CALLBACK(on_btn_insert_octa), ctx));
    gtk_box_append(GTK_BOX(vbox), menu_button("  Dodecahedron", G_CALLBACK(on_btn_insert_dodeca), ctx));
    gtk_box_append(GTK_BOX(vbox), menu_button("  Icosahedron", G_CALLBACK(on_btn_insert_icosa), ctx));

    /* Section: Modify Shape (only if selected) */
    if (ctx->sel_obj >= 0) {
        gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

        GtkWidget *modify_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(modify_label), "<b>Modify Shape</b>");
        gtk_widget_set_halign(modify_label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(vbox), modify_label);

        gtk_box_append(GTK_BOX(vbox), menu_button("  Difference (subtract cylinder)",
                       G_CALLBACK(on_btn_modify_difference), ctx));
        gtk_box_append(GTK_BOX(vbox), menu_button("  Translate",
                       G_CALLBACK(on_btn_modify_translate), ctx));
        gtk_box_append(GTK_BOX(vbox), menu_button("  Rotate",
                       G_CALLBACK(on_btn_modify_rotate), ctx));
        gtk_box_append(GTK_BOX(vbox), menu_button("  Scale",
                       G_CALLBACK(on_btn_modify_scale), ctx));
        gtk_box_append(GTK_BOX(vbox), menu_button("  Minkowski",
                       G_CALLBACK(on_btn_modify_minkowski), ctx));
    }

    /* Section: Face Operations (only in face mode with a selected face) */
    DC_SelectMode sel_mode = ctx->gl_vp ? dc_gl_viewport_get_select_mode(ctx->gl_vp) : DC_SEL_OBJECT;
    int sel_face = ctx->gl_vp ? dc_gl_viewport_get_selected_face(ctx->gl_vp) : -1;
    if (sel_mode == DC_SEL_FACE && sel_face >= 0 && ctx->sel_obj >= 0) {
        gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

        GtkWidget *face_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(face_label), "<b>Face Operations</b>");
        gtk_widget_set_halign(face_label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(vbox), face_label);

        gtk_box_append(GTK_BOX(vbox), menu_button("  Extrude Face...",
                       G_CALLBACK(on_btn_extrude_face), ctx));
        if (ctx->bez_ed) {
            gtk_box_append(GTK_BOX(vbox), menu_button("  Edit Profile...",
                           G_CALLBACK(on_btn_edit_profile), ctx));
        }
    }

    GtkWidget *gl_widget = dc_gl_viewport_widget(ctx->gl_vp);
    ctx->popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(ctx->popover), vbox);
    gtk_popover_set_autohide(GTK_POPOVER(ctx->popover), TRUE);
    gtk_widget_set_parent(ctx->popover, gl_widget);

    /* Clean up popover on close */
    g_signal_connect(ctx->popover, "closed",
                     G_CALLBACK(on_popover_closed), ctx);

    /* Position at click point */
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(ctx->popover), &rect);
    gtk_popover_set_has_arrow(GTK_POPOVER(ctx->popover), TRUE);

    gtk_popover_popup(GTK_POPOVER(ctx->popover));
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void
dc_shape_menu_attach(GtkWidget *gl_widget,
                      DC_CodeEditor *code_ed,
                      DC_GlViewport *gl_vp,
                      DC_TransformPanel *transform,
                      DC_ScadPreview *preview,
                      DC_BezierEditor *bez_ed)
{
    ShapeMenuCtx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return;
    ctx->code_ed   = code_ed;
    ctx->gl_vp     = gl_vp;
    ctx->transform = transform;
    ctx->preview   = preview;
    ctx->bez_ed    = bez_ed;
    ctx->sel_obj   = -1;

    /* Register actions — shared group installed on both GL widget and window */
    GSimpleActionGroup *group = g_simple_action_group_new();

    /* Insert actions */
    GSimpleAction *a;
    a = g_simple_action_new("insert-cube", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_insert_cube), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    a = g_simple_action_new("insert-sphere", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_insert_sphere), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    a = g_simple_action_new("insert-cylinder", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_insert_cylinder), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    a = g_simple_action_new("insert-tetrahedron", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_insert_tetrahedron), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    a = g_simple_action_new("insert-octahedron", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_insert_octahedron), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    a = g_simple_action_new("insert-dodecahedron", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_insert_dodecahedron), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    a = g_simple_action_new("insert-icosahedron", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_insert_icosahedron), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    /* Modify actions */
    a = g_simple_action_new("modify-difference", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_modify_difference), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    a = g_simple_action_new("modify-translate", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_modify_translate), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    a = g_simple_action_new("modify-rotate", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_modify_rotate), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    a = g_simple_action_new("modify-scale", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_modify_scale), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    a = g_simple_action_new("modify-minkowski", NULL);
    g_signal_connect(a, "activate", G_CALLBACK(on_modify_minkowski), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(a));

    gtk_widget_insert_action_group(gl_widget, "shape", G_ACTION_GROUP(group));

    /* Store group ref so caller can install on window for menu bar */
    g_object_set_data(G_OBJECT(gl_widget), "dc-shape-action-group", group);

    /* Right-click gesture (button 3) */
    GtkGesture *right = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), 3);
    g_signal_connect(right, "pressed",
                     G_CALLBACK(on_right_click), ctx);
    gtk_widget_add_controller(gl_widget, GTK_EVENT_CONTROLLER(right));

    /* Store ctx for cleanup */
    g_object_set_data_full(G_OBJECT(gl_widget), "dc-shape-menu-ctx", ctx, free);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "shape_menu: attached to viewport");
}

GMenuModel *
dc_shape_menu_build_insert_menu(void)
{
    GMenu *insert_menu = g_menu_new();
    g_menu_append(insert_menu, "Cube",         "shape.insert-cube");
    g_menu_append(insert_menu, "Sphere",       "shape.insert-sphere");
    g_menu_append(insert_menu, "Cylinder",     "shape.insert-cylinder");
    g_menu_append(insert_menu, "Tetrahedron",  "shape.insert-tetrahedron");
    g_menu_append(insert_menu, "Octahedron",   "shape.insert-octahedron");
    g_menu_append(insert_menu, "Dodecahedron", "shape.insert-dodecahedron");
    g_menu_append(insert_menu, "Icosahedron",  "shape.insert-icosahedron");
    return G_MENU_MODEL(insert_menu);
}
