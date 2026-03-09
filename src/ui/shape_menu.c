#include "ui/shape_menu.h"
#include "ui/code_editor.h"
#include "ui/transform_panel.h"
#include "ui/scad_preview.h"
#include "gl/gl_viewport.h"
#include "core/log.h"

#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Context state — attached to the viewport widget via g_object_set_data
 * ---------------------------------------------------------------------- */
typedef struct {
    DC_CodeEditor     *code_ed;    /* borrowed */
    DC_GlViewport     *gl_vp;     /* borrowed */
    DC_TransformPanel *transform; /* borrowed, may be NULL */
    DC_ScadPreview    *preview;   /* borrowed, may be NULL */
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
    char *indented = malloc(sel_len * 2 + 1);
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
                      DC_ScadPreview *preview)
{
    ShapeMenuCtx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return;
    ctx->code_ed   = code_ed;
    ctx->gl_vp     = gl_vp;
    ctx->transform = transform;
    ctx->preview   = preview;
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
    g_menu_append(insert_menu, "Cube",     "shape.insert-cube");
    g_menu_append(insert_menu, "Sphere",   "shape.insert-sphere");
    g_menu_append(insert_menu, "Cylinder", "shape.insert-cylinder");
    return G_MENU_MODEL(insert_menu);
}
