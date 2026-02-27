#include "bezier/bezier_editor.h"
#include "bezier/bezier_canvas.h"
#include "bezier/bezier_curve.h"   /* DC_Point2 */
#include "core/array.h"
#include "core/log.h"
#include "ui/app_window.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */
#define DC_POINT_RADIUS_PX  6.0
#define DC_HIT_RADIUS_PX    10.0
#define DC_CURVE_STEPS      200

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */
struct DC_BezierEditor {
    DC_BezierCanvas *canvas;        /* owned */
    DC_Array        *pts;           /* DC_Array of DC_Point2 in world coords */
    DC_Array        *junctures;     /* DC_Array of uint8_t, parallel to pts */
    int              selected;      /* -1 or index into pts */
    int              mouse_down;    /* 1 while button 1 is held */
    double           orig_x;       /* world pos of selected point at drag start */
    double           orig_y;
    double           press_sx;     /* screen pos at press (for computing delta) */
    double           press_sy;
    GtkWidget       *window;        /* borrowed */
    GtkWidget       *container;     /* GtkBox(V): toolbar + canvas */
    GtkWidget       *global_chain_btn;  /* GtkToggleButton: global chain mode */
    gulong           global_chain_hid;
    GtkWidget       *chain_btn;     /* GtkToggleButton: local juncture toggle */
    gulong           chain_handler_id;
    uint8_t          chain_mode;    /* 1 = new endpoints are junctures (default) */
};

/* -------------------------------------------------------------------------
 * Juncture helpers
 * ---------------------------------------------------------------------- */

/* Returns 1 if point at index is a juncture (on-curve boundary).
 * First and last points are always junctures regardless of flag. */
static int
is_juncture(DC_BezierEditor *ed, int index)
{
    int count = (int)dc_array_length(ed->pts);
    if (index <= 0 || index >= count - 1) return 1;
    uint8_t *flag = dc_array_get(ed->junctures, (size_t)index);
    return flag ? (*flag != 0) : 1;
}

/* Sync chain toggle button to reflect the currently selected point. */
static void
update_chain_button(DC_BezierEditor *ed)
{
    if (!ed->chain_btn) return;
    int count = (int)dc_array_length(ed->pts);
    int sel = ed->selected;

    if (sel < 0 || sel == 0 || sel == count - 1) {
        /* No selection, or first/last — grey out */
        g_signal_handler_block(ed->chain_btn, ed->chain_handler_id);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ed->chain_btn), FALSE);
        g_signal_handler_unblock(ed->chain_btn, ed->chain_handler_id);
        gtk_widget_set_sensitive(ed->chain_btn, FALSE);
    } else {
        gtk_widget_set_sensitive(ed->chain_btn, TRUE);
        g_signal_handler_block(ed->chain_btn, ed->chain_handler_id);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ed->chain_btn),
                                     is_juncture(ed, sel) ? TRUE : FALSE);
        g_signal_handler_unblock(ed->chain_btn, ed->chain_handler_id);
    }
}

/* -------------------------------------------------------------------------
 * Status bar helper
 * ---------------------------------------------------------------------- */
static void
update_status(DC_BezierEditor *ed)
{
    if (!ed->window) return;
    int count = (int)dc_array_length(ed->pts);
    char buf[256];
    const char *mode = ed->chain_mode ? "Chain: ON" : "Chain: OFF";

    /* Count segments by walking juncture boundaries */
    int num_segments = 0;
    if (count >= 2) {
        for (int i = 1; i < count; i++) {
            if (i == count - 1 || is_juncture(ed, i))
                num_segments++;
        }
    }

    if (num_segments == 0) {
        snprintf(buf, sizeof(buf),
                 "%s  |  Click to place points  (%d placed)", mode, count);
    } else if (ed->selected >= 0) {
        const char *kind = is_juncture(ed, ed->selected)
                         ? "juncture" : "control";
        snprintf(buf, sizeof(buf),
                 "%s  |  %d seg%s  |  P%d (%s)  |  [C] local  [Shift+C] global",
                 mode, num_segments, num_segments == 1 ? "" : "s",
                 ed->selected, kind);
    } else {
        snprintf(buf, sizeof(buf),
                 "%s  |  %d seg%s  |  Click to add or drag  |  [Shift+C] global",
                 mode, num_segments, num_segments == 1 ? "" : "s");
    }
    dc_app_window_set_status(ed->window, buf);
}

/* -------------------------------------------------------------------------
 * Hit test — find nearest point within radius, return index or -1
 * ---------------------------------------------------------------------- */
static int
hit_test(DC_BezierEditor *ed, double wx, double wy)
{
    double zoom = dc_bezier_canvas_get_zoom(ed->canvas);
    double radius = DC_HIT_RADIUS_PX / zoom;
    double best = radius;
    int hit = -1;
    int count = (int)dc_array_length(ed->pts);

    for (int i = 0; i < count; i++) {
        DC_Point2 *p = dc_array_get(ed->pts, (size_t)i);
        double dx = wx - p->x;
        double dy = wy - p->y;
        double d = sqrt(dx * dx + dy * dy);
        if (d < best) {
            best = d;
            hit = i;
        }
    }
    return hit;
}

/* -------------------------------------------------------------------------
 * De Casteljau — evaluate arbitrary-degree bezier at parameter t
 * Input: N screen-coordinate points (px, py), parameter t
 * Output: result written to *out_x, *out_y
 * tmp_x/tmp_y must be caller-allocated arrays of length N
 * ---------------------------------------------------------------------- */
static void
decasteljau(const double *px, const double *py, int n,
            double t, double *tmp_x, double *tmp_y,
            double *out_x, double *out_y)
{
    for (int i = 0; i < n; i++) {
        tmp_x[i] = px[i];
        tmp_y[i] = py[i];
    }
    double u = 1.0 - t;
    for (int level = 1; level < n; level++) {
        for (int i = 0; i < n - level; i++) {
            tmp_x[i] = u * tmp_x[i] + t * tmp_x[i + 1];
            tmp_y[i] = u * tmp_y[i] + t * tmp_y[i + 1];
        }
    }
    *out_x = tmp_x[0];
    *out_y = tmp_y[0];
}

/* -------------------------------------------------------------------------
 * Overlay — draw control polygon, bezier curve(s), and points
 * ---------------------------------------------------------------------- */
static void
editor_overlay(DC_BezierCanvas *canvas, cairo_t *cr,
               int width, int height, void *userdata)
{
    (void)width; (void)height;
    DC_BezierEditor *ed = userdata;
    int count = (int)dc_array_length(ed->pts);
    if (count == 0) return;

    /* Convert all placed points to screen coordinates */
    double *sx = malloc((size_t)count * sizeof(double));
    double *sy = malloc((size_t)count * sizeof(double));
    if (!sx || !sy) { free(sx); free(sy); return; }

    for (int i = 0; i < count; i++) {
        DC_Point2 *p = dc_array_get(ed->pts, (size_t)i);
        dc_bezier_canvas_world_to_screen(canvas, p->x, p->y, &sx[i], &sy[i]);
    }

    /* Control polygon: thin gray dashed lines connecting all placed points */
    if (count >= 2) {
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.6);
        cairo_set_line_width(cr, 1.0);
        double dashes[] = {4.0, 4.0};
        cairo_set_dash(cr, dashes, 2, 0);
        cairo_move_to(cr, sx[0], sy[0]);
        for (int i = 1; i < count; i++)
            cairo_line_to(cr, sx[i], sy[i]);
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0);
    }

    /* Draw bezier spans between juncture boundaries */
    if (count >= 2) {
        double *tmp_x = malloc((size_t)count * sizeof(double));
        double *tmp_y = malloc((size_t)count * sizeof(double));
        if (tmp_x && tmp_y) {
            cairo_set_source_rgba(cr, 0.0, 1.0, 0.8, 1.0);
            cairo_set_line_width(cr, 3.0);

            int seg_start = 0;
            for (int i = 1; i < count; i++) {
                if (i == count - 1 || is_juncture(ed, i)) {
                    int n = i - seg_start + 1;
                    if (n >= 2) {
                        cairo_move_to(cr, sx[seg_start], sy[seg_start]);
                        for (int step = 1; step <= DC_CURVE_STEPS; step++) {
                            double t = (double)step / DC_CURVE_STEPS;
                            double bx, by;
                            decasteljau(sx + seg_start, sy + seg_start, n,
                                        t, tmp_x, tmp_y, &bx, &by);
                            cairo_line_to(cr, bx, by);
                        }
                    }
                    seg_start = i;
                }
            }
            cairo_stroke(cr);
        }
        free(tmp_x);
        free(tmp_y);
    }

    /* Control point dots */
    for (int i = 0; i < count; i++) {
        if (i == ed->selected) {
            cairo_set_source_rgba(cr, 1.0, 0.6, 0.1, 1.0);   /* orange: selected */
        } else if (is_juncture(ed, i)) {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);   /* white: juncture */
        } else {
            cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, 1.0);   /* blue: control */
        }
        cairo_arc(cr, sx[i], sy[i], DC_POINT_RADIUS_PX, 0, 2 * G_PI);
        cairo_fill(cr);
    }

    free(sx);
    free(sy);
}

/* -------------------------------------------------------------------------
 * Click handler — place or select points
 * ---------------------------------------------------------------------- */
static void
on_press(GtkGestureClick *gesture, int n_press, double x, double y,
         gpointer data)
{
    (void)n_press;
    DC_BezierEditor *ed = data;

    /* Grab focus so key events reach the canvas */
    gtk_widget_grab_focus(dc_bezier_canvas_widget(ed->canvas));

    /* If space is held, don't interfere with canvas pan */
    if (dc_bezier_canvas_space_held(ed->canvas)) return;

    double wx, wy;
    dc_bezier_canvas_screen_to_world(ed->canvas, x, y, &wx, &wy);

    int hit = hit_test(ed, wx, wy);

    if (hit >= 0) {
        /* Select existing point, prepare for drag */
        DC_Point2 *p = dc_array_get(ed->pts, (size_t)hit);
        ed->selected = hit;
        ed->mouse_down = 1;
        ed->orig_x = p->x;
        ed->orig_y = p->y;
        ed->press_sx = x;
        ed->press_sy = y;
    } else {
        /* Place new endpoint (+ control point for segments after the first) */
        int count = (int)dc_array_length(ed->pts);
        DC_Point2 pt = { wx, wy };

        if (count == 0) {
            /* First point: just one juncture endpoint */
            dc_array_push(ed->pts, &pt);
            uint8_t jflag = 1;
            dc_array_push(ed->junctures, &jflag);
            ed->selected = 0;
        } else {
            /* Control point at midpoint of previous endpoint and click,
             * endpoint at click position. Drag moves control relative. */
            DC_Point2 *prev = dc_array_get(ed->pts, (size_t)(count - 1));
            DC_Point2 mid = { (prev->x + wx) * 0.5, (prev->y + wy) * 0.5 };

            dc_array_push(ed->pts, &mid);
            uint8_t ctrl_flag = 0;
            dc_array_push(ed->junctures, &ctrl_flag);

            dc_array_push(ed->pts, &pt);
            uint8_t end_flag = ed->chain_mode;
            dc_array_push(ed->junctures, &end_flag);

            /* Select the control point so drag shapes the curve */
            ed->selected = (int)dc_array_length(ed->pts) - 2;
        }

        ed->mouse_down = 1;
        ed->orig_x = (count == 0) ? wx
                     : ((*(DC_Point2*)dc_array_get(ed->pts,
                           (size_t)ed->selected)).x);
        ed->orig_y = (count == 0) ? wy
                     : ((*(DC_Point2*)dc_array_get(ed->pts,
                           (size_t)ed->selected)).y);
        ed->press_sx = x;
        ed->press_sy = y;
    }

    update_chain_button(ed);
    update_status(ed);
    gtk_widget_queue_draw(dc_bezier_canvas_widget(ed->canvas));
    (void)gesture;
}

static void
on_release(GtkGestureClick *gesture, int n_press, double x, double y,
           gpointer data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    DC_BezierEditor *ed = data;
    ed->mouse_down = 0;
}

/* -------------------------------------------------------------------------
 * Motion handler — drag selected point
 * ---------------------------------------------------------------------- */
static void
on_motion(GtkEventControllerMotion *ctrl, double x, double y, gpointer data)
{
    (void)ctrl;
    DC_BezierEditor *ed = data;

    if (!ed->mouse_down || ed->selected < 0) return;

    /* Convert screen delta to world delta */
    double zoom = dc_bezier_canvas_get_zoom(ed->canvas);
    double dx_screen = x - ed->press_sx;
    double dy_screen = y - ed->press_sy;
    double dwx =  dx_screen / zoom;
    double dwy = -dy_screen / zoom;   /* Y inverted: screen down = world down */

    DC_Point2 *p = dc_array_get(ed->pts, (size_t)ed->selected);
    if (!p) return;
    p->x = ed->orig_x + dwx;
    p->y = ed->orig_y + dwy;

    gtk_widget_queue_draw(dc_bezier_canvas_widget(ed->canvas));
}

/* -------------------------------------------------------------------------
 * Key handler — 'C' toggles selected point's juncture flag
 * ---------------------------------------------------------------------- */
static gboolean
on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
               GdkModifierType state, gpointer data)
{
    (void)ctrl; (void)keycode;
    DC_BezierEditor *ed = data;

    if (keyval == GDK_KEY_c || keyval == GDK_KEY_C) {
        if (state & GDK_SHIFT_MASK) {
            /* Shift+C: toggle global chain mode */
            ed->chain_mode = ed->chain_mode ? 0 : 1;
            g_signal_handler_block(ed->global_chain_btn,
                                   ed->global_chain_hid);
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(ed->global_chain_btn),
                ed->chain_mode ? TRUE : FALSE);
            g_signal_handler_unblock(ed->global_chain_btn,
                                     ed->global_chain_hid);
        } else {
            /* C: toggle selected point's local juncture flag */
            int count = (int)dc_array_length(ed->pts);
            int sel = ed->selected;
            if (sel > 0 && sel < count - 1) {
                uint8_t *flag = dc_array_get(ed->junctures, (size_t)sel);
                if (flag) *flag = (*flag) ? 0 : 1;
                update_chain_button(ed);
            }
        }
        update_status(ed);
        gtk_widget_queue_draw(dc_bezier_canvas_widget(ed->canvas));
        return TRUE;
    }
    return FALSE;
}

/* -------------------------------------------------------------------------
 * Global chain mode button callback
 * ---------------------------------------------------------------------- */
static void
on_global_chain_toggled(GtkToggleButton *btn, gpointer data)
{
    DC_BezierEditor *ed = data;
    ed->chain_mode = gtk_toggle_button_get_active(btn) ? 1 : 0;
    update_status(ed);
    gtk_widget_grab_focus(dc_bezier_canvas_widget(ed->canvas));
}

/* -------------------------------------------------------------------------
 * Local chain button callback
 * ---------------------------------------------------------------------- */
static void
on_chain_toggled(GtkToggleButton *btn, gpointer data)
{
    DC_BezierEditor *ed = data;
    int count = (int)dc_array_length(ed->pts);
    int sel = ed->selected;
    if (sel <= 0 || sel >= count - 1) return;

    uint8_t *flag = dc_array_get(ed->junctures, (size_t)sel);
    if (flag) *flag = gtk_toggle_button_get_active(btn) ? 1 : 0;

    update_status(ed);
    gtk_widget_queue_draw(dc_bezier_canvas_widget(ed->canvas));
    gtk_widget_grab_focus(dc_bezier_canvas_widget(ed->canvas));
}

/* -------------------------------------------------------------------------
 * Destroy notify
 * ---------------------------------------------------------------------- */
static void
editor_destroy_notify(gpointer data)
{
    dc_bezier_editor_free(data);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
DC_BezierEditor *
dc_bezier_editor_new(void)
{
    DC_BezierEditor *ed = calloc(1, sizeof(*ed));
    if (!ed) return NULL;

    ed->canvas = dc_bezier_canvas_new();
    if (!ed->canvas) { free(ed); return NULL; }

    ed->pts = dc_array_new(sizeof(DC_Point2));
    if (!ed->pts) { dc_bezier_canvas_free(ed->canvas); free(ed); return NULL; }

    ed->junctures = dc_array_new(sizeof(uint8_t));
    if (!ed->junctures) {
        dc_array_free(ed->pts);
        dc_bezier_canvas_free(ed->canvas);
        free(ed);
        return NULL;
    }

    ed->selected = -1;
    ed->mouse_down = 0;
    ed->chain_mode = 1;

    /* Register overlay for drawing points and curve */
    dc_bezier_canvas_set_overlay_cb(ed->canvas, editor_overlay, ed);

    GtkWidget *canvas_widget = dc_bezier_canvas_widget(ed->canvas);

    /* Click gesture for placing and selecting points */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_press), ed);
    g_signal_connect(click, "released", G_CALLBACK(on_release), ed);
    gtk_widget_add_controller(canvas_widget, GTK_EVENT_CONTROLLER(click));

    /* Motion controller for dragging */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), ed);
    gtk_widget_add_controller(canvas_widget, motion);

    /* Key controller for juncture toggle */
    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), ed);
    gtk_widget_add_controller(canvas_widget, key);

    /* --- Build widget hierarchy: container(V) -> toolbar(H) + canvas --- */
    ed->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(ed->container, TRUE);
    gtk_widget_set_vexpand(ed->container, TRUE);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);
    gtk_widget_set_margin_top(toolbar, 2);
    gtk_widget_set_margin_bottom(toolbar, 2);

    /* Global chain mode toggle (always active, default ON) */
    ed->global_chain_btn = gtk_toggle_button_new_with_label("Chain");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ed->global_chain_btn), TRUE);
    gtk_widget_set_focusable(ed->global_chain_btn, FALSE);
    gtk_widget_set_tooltip_text(ed->global_chain_btn,
                                "Global chain mode (Shift+C)");
    ed->global_chain_hid = g_signal_connect(ed->global_chain_btn, "toggled",
                                            G_CALLBACK(on_global_chain_toggled),
                                            ed);
    gtk_box_append(GTK_BOX(toolbar), ed->global_chain_btn);

    /* Local juncture toggle (active only when interior point selected) */
    ed->chain_btn = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(ed->chain_btn), "insert-link-symbolic");
    gtk_widget_set_focusable(ed->chain_btn, FALSE);
    gtk_widget_set_sensitive(ed->chain_btn, FALSE);
    gtk_widget_set_tooltip_text(ed->chain_btn, "Toggle point juncture (C)");
    ed->chain_handler_id = g_signal_connect(ed->chain_btn, "toggled",
                                            G_CALLBACK(on_chain_toggled), ed);

    gtk_box_append(GTK_BOX(toolbar), ed->chain_btn);
    gtk_box_append(GTK_BOX(ed->container), toolbar);

    /* Canvas fills remaining space */
    gtk_widget_set_vexpand(canvas_widget, TRUE);
    gtk_box_append(GTK_BOX(ed->container), canvas_widget);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "bezier editor created");
    return ed;
}

void
dc_bezier_editor_free(DC_BezierEditor *editor)
{
    if (!editor) return;
    dc_array_free(editor->junctures);
    dc_array_free(editor->pts);
    dc_bezier_canvas_free(editor->canvas);
    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "bezier editor freed");
    free(editor);
}

GtkWidget *
dc_bezier_editor_widget(DC_BezierEditor *editor)
{
    if (!editor) return NULL;
    return editor->container;
}

void
dc_bezier_editor_set_window(DC_BezierEditor *editor, GtkWidget *window)
{
    if (!editor) return;
    editor->window = window;
    dc_bezier_canvas_set_status_window(editor->canvas, window);
    if (window) {
        g_object_set_data_full(G_OBJECT(window), "dc-bezier-editor",
                               editor, editor_destroy_notify);
        update_status(editor);
    }
}

int
dc_bezier_editor_point_count(const DC_BezierEditor *editor)
{
    if (!editor) return 0;
    return (int)dc_array_length(editor->pts);
}

int
dc_bezier_editor_selected_point(const DC_BezierEditor *editor)
{
    if (!editor) return -1;
    return editor->selected;
}
