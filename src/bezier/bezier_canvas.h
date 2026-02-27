#ifndef DC_BEZIER_CANVAS_H
#define DC_BEZIER_CANVAS_H

/*
 * bezier_canvas.h — Cairo drawing area for the DunCAD bezier editor.
 *
 * Phase 2.1: grid rendering, zoom, pan, coordinate transforms, and
 * live cursor-coordinate display.  No curve data yet (that's 2.2).
 *
 * Ownership:
 *   - dc_bezier_canvas_new() returns an owned DC_BezierCanvas*.
 *   - The caller may attach it to a window via dc_bezier_canvas_set_window(),
 *     which registers a destroy-notify so the canvas is freed when the
 *     window is destroyed.  Otherwise the caller must call
 *     dc_bezier_canvas_free() manually.
 *   - dc_bezier_canvas_widget() returns a borrowed GtkWidget* whose
 *     lifetime is managed by GTK.
 */

#include <gtk/gtk.h>

typedef struct DC_BezierCanvas DC_BezierCanvas;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/* Create a new canvas.  Returns NULL on allocation failure. */
DC_BezierCanvas *dc_bezier_canvas_new(void);

/* Free the canvas.  Safe to call with NULL. */
void dc_bezier_canvas_free(DC_BezierCanvas *canvas);

/* -------------------------------------------------------------------------
 * Widget access
 * ---------------------------------------------------------------------- */

/* Return the GtkWidget* (GtkDrawingArea) managed by this canvas.
 * The returned pointer is borrowed — do not unref or free it. */
GtkWidget *dc_bezier_canvas_widget(DC_BezierCanvas *canvas);

/* -------------------------------------------------------------------------
 * Window integration
 * ---------------------------------------------------------------------- */

/* Attach the canvas to a window.  Registers a destroy-notify so the
 * canvas is freed when the window is destroyed.  Also used to push
 * status-bar updates.  Pass NULL to detach. */
void dc_bezier_canvas_set_window(DC_BezierCanvas *canvas, GtkWidget *window);

/* -------------------------------------------------------------------------
 * View control
 * ---------------------------------------------------------------------- */

/* Set the zoom level (pixels per mm).  Clamped to [0.1, 100.0]. */
void dc_bezier_canvas_set_zoom(DC_BezierCanvas *canvas, double zoom);

/* -------------------------------------------------------------------------
 * Coordinate transforms
 * ---------------------------------------------------------------------- */

/* Convert screen (widget) coordinates to world (mm, Y-up) coordinates. */
void dc_bezier_canvas_screen_to_world(DC_BezierCanvas *canvas,
                                      double sx, double sy,
                                      double *wx, double *wy);

/* Convert world coordinates to screen coordinates. */
void dc_bezier_canvas_world_to_screen(DC_BezierCanvas *canvas,
                                      double wx, double wy,
                                      double *sx, double *sy);

/* -------------------------------------------------------------------------
 * Curve binding
 * ---------------------------------------------------------------------- */

struct DC_BezierCurve;

/* Set a borrowed curve pointer for the canvas to render.
 * The canvas does NOT own the curve. Pass NULL to clear. */
void dc_bezier_canvas_set_curve(DC_BezierCanvas *canvas,
                                struct DC_BezierCurve *curve);

/* -------------------------------------------------------------------------
 * Status-bar window (no destroy-notify variant)
 * ---------------------------------------------------------------------- */

/* Like set_window() but does NOT register destroy-notify.
 * Used by the editor which manages its own lifecycle. */
void dc_bezier_canvas_set_status_window(DC_BezierCanvas *canvas,
                                        GtkWidget *window);

/* -------------------------------------------------------------------------
 * Overlay callback
 * ---------------------------------------------------------------------- */

typedef void (*DC_CanvasOverlayCb)(DC_BezierCanvas *canvas, cairo_t *cr,
                                   int width, int height, void *userdata);

/* Register a callback invoked after the grid+curve are drawn but in
 * screen (pixel) coordinates.  Used by the editor to draw knot/handle
 * dots at fixed pixel sizes. */
void dc_bezier_canvas_set_overlay_cb(DC_BezierCanvas *canvas,
                                     DC_CanvasOverlayCb cb, void *userdata);

/* -------------------------------------------------------------------------
 * Zoom read-back
 * ---------------------------------------------------------------------- */

/* Return the current zoom level (pixels per mm). */
double dc_bezier_canvas_get_zoom(const DC_BezierCanvas *canvas);

/* Return 1 if space bar is currently held (pan mode), 0 otherwise. */
int dc_bezier_canvas_space_held(const DC_BezierCanvas *canvas);

#endif /* DC_BEZIER_CANVAS_H */
