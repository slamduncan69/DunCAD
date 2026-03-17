#ifndef DC_EDA_FOOTPRINT_BROWSER_H
#define DC_EDA_FOOTPRINT_BROWSER_H

/*
 * eda_footprint_browser.h — Footprint library browser dialog.
 *
 * Three-pane layout: library list | footprint list | preview + info.
 * Returns selected "lib:footprint" string.
 *
 * Usage:
 *   char *fp_id = dc_eda_footprint_browser_run(parent, lib);
 *   // fp_id is "Resistor_SMD:R_0402_1005Metric" etc., or NULL if cancelled
 *   free(fp_id);
 */

#include <gtk/gtk.h>

struct DC_ELibrary;

/* Run the footprint browser dialog.
 * parent: parent window (for modal positioning)
 * lib:    library to browse (borrowed, must have footprints loaded)
 * Returns: owned string (lib:footprint) or NULL if cancelled. Caller must free(). */
char *dc_eda_footprint_browser_run(GtkWindow *parent, struct DC_ELibrary *lib);

#endif /* DC_EDA_FOOTPRINT_BROWSER_H */
