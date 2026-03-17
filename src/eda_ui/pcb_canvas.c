#define _POSIX_C_SOURCE 200809L

#include "pcb_canvas.h"
#include "eda/eda_pcb.h"
#include "eda/eda_ratsnest.h"
#include "eda/eda_library.h"
#include "core/log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Constants
 * ========================================================================= */
#define PCB_ZOOM_MIN    0.5
#define PCB_ZOOM_MAX    200.0
#define PCB_ZOOM_DEFAULT 10.0   /* pixels per mm */

/* Standard layer colors (r, g, b, a) */
typedef struct { double r, g, b, a; } LayerColor;

static const LayerColor LAYER_COLORS[] = {
    [DC_PCB_LAYER_F_CU]      = { 0.8, 0.0, 0.0, 0.8 },  /* red */
    [DC_PCB_LAYER_B_CU]      = { 0.0, 0.0, 0.8, 0.8 },  /* blue */
    [DC_PCB_LAYER_F_SILKS]   = { 1.0, 1.0, 1.0, 0.7 },  /* white */
    [DC_PCB_LAYER_B_SILKS]   = { 0.0, 0.8, 0.8, 0.7 },  /* cyan */
    [DC_PCB_LAYER_F_MASK]    = { 0.5, 0.0, 0.5, 0.3 },  /* purple */
    [DC_PCB_LAYER_B_MASK]    = { 0.0, 0.5, 0.5, 0.3 },  /* teal */
    [DC_PCB_LAYER_EDGE_CUTS] = { 0.9, 0.9, 0.0, 1.0 },  /* yellow */
};
#define N_LAYER_COLORS ((int)(sizeof(LAYER_COLORS) / sizeof(LAYER_COLORS[0])))

static LayerColor get_layer_color(int layer_id)
{
    if (layer_id >= 0 && layer_id < N_LAYER_COLORS &&
        (LAYER_COLORS[layer_id].r > 0 || LAYER_COLORS[layer_id].g > 0 ||
         LAYER_COLORS[layer_id].b > 0)) {
        return LAYER_COLORS[layer_id];
    }
    /* Default for inner layers */
    return (LayerColor){ 0.6, 0.6, 0.0, 0.6 };
}

/* =========================================================================
 * Internal state
 * ========================================================================= */
struct DC_PcbCanvas {
    GtkWidget      *drawing_area;
    double          zoom;          /* pixels per mm */
    double          pan_x, pan_y;  /* world center (mm) */

    DC_EPcb        *pcb;           /* borrowed */
    DC_ELibrary    *lib;           /* borrowed */
    DC_Ratsnest    *ratsnest;      /* borrowed */

    int             active_layer;
    int             selected_fp;   /* -1 = none */
    unsigned char   layer_visible[DC_PCB_LAYER_COUNT]; /* 1=visible */

    int             dragging;
    double          drag_start_x, drag_start_y;
    double          pan_start_x, pan_start_y;
};

/* =========================================================================
 * Coordinate transforms
 * ========================================================================= */
void dc_pcb_canvas_screen_to_world(DC_PcbCanvas *c, double sx, double sy,
                                    double *wx, double *wy)
{
    if (!c) return;
    int w = gtk_widget_get_width(c->drawing_area);
    int h = gtk_widget_get_height(c->drawing_area);
    if (wx) *wx = (sx - w / 2.0) / c->zoom + c->pan_x;
    if (wy) *wy = (sy - h / 2.0) / c->zoom + c->pan_y;
}

void dc_pcb_canvas_world_to_screen(DC_PcbCanvas *c, double wx, double wy,
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
static void draw_grid(DC_PcbCanvas *c, cairo_t *cr, int width, int height)
{
    double wl, wt, wr, wb;
    dc_pcb_canvas_screen_to_world(c, 0, 0, &wl, &wt);
    dc_pcb_canvas_screen_to_world(c, width, height, &wr, &wb);

    /* Fine grid: 0.1mm, coarse: 1mm */
    double fine_grid = 0.1;
    double coarse_grid = 1.0;

    double screen_fine = fine_grid * c->zoom;
    if (screen_fine < 4.0) fine_grid = coarse_grid; /* skip fine if too dense */

    /* Fine grid dots */
    if (screen_fine >= 4.0) {
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.4);
        double sx_start = floor(wl / fine_grid) * fine_grid;
        double sy_start = floor(wt / fine_grid) * fine_grid;
        for (double gy = sy_start; gy <= wb; gy += fine_grid) {
            for (double gx = sx_start; gx <= wr; gx += fine_grid) {
                double sx, sy;
                dc_pcb_canvas_world_to_screen(c, gx, gy, &sx, &sy);
                cairo_rectangle(cr, sx - 0.5, sy - 0.5, 1, 1);
            }
        }
        cairo_fill(cr);
    }

    /* Coarse grid lines */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.3);
    cairo_set_line_width(cr, 1.0);
    double cx_start = floor(wl / coarse_grid) * coarse_grid;
    for (double gx = cx_start; gx <= wr; gx += coarse_grid) {
        double sx, sy_unused;
        dc_pcb_canvas_world_to_screen(c, gx, 0, &sx, &sy_unused);
        cairo_move_to(cr, sx, 0);
        cairo_line_to(cr, sx, height);
    }
    double cy_start = floor(wt / coarse_grid) * coarse_grid;
    for (double gy = cy_start; gy <= wb; gy += coarse_grid) {
        double sx_unused, sy;
        dc_pcb_canvas_world_to_screen(c, 0, gy, &sx_unused, &sy);
        cairo_move_to(cr, 0, sy);
        cairo_line_to(cr, width, sy);
    }
    cairo_stroke(cr);

    /* Origin crosshair */
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.5);
    double ox, oy;
    dc_pcb_canvas_world_to_screen(c, 0, 0, &ox, &oy);
    cairo_move_to(cr, ox, 0); cairo_line_to(cr, ox, height);
    cairo_move_to(cr, 0, oy); cairo_line_to(cr, width, oy);
    cairo_stroke(cr);
}

/* =========================================================================
 * PCB element rendering
 * ========================================================================= */
static void draw_pcb(DC_PcbCanvas *c, cairo_t *cr)
{
    if (!c->pcb) return;

    /* Draw zones (filled polygons) */
    for (size_t i = 0; i < dc_epcb_zone_count(c->pcb); i++) {
        DC_PcbZone *z = dc_epcb_get_zone(c->pcb, i);
        if (!c->layer_visible[z->layer]) continue;

        LayerColor lc = get_layer_color(z->layer);
        cairo_set_source_rgba(cr, lc.r, lc.g, lc.b, lc.a * 0.2);

        if (z->outline && dc_array_length(z->outline) > 2) {
            DC_PcbZoneVertex *v0 = dc_array_get(z->outline, 0);
            double sx, sy;
            dc_pcb_canvas_world_to_screen(c, v0->x, v0->y, &sx, &sy);
            cairo_move_to(cr, sx, sy);
            for (size_t j = 1; j < dc_array_length(z->outline); j++) {
                DC_PcbZoneVertex *v = dc_array_get(z->outline, j);
                dc_pcb_canvas_world_to_screen(c, v->x, v->y, &sx, &sy);
                cairo_line_to(cr, sx, sy);
            }
            cairo_close_path(cr);
            cairo_fill(cr);
        }
    }

    /* Draw tracks */
    for (size_t i = 0; i < dc_epcb_track_count(c->pcb); i++) {
        DC_PcbTrack *t = dc_epcb_get_track(c->pcb, i);
        if (!c->layer_visible[t->layer]) continue;

        LayerColor lc = get_layer_color(t->layer);
        cairo_set_source_rgba(cr, lc.r, lc.g, lc.b, lc.a);

        double w = t->width * c->zoom;
        if (w < 1.0) w = 1.0;
        cairo_set_line_width(cr, w);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

        double sx1, sy1, sx2, sy2;
        dc_pcb_canvas_world_to_screen(c, t->x1, t->y1, &sx1, &sy1);
        dc_pcb_canvas_world_to_screen(c, t->x2, t->y2, &sx2, &sy2);
        cairo_move_to(cr, sx1, sy1);
        cairo_line_to(cr, sx2, sy2);
        cairo_stroke(cr);
    }

    /* Draw vias */
    for (size_t i = 0; i < dc_epcb_via_count(c->pcb); i++) {
        DC_PcbVia *v = dc_epcb_get_via(c->pcb, i);

        double sx, sy;
        dc_pcb_canvas_world_to_screen(c, v->x, v->y, &sx, &sy);
        double r = (v->size / 2.0) * c->zoom;
        if (r < 2.0) r = 2.0;
        double dr = (v->drill / 2.0) * c->zoom;

        /* Annular ring */
        cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.9);
        cairo_arc(cr, sx, sy, r, 0, 2 * G_PI);
        cairo_fill(cr);

        /* Drill hole */
        cairo_set_source_rgba(cr, 0.1, 0.1, 0.12, 1.0);
        cairo_arc(cr, sx, sy, dr, 0, 2 * G_PI);
        cairo_fill(cr);
    }

    /* Draw footprints */
    for (size_t i = 0; i < dc_epcb_footprint_count(c->pcb); i++) {
        DC_PcbFootprint *fp = dc_epcb_get_footprint(c->pcb, i);
        if (!c->layer_visible[fp->layer]) continue;

        double sx, sy;
        dc_pcb_canvas_world_to_screen(c, fp->x, fp->y, &sx, &sy);

        int selected = ((int)i == c->selected_fp);
        LayerColor lc = get_layer_color(fp->layer);

        /* Courtyard box */
        double bw = 3.0 * c->zoom; /* approximate 3mm body */
        double bh = 2.0 * c->zoom;
        if (selected)
            cairo_set_source_rgba(cr, 0.3, 0.8, 1.0, 0.8);
        else
            cairo_set_source_rgba(cr, lc.r, lc.g, lc.b, 0.5);

        cairo_set_line_width(cr, 1.5);
        cairo_rectangle(cr, sx - bw / 2, sy - bh / 2, bw, bh);
        cairo_stroke(cr);

        /* Pads */
        if (fp->pads) {
            for (size_t pi = 0; pi < dc_array_length(fp->pads); pi++) {
                DC_PcbPad *pad = dc_array_get(fp->pads, pi);
                double px, py;
                dc_pcb_canvas_world_to_screen(c, fp->x + pad->x, fp->y + pad->y, &px, &py);
                double pw = pad->size_x * c->zoom / 2.0;
                double ph = pad->size_y * c->zoom / 2.0;
                if (pw < 1.5) pw = 1.5;
                if (ph < 1.5) ph = 1.5;

                cairo_set_source_rgba(cr, lc.r, lc.g, lc.b, lc.a);
                cairo_rectangle(cr, px - pw, py - ph, pw * 2, ph * 2);
                cairo_fill(cr);
            }
        }

        /* Reference label */
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_BOLD);
        double font_size = 10.0;
        if (c->zoom > 5.0) font_size = 10.0 * (c->zoom / 5.0);
        if (font_size > 18.0) font_size = 18.0;
        cairo_set_font_size(cr, font_size);
        if (fp->reference) {
            cairo_move_to(cr, sx - bw / 2, sy - bh / 2 - 3);
            cairo_show_text(cr, fp->reference);
        }
    }

    /* Draw ratsnest */
    if (c->ratsnest) {
        cairo_set_source_rgba(cr, 0.3, 0.8, 0.3, 0.6);
        cairo_set_line_width(cr, 1.0);
        double dash[] = { 4.0, 4.0 };
        cairo_set_dash(cr, dash, 2, 0);

        for (size_t i = 0; i < dc_ratsnest_line_count(c->ratsnest); i++) {
            const DC_RatsnestLine *rl = dc_ratsnest_get_line(c->ratsnest, i);
            double sx1, sy1, sx2, sy2;
            dc_pcb_canvas_world_to_screen(c, rl->x1, rl->y1, &sx1, &sy1);
            dc_pcb_canvas_world_to_screen(c, rl->x2, rl->y2, &sx2, &sy2);
            cairo_move_to(cr, sx1, sy1);
            cairo_line_to(cr, sx2, sy2);
        }
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0);
    }
}

/* =========================================================================
 * Draw callback
 * ========================================================================= */
static void on_draw(GtkDrawingArea *area, cairo_t *cr,
                     int width, int height, gpointer userdata)
{
    (void)area;
    DC_PcbCanvas *c = userdata;

    /* Dark background */
    cairo_set_source_rgb(cr, 0.08, 0.08, 0.10);
    cairo_paint(cr);

    draw_grid(c, cr, width, height);
    draw_pcb(c, cr);
}

/* =========================================================================
 * Event handlers
 * ========================================================================= */
static void on_scroll(GtkEventControllerScroll *ctrl, double dx, double dy,
                       gpointer userdata)
{
    (void)ctrl; (void)dx;
    DC_PcbCanvas *c = userdata;
    double factor = (dy < 0) ? 1.15 : 1.0 / 1.15;
    double new_zoom = c->zoom * factor;
    if (new_zoom < PCB_ZOOM_MIN) new_zoom = PCB_ZOOM_MIN;
    if (new_zoom > PCB_ZOOM_MAX) new_zoom = PCB_ZOOM_MAX;
    c->zoom = new_zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

static void on_pressed(GtkGestureClick *gesture, int n_press,
                        double x, double y, gpointer userdata)
{
    (void)gesture; (void)n_press;
    DC_PcbCanvas *c = userdata;
    c->dragging = 1;
    c->drag_start_x = x;
    c->drag_start_y = y;
    c->pan_start_x = c->pan_x;
    c->pan_start_y = c->pan_y;
}

static void on_released(GtkGestureClick *gesture, int n_press,
                         double x, double y, gpointer userdata)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    DC_PcbCanvas *c = userdata;
    c->dragging = 0;
}

static void on_motion(GtkEventControllerMotion *ctrl, double x, double y,
                       gpointer userdata)
{
    (void)ctrl;
    DC_PcbCanvas *c = userdata;
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
DC_PcbCanvas *dc_pcb_canvas_new(void)
{
    DC_PcbCanvas *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->zoom = PCB_ZOOM_DEFAULT;
    c->selected_fp = -1;
    c->active_layer = DC_PCB_LAYER_F_CU;

    /* All layers visible by default */
    memset(c->layer_visible, 1, sizeof(c->layer_visible));

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

    /* Click to pan */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), c);
    g_signal_connect(click, "released", G_CALLBACK(on_released), c);
    gtk_widget_add_controller(c->drawing_area, GTK_EVENT_CONTROLLER(click));

    /* Motion for drag */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), c);
    gtk_widget_add_controller(c->drawing_area, motion);

    gtk_widget_set_focusable(c->drawing_area, TRUE);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA, "PCB canvas created");
    return c;
}

void dc_pcb_canvas_free(DC_PcbCanvas *c)
{
    if (!c) return;
    free(c);
}

GtkWidget *dc_pcb_canvas_widget(DC_PcbCanvas *c)
{
    return c ? c->drawing_area : NULL;
}

void dc_pcb_canvas_set_pcb(DC_PcbCanvas *c, DC_EPcb *pcb)
{
    if (!c) return;
    c->pcb = pcb;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_pcb_canvas_set_library(DC_PcbCanvas *c, DC_ELibrary *lib)
{
    if (c) c->lib = lib;
}

void dc_pcb_canvas_set_ratsnest(DC_PcbCanvas *c, DC_Ratsnest *rn)
{
    if (!c) return;
    c->ratsnest = rn;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_pcb_canvas_set_zoom(DC_PcbCanvas *c, double zoom)
{
    if (!c) return;
    if (zoom < PCB_ZOOM_MIN) zoom = PCB_ZOOM_MIN;
    if (zoom > PCB_ZOOM_MAX) zoom = PCB_ZOOM_MAX;
    c->zoom = zoom;
    gtk_widget_queue_draw(c->drawing_area);
}

double dc_pcb_canvas_get_zoom(const DC_PcbCanvas *c) { return c ? c->zoom : 1.0; }

void dc_pcb_canvas_set_pan(DC_PcbCanvas *c, double x, double y)
{
    if (!c) return;
    c->pan_x = x; c->pan_y = y;
    gtk_widget_queue_draw(c->drawing_area);
}

void dc_pcb_canvas_get_pan(const DC_PcbCanvas *c, double *x, double *y)
{
    if (!c) return;
    if (x) *x = c->pan_x;
    if (y) *y = c->pan_y;
}

void dc_pcb_canvas_queue_redraw(DC_PcbCanvas *c)
{
    if (c && c->drawing_area) gtk_widget_queue_draw(c->drawing_area);
}

void dc_pcb_canvas_set_layer_visible(DC_PcbCanvas *c, int layer_id, int visible)
{
    if (!c || layer_id < 0 || layer_id >= DC_PCB_LAYER_COUNT) return;
    c->layer_visible[layer_id] = (unsigned char)(visible ? 1 : 0);
    gtk_widget_queue_draw(c->drawing_area);
}

int dc_pcb_canvas_get_layer_visible(const DC_PcbCanvas *c, int layer_id)
{
    if (!c || layer_id < 0 || layer_id >= DC_PCB_LAYER_COUNT) return 0;
    return c->layer_visible[layer_id];
}

void dc_pcb_canvas_set_active_layer(DC_PcbCanvas *c, int layer_id)
{
    if (c) c->active_layer = layer_id;
}

int dc_pcb_canvas_get_active_layer(const DC_PcbCanvas *c)
{
    return c ? c->active_layer : DC_PCB_LAYER_F_CU;
}

int dc_pcb_canvas_get_selected_footprint(const DC_PcbCanvas *c) { return c ? c->selected_fp : -1; }
void dc_pcb_canvas_set_selected_footprint(DC_PcbCanvas *c, int i) { if (c) { c->selected_fp = i; gtk_widget_queue_draw(c->drawing_area); } }

int dc_pcb_canvas_render_to_png(DC_PcbCanvas *c, const char *path, int width, int height)
{
    if (!c || !path) return -1;
    if (width <= 0) width = gtk_widget_get_width(c->drawing_area);
    if (height <= 0) height = gtk_widget_get_height(c->drawing_area);
    if (width <= 0 || height <= 0) return -1;

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);
    on_draw(GTK_DRAWING_AREA(c->drawing_area), cr, width, height, c);
    cairo_status_t status = cairo_surface_write_to_png(surface, path);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return (status == CAIRO_STATUS_SUCCESS) ? 0 : -1;
}
