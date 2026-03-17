#ifndef DC_SCH_EDITOR_H
#define DC_SCH_EDITOR_H

/*
 * sch_editor.h — Interactive schematic editor for DunCAD EDA.
 *
 * Wraps DC_SchCanvas + DC_ESchematic with editing modes:
 * SELECT, WIRE, PLACE_SYMBOL, PLACE_LABEL, MOVE.
 * Undo/redo via command array.
 *
 * Ownership:
 *   - dc_sch_editor_new() returns an owned DC_SchEditor*.
 *   - dc_sch_editor_widget() returns a borrowed GtkWidget*.
 *   - dc_sch_editor_free() releases all resources.
 */

#include <gtk/gtk.h>

typedef struct DC_SchEditor DC_SchEditor;

/* Editor modes */
typedef enum {
    DC_SCH_MODE_SELECT = 0,
    DC_SCH_MODE_WIRE,
    DC_SCH_MODE_PLACE_SYMBOL,
    DC_SCH_MODE_PLACE_LABEL,
    DC_SCH_MODE_MOVE,
} DC_SchEditMode;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_SchEditor *dc_sch_editor_new(void);
void dc_sch_editor_free(DC_SchEditor *ed);

/* =========================================================================
 * Widget access
 * ========================================================================= */

/* Returns a GtkBox containing the toolbar + canvas. */
GtkWidget *dc_sch_editor_widget(DC_SchEditor *ed);

/* =========================================================================
 * Data access
 * ========================================================================= */

struct DC_ESchematic;
struct DC_ELibrary;
struct DC_SchCanvas;

/* Get the underlying schematic (owned by editor). */
struct DC_ESchematic *dc_sch_editor_get_schematic(DC_SchEditor *ed);

/* Get the underlying canvas (borrowed). */
struct DC_SchCanvas *dc_sch_editor_get_canvas(DC_SchEditor *ed);

/* Set library for symbol rendering + resolution. Borrowed. */
void dc_sch_editor_set_library(DC_SchEditor *ed, struct DC_ELibrary *lib);

/* =========================================================================
 * File I/O
 * ========================================================================= */

int dc_sch_editor_load(DC_SchEditor *ed, const char *path);
int dc_sch_editor_save(DC_SchEditor *ed, const char *path);

/* =========================================================================
 * Mode control
 * ========================================================================= */

void dc_sch_editor_set_mode(DC_SchEditor *ed, DC_SchEditMode mode);
DC_SchEditMode dc_sch_editor_get_mode(const DC_SchEditor *ed);

#endif /* DC_SCH_EDITOR_H */
