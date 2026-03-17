#ifndef DC_EDA_VIEW_H
#define DC_EDA_VIEW_H

/*
 * eda_view.h — EDA tab container for DunCAD.
 *
 * Horizontal GtkPaned: schematic editor (left) + Cubeiform code editor
 * (right, GtkSourceView). Bidirectional sync: GUI edits update Cubeiform
 * source, Cubeiform execution updates GUI.
 *
 * Ownership:
 *   - dc_eda_view_new() returns an owned DC_EdaView*.
 *   - dc_eda_view_widget() returns a borrowed GtkWidget*.
 *   - dc_eda_view_free() releases all resources.
 */

#include <gtk/gtk.h>

typedef struct DC_EdaView DC_EdaView;

/* Forward declarations */
struct DC_SchEditor;
struct DC_PcbEditor;
struct DC_CodeEditor;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_EdaView *dc_eda_view_new(void);
void dc_eda_view_free(DC_EdaView *view);

/* =========================================================================
 * Widget access
 * ========================================================================= */

GtkWidget *dc_eda_view_widget(DC_EdaView *view);

/* =========================================================================
 * Component access
 * ========================================================================= */

struct DC_SchEditor *dc_eda_view_get_sch_editor(DC_EdaView *view);
struct DC_PcbEditor *dc_eda_view_get_pcb_editor(DC_EdaView *view);
struct DC_CodeEditor *dc_eda_view_get_code_editor(DC_EdaView *view);

/* =========================================================================
 * Sync
 * ========================================================================= */

/* Execute the Cubeiform source in the code editor against the schematic. */
int dc_eda_view_execute_cubeiform(DC_EdaView *view);

/* Export current schematic state to Cubeiform in the code editor. */
int dc_eda_view_export_to_cubeiform(DC_EdaView *view);

#endif /* DC_EDA_VIEW_H */
