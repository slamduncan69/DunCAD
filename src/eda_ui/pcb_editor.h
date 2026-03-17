#ifndef DC_PCB_EDITOR_H
#define DC_PCB_EDITOR_H

/*
 * pcb_editor.h — Interactive PCB editor for DunCAD EDA.
 *
 * Wraps DC_PcbCanvas + DC_EPcb with editing modes:
 * SELECT, ROUTE, PLACE_FOOTPRINT, PLACE_VIA, ZONE, MEASURE.
 *
 * Ownership: dc_pcb_editor_new() returns an owned DC_PcbEditor*.
 */

#include <gtk/gtk.h>

typedef struct DC_PcbEditor DC_PcbEditor;

typedef enum {
    DC_PCB_MODE_SELECT = 0,
    DC_PCB_MODE_ROUTE,
    DC_PCB_MODE_PLACE_FOOTPRINT,
    DC_PCB_MODE_PLACE_VIA,
    DC_PCB_MODE_ZONE,
    DC_PCB_MODE_MEASURE,
} DC_PcbEditMode;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_PcbEditor *dc_pcb_editor_new(void);
void dc_pcb_editor_free(DC_PcbEditor *ed);

/* =========================================================================
 * Widget access
 * ========================================================================= */

GtkWidget *dc_pcb_editor_widget(DC_PcbEditor *ed);

/* =========================================================================
 * Data access
 * ========================================================================= */

struct DC_EPcb;
struct DC_ELibrary;
struct DC_PcbCanvas;

struct DC_EPcb *dc_pcb_editor_get_pcb(DC_PcbEditor *ed);
struct DC_PcbCanvas *dc_pcb_editor_get_canvas(DC_PcbEditor *ed);
void dc_pcb_editor_set_library(DC_PcbEditor *ed, struct DC_ELibrary *lib);

/* =========================================================================
 * File I/O
 * ========================================================================= */

int dc_pcb_editor_load(DC_PcbEditor *ed, const char *path);
int dc_pcb_editor_save(DC_PcbEditor *ed, const char *path);

/* =========================================================================
 * Mode / Ratsnest
 * ========================================================================= */

void dc_pcb_editor_set_mode(DC_PcbEditor *ed, DC_PcbEditMode mode);
DC_PcbEditMode dc_pcb_editor_get_mode(const DC_PcbEditor *ed);

/* Recompute ratsnest. Call after modifying tracks/nets. */
void dc_pcb_editor_update_ratsnest(DC_PcbEditor *ed);

#endif /* DC_PCB_EDITOR_H */
