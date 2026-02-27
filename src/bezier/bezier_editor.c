#include "bezier/bezier_editor.h"
#include "bezier/bezier_canvas.h"
#include "bezier/bezier_curve.h"   /* DC_Point2 */
#include "core/array.h"
#include "core/log.h"
#include "ui/app_window.h"

#include <math.h>
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
    int              selected;      /* -1 or index into pts */
    int              mouse_down;    /* 1 while button 1 is held */
    double           orig_x;       /* world pos of selected point at drag start */
    double           orig_y;
    double           press_sx;     /* screen pos at press (for computing delta) */
    double           press_sy;
    GtkWidget       *window;        /* borrowed */
};

/* -------------------------------------------------------------------------
 * Status bar helper
 * ---------------------------------------------------------------------- */
static void
update_status(DC_BezierEditor *ed)
{
    if (!ed->window) return;
    int count = (int)dc_array_length(ed->pts);
    int num_segments = (count >= 3) ? (count - 1) / 2 : 0;
    char buf[128];
    if (num_segments == 0) {
        snprintf(buf, sizeof(buf), "Click to place points  (%d placed)", count);
    } else if (ed->selected >= 0) {
        const char *kind = (ed->selected % 2 == 1) ? "control" : "endpoint";
        snprintf(buf, sizeof(buf), "%d segment%s  |  Dragging P%d (%s)",
                 num_segments, num_segments == 1 ? "" : "s",
                 ed->selected, kind);
    } else {
        snprintf(buf, sizeof(buf), "%d segment%s  |  Click to add point or drag existing",
                 num_segments, num_segments == 1 ? "" : "s");
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
 * Overlay — draw control polygon, quadratic bezier curve, and points
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

    /* Quadratic bezier segments: segment i uses points 2i, 2i+1, 2i+2 */
    int num_segments = (count >= 3) ? (count - 1) / 2 : 0;
    if (num_segments > 0) {
        cairo_set_source_rgba(cr, 0.0, 1.0, 0.8, 1.0);  /* bright cyan */
        cairo_set_line_width(cr, 3.0);

        for (int seg = 0; seg < num_segments; seg++) {
            int i0 = 2 * seg;
            int i1 = 2 * seg + 1;
            int i2 = 2 * seg + 2;
            cairo_move_to(cr, sx[i0], sy[i0]);
            for (int step = 1; step <= DC_CURVE_STEPS; step++) {
                double t = (double)step / DC_CURVE_STEPS;
                double u = 1.0 - t;
                double bx = u*u*sx[i0] + 2.0*u*t*sx[i1] + t*t*sx[i2];
                double by = u*u*sy[i0] + 2.0*u*t*sy[i1] + t*t*sy[i2];
                cairo_line_to(cr, bx, by);
            }
        }
        cairo_stroke(cr);
    }

    /* Control point dots */
    for (int i = 0; i < count; i++) {
        if (i == ed->selected) {
            cairo_set_source_rgba(cr, 1.0, 0.6, 0.1, 1.0);   /* orange */
        } else if (i % 2 == 1) {
            cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, 1.0);   /* blue: off-curve */
        } else {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);   /* white: on-curve */
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
        /* Place new control point */
        DC_Point2 pt = { wx, wy };
        dc_array_push(ed->pts, &pt);
        ed->selected = (int)dc_array_length(ed->pts) - 1;
        ed->mouse_down = 1;
        ed->orig_x = wx;
        ed->orig_y = wy;
        ed->press_sx = x;
        ed->press_sy = y;
    }

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

    ed->selected = -1;
    ed->mouse_down = 0;

    /* Register overlay for drawing points and curve */
    dc_bezier_canvas_set_overlay_cb(ed->canvas, editor_overlay, ed);

    GtkWidget *widget = dc_bezier_canvas_widget(ed->canvas);

    /* Click gesture for placing and selecting points */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_press), ed);
    g_signal_connect(click, "released", G_CALLBACK(on_release), ed);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

    /* Motion controller for dragging */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), ed);
    gtk_widget_add_controller(widget, motion);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP, "bezier editor created");
    return ed;
}

void
dc_bezier_editor_free(DC_BezierEditor *editor)
{
    if (!editor) return;
    dc_array_free(editor->pts);
    dc_bezier_canvas_free(editor->canvas);
    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "bezier editor freed");
    free(editor);
}

GtkWidget *
dc_bezier_editor_widget(DC_BezierEditor *editor)
{
    if (!editor) return NULL;
    return dc_bezier_canvas_widget(editor->canvas);
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
