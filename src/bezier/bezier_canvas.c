#include "bezier/bezier_canvas.h"
#include "core/log.h"
#include "ui/app_window.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */
#define DC_CANVAS_DEFAULT_ZOOM  4.0   /* pixels per mm */
#define DC_CANVAS_MIN_ZOOM      0.1
#define DC_CANVAS_MAX_ZOOM      100.0
#define DC_CANVAS_ZOOM_FACTOR   1.15  /* per scroll step */

#define DC_CANVAS_GRID_MINOR    1.0   /* mm */
#define DC_CANVAS_GRID_MAJOR    10.0  /* mm */

/* Grid visibility threshold: minor grid drawn only when zoom >= this */
#define DC_CANVAS_MINOR_GRID_MIN_ZOOM  1.0

/* -------------------------------------------------------------------------
 * Opaque struct
 * ---------------------------------------------------------------------- */
struct DC_BezierCanvas {
    GtkWidget  *drawing_area;
    GtkWidget  *window;       /* borrowed; for status bar updates */

    /* View state */
    double      zoom;         /* pixels per mm */
    double      pan_x;        /* world X at center of viewport (mm) */
    double      pan_y;        /* world Y at center of viewport (mm) */

    /* Interaction state */
    int         space_held;
    int         panning;      /* 1 while a pan drag is in progress */
    double      drag_start_pan_x;
    double      drag_start_pan_y;
};

/* -------------------------------------------------------------------------
 * Forward declarations of static helpers
 * ---------------------------------------------------------------------- */
static void on_draw(GtkDrawingArea *area, cairo_t *cr,
                    int width, int height, gpointer data);

static gboolean on_scroll(GtkEventControllerScroll *ctrl,
                          double dx, double dy, gpointer data);

static void on_drag_begin_middle(GtkGestureDrag *gesture,
                                 double start_x, double start_y,
                                 gpointer data);
static void on_drag_update_middle(GtkGestureDrag *gesture,
                                  double offset_x, double offset_y,
                                  gpointer data);
static void on_drag_end_middle(GtkGestureDrag *gesture,
                               double offset_x, double offset_y,
                               gpointer data);

static void on_drag_begin_left(GtkGestureDrag *gesture,
                               double start_x, double start_y,
                               gpointer data);
static void on_drag_update_left(GtkGestureDrag *gesture,
                                double offset_x, double offset_y,
                                gpointer data);
static void on_drag_end_left(GtkGestureDrag *gesture,
                             double offset_x, double offset_y,
                             gpointer data);

static void on_motion(GtkEventControllerMotion *ctrl,
                      double x, double y, gpointer data);

static gboolean on_key_pressed(GtkEventControllerKey *ctrl,
                               guint keyval, guint keycode,
                               GdkModifierType state, gpointer data);
static void on_key_released(GtkEventControllerKey *ctrl,
                            guint keyval, guint keycode,
                            GdkModifierType state, gpointer data);

static void on_click_pressed(GtkGestureClick *gesture, int n_press,
                             double x, double y, gpointer data);

/* -------------------------------------------------------------------------
 * Drawing helpers
 * ---------------------------------------------------------------------- */
static void
draw_grid(cairo_t *cr, double zoom, double pan_x, double pan_y,
          int width, int height)
{
    /* Compute world-space bounds visible in the viewport */
    double half_w = (width  / 2.0) / zoom;
    double half_h = (height / 2.0) / zoom;

    double world_left   = pan_x - half_w;
    double world_right  = pan_x + half_w;
    double world_bottom = pan_y - half_h;
    double world_top    = pan_y + half_h;

    /* --- Minor grid (1 mm) --- */
    if (zoom >= DC_CANVAS_MINOR_GRID_MIN_ZOOM) {
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.4);
        cairo_set_line_width(cr, 1.0 / zoom);

        double start_x = floor(world_left / DC_CANVAS_GRID_MINOR) * DC_CANVAS_GRID_MINOR;
        double start_y = floor(world_bottom / DC_CANVAS_GRID_MINOR) * DC_CANVAS_GRID_MINOR;

        for (double x = start_x; x <= world_right; x += DC_CANVAS_GRID_MINOR) {
            cairo_move_to(cr, x, world_bottom);
            cairo_line_to(cr, x, world_top);
        }
        for (double y = start_y; y <= world_top; y += DC_CANVAS_GRID_MINOR) {
            cairo_move_to(cr, world_left, y);
            cairo_line_to(cr, world_right, y);
        }
        cairo_stroke(cr);
    }

    /* --- Major grid (10 mm) --- */
    cairo_set_source_rgba(cr, 0.4, 0.4, 0.4, 0.6);
    cairo_set_line_width(cr, 1.5 / zoom);

    double start_x = floor(world_left / DC_CANVAS_GRID_MAJOR) * DC_CANVAS_GRID_MAJOR;
    double start_y = floor(world_bottom / DC_CANVAS_GRID_MAJOR) * DC_CANVAS_GRID_MAJOR;

    for (double x = start_x; x <= world_right; x += DC_CANVAS_GRID_MAJOR) {
        cairo_move_to(cr, x, world_bottom);
        cairo_line_to(cr, x, world_top);
    }
    for (double y = start_y; y <= world_top; y += DC_CANVAS_GRID_MAJOR) {
        cairo_move_to(cr, world_left, y);
        cairo_line_to(cr, world_right, y);
    }
    cairo_stroke(cr);
}

static void
draw_origin_crosshair(cairo_t *cr, double zoom,
                      double pan_x, double pan_y,
                      int width, int height)
{
    double half_w = (width  / 2.0) / zoom;
    double half_h = (height / 2.0) / zoom;

    double world_left   = pan_x - half_w;
    double world_right  = pan_x + half_w;
    double world_bottom = pan_y - half_h;
    double world_top    = pan_y + half_h;

    double line_w = 2.0 / zoom;

    /* X axis — red */
    cairo_set_source_rgba(cr, 0.8, 0.2, 0.2, 0.9);
    cairo_set_line_width(cr, line_w);
    cairo_move_to(cr, world_left, 0.0);
    cairo_line_to(cr, world_right, 0.0);
    cairo_stroke(cr);

    /* Y axis — green */
    cairo_set_source_rgba(cr, 0.2, 0.8, 0.2, 0.9);
    cairo_set_line_width(cr, line_w);
    cairo_move_to(cr, 0.0, world_bottom);
    cairo_line_to(cr, 0.0, world_top);
    cairo_stroke(cr);
}

/* -------------------------------------------------------------------------
 * Draw callback
 * ---------------------------------------------------------------------- */
static void
on_draw(GtkDrawingArea *area, cairo_t *cr,
        int width, int height, gpointer data)
{
    (void)area;
    DC_BezierCanvas *c = data;

    /* Dark background */
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.14);
    cairo_paint(cr);

    /* Set up world-space transform:
     *   screen_center + (world - pan) * (zoom, -zoom)
     * Cairo transform: translate to center, scale, then translate by -pan.
     * Y is negated so world Y-up becomes screen Y-down. */
    cairo_translate(cr, width / 2.0, height / 2.0);
    cairo_scale(cr, c->zoom, -c->zoom);
    cairo_translate(cr, -c->pan_x, -c->pan_y);

    /* Draw in world coords from here */
    draw_grid(cr, c->zoom, c->pan_x, c->pan_y, width, height);
    draw_origin_crosshair(cr, c->zoom, c->pan_x, c->pan_y, width, height);
}

/* -------------------------------------------------------------------------
 * Scroll (zoom) handler
 * ---------------------------------------------------------------------- */
static gboolean
on_scroll(GtkEventControllerScroll *ctrl,
          double dx, double dy, gpointer data)
{
    (void)ctrl;
    (void)dx;
    DC_BezierCanvas *c = data;

    /* Get cursor position relative to the widget */
    GtkWidget *widget = c->drawing_area;
    /* We need the cursor position — grab it from the scroll controller's
     * current point (available via the motion controller cache, but for
     * simplicity we compute zoom-toward-center if we can't get it). */

    /* Compute old world position under cursor.  We approximate by using
     * the widget center since GTK4 scroll controllers don't directly
     * expose cursor coords.  The motion handler updates status separately. */
    int w = gtk_widget_get_width(widget);
    int h = gtk_widget_get_height(widget);

    /* Get cursor position from the event controller */
    double cx = w / 2.0;
    double cy = h / 2.0;

    /* Extract cursor position from the underlying event and convert
     * surface coords to widget-local via gtk_widget_compute_point. */
    GdkEvent *event = gtk_event_controller_get_current_event(
        GTK_EVENT_CONTROLLER(ctrl));
    if (event) {
        double ex, ey;
        gdk_event_get_position(event, &ex, &ey);
        graphene_point_t surface_pt = GRAPHENE_POINT_INIT((float)ex, (float)ey);
        graphene_point_t widget_pt;
        if (gtk_widget_compute_point(
                GTK_WIDGET(gtk_widget_get_root(widget)),
                widget, &surface_pt, &widget_pt)) {
            cx = widget_pt.x;
            cy = widget_pt.y;
        }
    }

    /* World coords under cursor before zoom */
    double world_x, world_y;
    dc_bezier_canvas_screen_to_world(c, cx, cy, &world_x, &world_y);

    /* Apply zoom.  dy is continuous (smooth scrolling), so scale the
     * factor by the magnitude rather than applying a fixed step. */
    double old_zoom = c->zoom;
    c->zoom *= pow(DC_CANVAS_ZOOM_FACTOR, -dy);

    /* Clamp */
    if (c->zoom < DC_CANVAS_MIN_ZOOM) c->zoom = DC_CANVAS_MIN_ZOOM;
    if (c->zoom > DC_CANVAS_MAX_ZOOM) c->zoom = DC_CANVAS_MAX_ZOOM;

    if (c->zoom != old_zoom) {
        /* Adjust pan so the world point under cursor stays fixed:
         * screen_pos = center + (world - pan) * zoom
         * => pan = world - (screen_pos - center) / zoom */
        double new_pan_x = world_x - (cx - w / 2.0) / c->zoom;
        double new_pan_y = world_y + (cy - h / 2.0) / c->zoom;

        /* Guard against NaN/inf from degenerate coordinates */
        if (isfinite(new_pan_x) && isfinite(new_pan_y)) {
            c->pan_x = new_pan_x;
            c->pan_y = new_pan_y;
        }

        gtk_widget_queue_draw(c->drawing_area);
    }

    return TRUE;
}

/* -------------------------------------------------------------------------
 * Middle-click pan
 * ---------------------------------------------------------------------- */
static void
on_drag_begin_middle(GtkGestureDrag *gesture, double start_x, double start_y,
                     gpointer data)
{
    (void)gesture; (void)start_x; (void)start_y;
    DC_BezierCanvas *c = data;
    c->panning = 1;
    c->drag_start_pan_x = c->pan_x;
    c->drag_start_pan_y = c->pan_y;
}

static void
on_drag_update_middle(GtkGestureDrag *gesture, double offset_x, double offset_y,
                      gpointer data)
{
    (void)gesture;
    DC_BezierCanvas *c = data;
    /* Screen offset -> world offset.  Note Y is inverted. */
    c->pan_x = c->drag_start_pan_x - offset_x / c->zoom;
    c->pan_y = c->drag_start_pan_y + offset_y / c->zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

static void
on_drag_end_middle(GtkGestureDrag *gesture, double offset_x, double offset_y,
                   gpointer data)
{
    (void)gesture; (void)offset_x; (void)offset_y;
    DC_BezierCanvas *c = data;
    c->panning = 0;
}

/* -------------------------------------------------------------------------
 * Space+left-click pan
 * ---------------------------------------------------------------------- */
static void
on_drag_begin_left(GtkGestureDrag *gesture, double start_x, double start_y,
                   gpointer data)
{
    DC_BezierCanvas *c = data;
    if (!c->space_held) {
        /* Not space-dragging — let the gesture fail so it doesn't
         * interfere with future editors (Phase 2.3). */
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    (void)start_x; (void)start_y;
    c->panning = 1;
    c->drag_start_pan_x = c->pan_x;
    c->drag_start_pan_y = c->pan_y;
}

static void
on_drag_update_left(GtkGestureDrag *gesture, double offset_x, double offset_y,
                    gpointer data)
{
    (void)gesture;
    DC_BezierCanvas *c = data;
    if (!c->panning) return;
    c->pan_x = c->drag_start_pan_x - offset_x / c->zoom;
    c->pan_y = c->drag_start_pan_y + offset_y / c->zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

static void
on_drag_end_left(GtkGestureDrag *gesture, double offset_x, double offset_y,
                 gpointer data)
{
    (void)gesture; (void)offset_x; (void)offset_y;
    DC_BezierCanvas *c = data;
    c->panning = 0;
}

/* -------------------------------------------------------------------------
 * Motion (cursor tracking / status bar)
 * ---------------------------------------------------------------------- */
static void
on_motion(GtkEventControllerMotion *ctrl, double x, double y, gpointer data)
{
    (void)ctrl;
    DC_BezierCanvas *c = data;

    double wx, wy;
    dc_bezier_canvas_screen_to_world(c, x, y, &wx, &wy);

    if (c->window) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "X: %.2f mm  Y: %.2f mm  Zoom: %.0f%%",
                 wx, wy, c->zoom * 100.0 / DC_CANVAS_DEFAULT_ZOOM);
        dc_app_window_set_status(c->window, buf);
    }
}

/* -------------------------------------------------------------------------
 * Key handlers (space for pan mode)
 * ---------------------------------------------------------------------- */
static gboolean
on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
               GdkModifierType state, gpointer data)
{
    (void)ctrl; (void)keycode; (void)state;
    DC_BezierCanvas *c = data;
    if (keyval == GDK_KEY_space) {
        c->space_held = 1;
        return TRUE;
    }
    return FALSE;
}

static void
on_key_released(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                GdkModifierType state, gpointer data)
{
    (void)ctrl; (void)keycode; (void)state;
    DC_BezierCanvas *c = data;
    if (keyval == GDK_KEY_space) {
        c->space_held = 0;
    }
}

/* -------------------------------------------------------------------------
 * Click handler (grab focus)
 * ---------------------------------------------------------------------- */
static void
on_click_pressed(GtkGestureClick *gesture, int n_press,
                 double x, double y, gpointer data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    DC_BezierCanvas *c = data;
    gtk_widget_grab_focus(c->drawing_area);
}

/* -------------------------------------------------------------------------
 * Destroy notify for g_object_set_data_full
 * ---------------------------------------------------------------------- */
static void
canvas_destroy_notify(gpointer data)
{
    dc_bezier_canvas_free(data);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
DC_BezierCanvas *
dc_bezier_canvas_new(void)
{
    DC_BezierCanvas *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->zoom  = DC_CANVAS_DEFAULT_ZOOM;
    c->pan_x = 0.0;
    c->pan_y = 0.0;

    /* Create the GtkDrawingArea */
    c->drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(c->drawing_area),
                                   on_draw, c, NULL);
    gtk_widget_set_hexpand(c->drawing_area, TRUE);
    gtk_widget_set_vexpand(c->drawing_area, TRUE);
    gtk_widget_set_focusable(c->drawing_area, TRUE);

    /* --- Event controllers --- */

    /* Scroll (zoom) */
    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), c);
    gtk_widget_add_controller(c->drawing_area, scroll);

    /* Middle-click drag (pan) */
    GtkGesture *drag_middle = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag_middle), 2);
    g_signal_connect(drag_middle, "drag-begin",
                     G_CALLBACK(on_drag_begin_middle), c);
    g_signal_connect(drag_middle, "drag-update",
                     G_CALLBACK(on_drag_update_middle), c);
    g_signal_connect(drag_middle, "drag-end",
                     G_CALLBACK(on_drag_end_middle), c);
    gtk_widget_add_controller(c->drawing_area, GTK_EVENT_CONTROLLER(drag_middle));

    /* Left-click drag (space+drag pan) */
    GtkGesture *drag_left = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag_left), 1);
    g_signal_connect(drag_left, "drag-begin",
                     G_CALLBACK(on_drag_begin_left), c);
    g_signal_connect(drag_left, "drag-update",
                     G_CALLBACK(on_drag_update_left), c);
    g_signal_connect(drag_left, "drag-end",
                     G_CALLBACK(on_drag_end_left), c);
    gtk_widget_add_controller(c->drawing_area, GTK_EVENT_CONTROLLER(drag_left));

    /* Motion (cursor tracking) */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), c);
    gtk_widget_add_controller(c->drawing_area, motion);

    /* Key events (space press/release) */
    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), c);
    g_signal_connect(key, "key-released", G_CALLBACK(on_key_released), c);
    gtk_widget_add_controller(c->drawing_area, key);

    /* Click (grab focus) */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_click_pressed), c);
    gtk_widget_add_controller(c->drawing_area, GTK_EVENT_CONTROLLER(click));

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_APP,
           "bezier canvas created (zoom=%.1f px/mm)", c->zoom);

    return c;
}

void
dc_bezier_canvas_free(DC_BezierCanvas *canvas)
{
    if (!canvas) return;
    /* The GtkWidget is owned by GTK (its parent container).
     * We only free our struct. */
    dc_log(DC_LOG_DEBUG, DC_LOG_EVENT_APP, "bezier canvas freed");
    free(canvas);
}

GtkWidget *
dc_bezier_canvas_widget(DC_BezierCanvas *canvas)
{
    if (!canvas) return NULL;
    return canvas->drawing_area;
}

void
dc_bezier_canvas_set_window(DC_BezierCanvas *canvas, GtkWidget *window)
{
    if (!canvas) return;
    canvas->window = window;
    if (window) {
        g_object_set_data_full(G_OBJECT(window), "dc-bezier-canvas",
                               canvas, canvas_destroy_notify);
    }
}

void
dc_bezier_canvas_set_zoom(DC_BezierCanvas *canvas, double zoom)
{
    if (!canvas) return;
    if (zoom < DC_CANVAS_MIN_ZOOM) zoom = DC_CANVAS_MIN_ZOOM;
    if (zoom > DC_CANVAS_MAX_ZOOM) zoom = DC_CANVAS_MAX_ZOOM;
    canvas->zoom = zoom;
    gtk_widget_queue_draw(canvas->drawing_area);
}

void
dc_bezier_canvas_screen_to_world(DC_BezierCanvas *canvas,
                                 double sx, double sy,
                                 double *wx, double *wy)
{
    if (!canvas) return;

    int w = gtk_widget_get_width(canvas->drawing_area);
    int h = gtk_widget_get_height(canvas->drawing_area);

    /* Inverse of the Cairo transform:
     *   screen = center + (world - pan) * (zoom, -zoom)
     *   world  = pan + (screen - center) / (zoom, -zoom) */
    if (wx) *wx = canvas->pan_x + (sx - w / 2.0) / canvas->zoom;
    if (wy) *wy = canvas->pan_y - (sy - h / 2.0) / canvas->zoom;
}

void
dc_bezier_canvas_world_to_screen(DC_BezierCanvas *canvas,
                                 double wx, double wy,
                                 double *sx, double *sy)
{
    if (!canvas) return;

    int w = gtk_widget_get_width(canvas->drawing_area);
    int h = gtk_widget_get_height(canvas->drawing_area);

    /* screen = center + (world - pan) * (zoom, -zoom) */
    if (sx) *sx = w / 2.0 + (wx - canvas->pan_x) * canvas->zoom;
    if (sy) *sy = h / 2.0 - (wy - canvas->pan_y) * canvas->zoom;
}
