#ifndef DC_PCB_CANVAS_H
#define DC_PCB_CANVAS_H

/*
 * pcb_canvas.h — Multi-layer Cairo canvas for DunCAD PCB editor.
 *
 * Interactive canvas with mode-aware gesture dispatch: select, move,
 * track routing, via/footprint placement, zone drawing.
 * Per-layer color/visibility/opacity. mm grid (0.1mm fine, 1mm coarse).
 *
 * Ownership: dc_pcb_canvas_new() returns an owned DC_PcbCanvas*.
 */

#include <gtk/gtk.h>

typedef struct DC_PcbCanvas DC_PcbCanvas;
struct DC_EPcb;
struct DC_ELibrary;
struct DC_Ratsnest;
struct DC_PcbEditor;

/* Selection type — which kind of element is selected */
typedef enum {
    DC_PCB_SEL_NONE = 0,
    DC_PCB_SEL_FOOTPRINT,
    DC_PCB_SEL_TRACK,
    DC_PCB_SEL_VIA,
    DC_PCB_SEL_ZONE,
} DC_PcbSelType;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_PcbCanvas *dc_pcb_canvas_new(void);
void dc_pcb_canvas_free(DC_PcbCanvas *canvas);

/* =========================================================================
 * Widget access
 * ========================================================================= */

GtkWidget *dc_pcb_canvas_widget(DC_PcbCanvas *canvas);

/* =========================================================================
 * Data binding
 * ========================================================================= */

void dc_pcb_canvas_set_pcb(DC_PcbCanvas *canvas, struct DC_EPcb *pcb);
void dc_pcb_canvas_set_library(DC_PcbCanvas *canvas, struct DC_ELibrary *lib);
void dc_pcb_canvas_set_ratsnest(DC_PcbCanvas *canvas, struct DC_Ratsnest *rn);
void dc_pcb_canvas_set_editor(DC_PcbCanvas *canvas, struct DC_PcbEditor *editor);

/* =========================================================================
 * View control
 * ========================================================================= */

void dc_pcb_canvas_set_zoom(DC_PcbCanvas *canvas, double zoom);
double dc_pcb_canvas_get_zoom(const DC_PcbCanvas *canvas);
void dc_pcb_canvas_set_pan(DC_PcbCanvas *canvas, double x, double y);
void dc_pcb_canvas_get_pan(const DC_PcbCanvas *canvas, double *x, double *y);
void dc_pcb_canvas_queue_redraw(DC_PcbCanvas *canvas);

/* =========================================================================
 * Layer visibility
 * ========================================================================= */

void dc_pcb_canvas_set_layer_visible(DC_PcbCanvas *canvas, int layer_id, int visible);
int dc_pcb_canvas_get_layer_visible(const DC_PcbCanvas *canvas, int layer_id);
void dc_pcb_canvas_set_active_layer(DC_PcbCanvas *canvas, int layer_id);
int dc_pcb_canvas_get_active_layer(const DC_PcbCanvas *canvas);

/* =========================================================================
 * Coordinate transforms
 * ========================================================================= */

void dc_pcb_canvas_screen_to_world(DC_PcbCanvas *canvas,
                                    double sx, double sy,
                                    double *wx, double *wy);
void dc_pcb_canvas_world_to_screen(DC_PcbCanvas *canvas,
                                    double wx, double wy,
                                    double *sx, double *sy);

/* =========================================================================
 * Selection
 * ========================================================================= */

DC_PcbSelType dc_pcb_canvas_get_sel_type(const DC_PcbCanvas *canvas);
int dc_pcb_canvas_get_sel_index(const DC_PcbCanvas *canvas);
void dc_pcb_canvas_select(DC_PcbCanvas *canvas, DC_PcbSelType type, int index);
void dc_pcb_canvas_deselect(DC_PcbCanvas *canvas);

/* Legacy compat */
int dc_pcb_canvas_get_selected_footprint(const DC_PcbCanvas *canvas);
void dc_pcb_canvas_set_selected_footprint(DC_PcbCanvas *canvas, int index);

int dc_pcb_canvas_render_to_png(DC_PcbCanvas *canvas, const char *path,
                                 int width, int height);

#endif /* DC_PCB_CANVAS_H */
