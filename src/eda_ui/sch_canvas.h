#ifndef DC_SCH_CANVAS_H
#define DC_SCH_CANVAS_H

/*
 * sch_canvas.h — Cairo 2D schematic canvas for DunCAD EDA.
 *
 * Mirrors the DC_BezierCanvas pattern: zoom/pan/grid/overlay, with
 * world↔screen coordinate transforms. Uses a 50-mil grid (KiCad convention).
 *
 * Ownership:
 *   - dc_sch_canvas_new() returns an owned DC_SchCanvas*.
 *   - dc_sch_canvas_widget() returns a borrowed GtkWidget*.
 *   - dc_sch_canvas_free() releases all resources.
 */

#include <gtk/gtk.h>

/* Forward declarations */
typedef struct DC_SchCanvas DC_SchCanvas;
struct DC_ESchematic;
struct DC_ELibrary;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_SchCanvas *dc_sch_canvas_new(void);
void dc_sch_canvas_free(DC_SchCanvas *canvas);

/* =========================================================================
 * Widget access
 * ========================================================================= */

GtkWidget *dc_sch_canvas_widget(DC_SchCanvas *canvas);

/* =========================================================================
 * Data binding
 * ========================================================================= */

/* Set the schematic to render. Borrowed pointer — canvas does not own it. */
void dc_sch_canvas_set_schematic(DC_SchCanvas *canvas, struct DC_ESchematic *sch);

/* Set the library for symbol rendering. Borrowed. */
void dc_sch_canvas_set_library(DC_SchCanvas *canvas, struct DC_ELibrary *lib);

/* =========================================================================
 * View control
 * ========================================================================= */

void dc_sch_canvas_set_zoom(DC_SchCanvas *canvas, double zoom);
double dc_sch_canvas_get_zoom(const DC_SchCanvas *canvas);
void dc_sch_canvas_set_pan(DC_SchCanvas *canvas, double x, double y);
void dc_sch_canvas_get_pan(const DC_SchCanvas *canvas, double *x, double *y);
void dc_sch_canvas_queue_redraw(DC_SchCanvas *canvas);

/* =========================================================================
 * Coordinate transforms
 * ========================================================================= */

void dc_sch_canvas_screen_to_world(DC_SchCanvas *canvas,
                                    double sx, double sy,
                                    double *wx, double *wy);
void dc_sch_canvas_world_to_screen(DC_SchCanvas *canvas,
                                    double wx, double wy,
                                    double *sx, double *sy);

/* =========================================================================
 * Offscreen render
 * ========================================================================= */

int dc_sch_canvas_render_to_png(DC_SchCanvas *canvas, const char *path,
                                 int width, int height);

/* =========================================================================
 * Selection
 * ========================================================================= */

/* Returns index of selected symbol, or -1 if none. */
int dc_sch_canvas_get_selected_symbol(const DC_SchCanvas *canvas);
void dc_sch_canvas_set_selected_symbol(DC_SchCanvas *canvas, int index);

#endif /* DC_SCH_CANVAS_H */
