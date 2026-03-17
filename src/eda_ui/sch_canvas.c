#define _POSIX_C_SOURCE 200809L

#include "sch_canvas.h"
#include "sch_symbol_render.h"
#include "eda/eda_schematic.h"
#include "eda/eda_library.h"
#include "core/log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Constants
 * ========================================================================= */
#define SCH_GRID_MILS   50.0    /* 50-mil (1.27mm) grid — KiCad standard */
#define SCH_ZOOM_MIN    0.1
#define SCH_ZOOM_MAX    20.0
#define SCH_ZOOM_DEFAULT 2.0    /* pixels per mil */

/* =========================================================================
 * Internal state
 * ========================================================================= */
struct DC_SchCanvas {
    GtkWidget      *drawing_area;

    /* View */
    double          zoom;        /* pixels per mil */
    double          pan_x;       /* world X center (mils) */
    double          pan_y;       /* world Y center (mils) */

    /* Data (borrowed) */
    DC_ESchematic  *sch;
    DC_ELibrary    *lib;

    /* Interaction */
    int             selected_symbol; /* -1 = none */
    int             dragging;
    double          drag_start_x, drag_start_y;
    double          pan_start_x, pan_start_y;
    int             space_held;
};

/* =========================================================================
 * Coordinate transforms
 * ========================================================================= */
void
dc_sch_canvas_screen_to_world(DC_SchCanvas *c,
                               double sx, double sy,
                               double *wx, double *wy)
{
    if (!c) return;
    int w = gtk_widget_get_width(c->drawing_area);
    int h = gtk_widget_get_height(c->drawing_area);
    if (wx) *wx = (sx - w / 2.0) / c->zoom + c->pan_x;
    if (wy) *wy = (sy - h / 2.0) / c->zoom + c->pan_y;
}

void
dc_sch_canvas_world_to_screen(DC_SchCanvas *c,
                               double wx, double wy,
                               double *sx, double *sy)
{
    if (!c) return;
    int w = gtk_widget_get_width(c->drawing_area);
    int h = gtk_widget_get_height(c->drawing_area);
    if (sx) *sx = (wx - c->pan_x) * c->zoom + w / 2.0;
    if (sy) *sy = (wy - c->pan_y) * c->zoom + h / 2.0;
}

/* =========================================================================
 * Grid rendering
 * ========================================================================= */
static void
draw_grid(DC_SchCanvas *c, cairo_t *cr, int width, int height)
{
    double wl, wt, wr, wb;
    dc_sch_canvas_screen_to_world(c, 0, 0, &wl, &wt);
    dc_sch_canvas_screen_to_world(c, width, height, &wr, &wb);

    double grid = SCH_GRID_MILS;
    /* Coarsen if too dense */
    double screen_grid = grid * c->zoom;
    while (screen_grid < 10.0) {
        grid *= 2.0;
        screen_grid *= 2.0;
    }

    /* Minor grid dots */
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.3);
    double start_x = floor(wl / grid) * grid;
    double start_y = floor(wt / grid) * grid;

    for (double gy = start_y; gy <= wb; gy += grid) {
        for (double gx = start_x; gx <= wr; gx += grid) {
            double sx, sy;
            dc_sch_canvas_world_to_screen(c, gx, gy, &sx, &sy);
            cairo_rectangle(cr, sx - 0.5, sy - 0.5, 1, 1);
        }
    }
    cairo_fill(cr);

    /* Major grid lines (every 200 mils = 4 grid steps) */
    double major = grid * 4.0;
    cairo_set_source_rgba(cr, 0.4, 0.4, 0.4, 0.2);
    cairo_set_line_width(cr, 1.0);

    double ms_x = floor(wl / major) * major;
    for (double gx = ms_x; gx <= wr; gx += major) {
        double sx, sy_unused;
        dc_sch_canvas_world_to_screen(c, gx, 0, &sx, &sy_unused);
        cairo_move_to(cr, sx, 0);
        cairo_line_to(cr, sx, height);
    }
    double ms_y = floor(wt / major) * major;
    for (double gy = ms_y; gy <= wb; gy += major) {
        double sx_unused, sy;
        dc_sch_canvas_world_to_screen(c, 0, gy, &sx_unused, &sy);
        cairo_move_to(cr, 0, sy);
        cairo_line_to(cr, width, sy);
    }
    cairo_stroke(cr);

    /* Origin crosshair */
    cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.4);
    double ox, oy;
    dc_sch_canvas_world_to_screen(c, 0, 0, &ox, &oy);
    cairo_move_to(cr, ox, 0);
    cairo_line_to(cr, ox, height);
    cairo_move_to(cr, 0, oy);
    cairo_line_to(cr, width, oy);
    cairo_stroke(cr);
}

/* =========================================================================
 * Schematic element rendering
 * ========================================================================= */
static void
draw_schematic(DC_SchCanvas *c, cairo_t *cr)
{
    if (!c->sch) return;

    /* Draw wires */
    cairo_set_source_rgb(cr, 0.0, 0.6, 0.0);
    cairo_set_line_width(cr, 2.0);
    for (size_t i = 0; i < dc_eschematic_wire_count(c->sch); i++) {
        DC_SchWire *w = dc_eschematic_get_wire(c->sch, i);
        double sx1, sy1, sx2, sy2;
        dc_sch_canvas_world_to_screen(c, w->x1, w->y1, &sx1, &sy1);
        dc_sch_canvas_world_to_screen(c, w->x2, w->y2, &sx2, &sy2);
        cairo_move_to(cr, sx1, sy1);
        cairo_line_to(cr, sx2, sy2);
    }
    cairo_stroke(cr);

    /* Draw junctions */
    cairo_set_source_rgb(cr, 0.0, 0.6, 0.0);
    for (size_t i = 0; i < dc_eschematic_junction_count(c->sch); i++) {
        DC_SchJunction *j = dc_eschematic_get_junction(c->sch, i);
        double sx, sy;
        dc_sch_canvas_world_to_screen(c, j->x, j->y, &sx, &sy);
        cairo_arc(cr, sx, sy, 4.0, 0, 2 * G_PI);
        cairo_fill(cr);
    }

    /* Draw symbols */
    for (size_t i = 0; i < dc_eschematic_symbol_count(c->sch); i++) {
        DC_SchSymbol *sym = dc_eschematic_get_symbol(c->sch, i);
        int selected = ((int)i == c->selected_symbol);

        dc_sch_symbol_render(cr, c, sym, c->lib, selected);
    }

    /* Draw labels */
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.8);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);
    for (size_t i = 0; i < dc_eschematic_label_count(c->sch); i++) {
        DC_SchLabel *l = dc_eschematic_get_label(c->sch, i);
        double sx, sy;
        dc_sch_canvas_world_to_screen(c, l->x, l->y, &sx, &sy);
        cairo_move_to(cr, sx, sy - 4);
        cairo_show_text(cr, l->name);
    }

    /* Draw power ports */
    cairo_set_source_rgb(cr, 0.8, 0.0, 0.0);
    for (size_t i = 0; i < dc_eschematic_power_port_count(c->sch); i++) {
        DC_SchPowerPort *pp = dc_eschematic_get_power_port(c->sch, i);
        double sx, sy;
        dc_sch_canvas_world_to_screen(c, pp->x, pp->y, &sx, &sy);

        /* Simple power symbol: small circle + name */
        cairo_arc(cr, sx, sy, 5.0, 0, 2 * G_PI);
        cairo_stroke(cr);
        cairo_move_to(cr, sx + 8, sy + 4);
        cairo_show_text(cr, pp->name);
    }
}

/* =========================================================================
 * Draw callback
 * ========================================================================= */
static void
on_draw(GtkDrawingArea *area, cairo_t *cr,
        int width, int height, gpointer userdata)
{
    (void)area;
    DC_SchCanvas *c = userdata;

    /* Background */
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.14);
    cairo_paint(cr);

    draw_grid(c, cr, width, height);
    draw_schematic(c, cr);
}

/* =========================================================================
 * Event handlers
 * ========================================================================= */
static void
on_scroll(GtkEventControllerScroll *ctrl, double dx, double dy,
          gpointer userdata)
{
    (void)ctrl;
    (void)dx;
    DC_SchCanvas *c = userdata;

    double factor = (dy < 0) ? 1.15 : 1.0 / 1.15;
    double new_zoom = c->zoom * factor;
    if (new_zoom < SCH_ZOOM_MIN) new_zoom = SCH_ZOOM_MIN;
    if (new_zoom > SCH_ZOOM_MAX) new_zoom = SCH_ZOOM_MAX;
    c->zoom = new_zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

static void
on_pressed(GtkGestureClick *gesture, int n_press,
           double x, double y, gpointer userdata)
{
    (void)gesture;
    (void)n_press;
    DC_SchCanvas *c = userdata;

    c->dragging = 1;
    c->drag_start_x = x;
    c->drag_start_y = y;
    c->pan_start_x = c->pan_x;
    c->pan_start_y = c->pan_y;
}

static void
on_released(GtkGestureClick *gesture, int n_press,
            double x, double y, gpointer userdata)
{
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    DC_SchCanvas *c = userdata;
    c->dragging = 0;
}

static void
on_motion(GtkEventControllerMotion *ctrl, double x, double y,
          gpointer userdata)
{
    (void)ctrl;
    DC_SchCanvas *c = userdata;

    if (c->dragging) {
        double dx = (x - c->drag_start_x) / c->zoom;
        double dy = (y - c->drag_start_y) / c->zoom;
        c->pan_x = c->pan_start_x - dx;
        c->pan_y = c->pan_start_y - dy;
        gtk_widget_queue_draw(c->drawing_area);
    }
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
DC_SchCanvas *
dc_sch_canvas_new(void)
{
    DC_SchCanvas *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->zoom = SCH_ZOOM_DEFAULT;
    c->selected_symbol = -1;

    c->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(c->drawing_area, TRUE);
    gtk_widget_set_vexpand(c->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(c->drawing_area),
                                    on_draw, c, NULL);

    /* Scroll to zoom */
    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), c);
    gtk_widget_add_controller(c->drawing_area, scroll);

    /* Click to start pan */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), c);
    g_signal_connect(click, "released", G_CALLBACK(on_released), c);
    gtk_widget_add_controller(c->drawing_area, GTK_EVENT_CONTROLLER(click));

    /* Motion for drag-pan */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), c);
    gtk_widget_add_controller(c->drawing_area, motion);

    gtk_widget_set_focusable(c->drawing_area, TRUE);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "Schematic canvas created");
    return c;
}

void
dc_sch_canvas_free(DC_SchCanvas *c)
{
    if (!c) return;
    /* GtkWidget lifetime managed by GTK */
    free(c);
}

GtkWidget *
dc_sch_canvas_widget(DC_SchCanvas *c)
{
    return c ? c->drawing_area : NULL;
}

void dc_sch_canvas_set_schematic(DC_SchCanvas *c, DC_ESchematic *sch)
{
    if (!c) return;
    c->sch = sch;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_sch_canvas_set_library(DC_SchCanvas *c, DC_ELibrary *lib)
{
    if (!c) return;
    c->lib = lib;
}

void dc_sch_canvas_set_zoom(DC_SchCanvas *c, double zoom)
{
    if (!c) return;
    if (zoom < SCH_ZOOM_MIN) zoom = SCH_ZOOM_MIN;
    if (zoom > SCH_ZOOM_MAX) zoom = SCH_ZOOM_MAX;
    c->zoom = zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

double dc_sch_canvas_get_zoom(const DC_SchCanvas *c)
{
    return c ? c->zoom : 1.0;
}

void dc_sch_canvas_set_pan(DC_SchCanvas *c, double x, double y)
{
    if (!c) return;
    c->pan_x = x;
    c->pan_y = y;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_sch_canvas_get_pan(const DC_SchCanvas *c, double *x, double *y)
{
    if (!c) return;
    if (x) *x = c->pan_x;
    if (y) *y = c->pan_y;
}

void dc_sch_canvas_queue_redraw(DC_SchCanvas *c)
{
    if (c && c->drawing_area) gtk_widget_queue_draw(c->drawing_area);
}

int dc_sch_canvas_get_selected_symbol(const DC_SchCanvas *c)
{
    return c ? c->selected_symbol : -1;
}

void dc_sch_canvas_set_selected_symbol(DC_SchCanvas *c, int index)
{
    if (!c) return;
    c->selected_symbol = index;
    gtk_widget_queue_draw(c->drawing_area);
}

int
dc_sch_canvas_render_to_png(DC_SchCanvas *c, const char *path,
                             int width, int height)
{
    if (!c || !path) return -1;

    if (width <= 0) width = gtk_widget_get_width(c->drawing_area);
    if (height <= 0) height = gtk_widget_get_height(c->drawing_area);
    if (width <= 0 || height <= 0) return -1;

    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    on_draw(GTK_DRAWING_AREA(c->drawing_area), cr, width, height, c);

    cairo_status_t status = cairo_surface_write_to_png(surface, path);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return (status == CAIRO_STATUS_SUCCESS) ? 0 : -1;
}
