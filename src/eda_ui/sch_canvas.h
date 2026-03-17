#ifndef DC_SCH_CANVAS_H
#define DC_SCH_CANVAS_H

/*
 * sch_canvas.h — Cairo 2D schematic canvas for DunCAD EDA.
 *
 * Interactive canvas with mode-aware gesture dispatch: select, move,
 * wire drawing, symbol/label/power placement. Hit testing on all
 * schematic element types.
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
struct DC_SchEditor;

/* Selection type — which kind of element is selected */
typedef enum {
    DC_SCH_SEL_NONE = 0,
    DC_SCH_SEL_SYMBOL,
    DC_SCH_SEL_WIRE,
    DC_SCH_SEL_JUNCTION,
    DC_SCH_SEL_LABEL,
    DC_SCH_SEL_POWER_PORT,
} DC_SchSelType;

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

void dc_sch_canvas_set_schematic(DC_SchCanvas *canvas, struct DC_ESchematic *sch);
void dc_sch_canvas_set_library(DC_SchCanvas *canvas, struct DC_ELibrary *lib);

/* Back-pointer to editor for mode queries and mutations */
void dc_sch_canvas_set_editor(DC_SchCanvas *canvas, struct DC_SchEditor *editor);

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

DC_SchSelType dc_sch_canvas_get_sel_type(const DC_SchCanvas *canvas);
int dc_sch_canvas_get_sel_index(const DC_SchCanvas *canvas);
void dc_sch_canvas_select(DC_SchCanvas *canvas, DC_SchSelType type, int index);
void dc_sch_canvas_deselect(DC_SchCanvas *canvas);

/* Legacy compat */
int dc_sch_canvas_get_selected_symbol(const DC_SchCanvas *canvas);
void dc_sch_canvas_set_selected_symbol(DC_SchCanvas *canvas, int index);

#endif /* DC_SCH_CANVAS_H */
