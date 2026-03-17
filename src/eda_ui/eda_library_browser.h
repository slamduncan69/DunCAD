#ifndef DC_EDA_LIBRARY_BROWSER_H
#define DC_EDA_LIBRARY_BROWSER_H

/*
 * eda_library_browser.h — Library browser dialog for KiCad symbols/footprints.
 *
 * Modal dialog with GtkSearchEntry + GtkListView. Loads symbols from
 * /usr/share/kicad/symbols/ (or custom paths). Returns selected lib_id.
 *
 * Usage:
 *   char *lib_id = dc_eda_library_browser_run(parent, lib, "symbol");
 *   // lib_id is "Device:R_Small" etc., or NULL if cancelled
 *   free(lib_id);
 */

#include <gtk/gtk.h>

struct DC_ELibrary;

/* Run the library browser dialog.
 * parent:   parent window (for modal positioning)
 * lib:      library to browse (borrowed, must have symbols loaded)
 * kind:     "symbol" or "footprint"
 * Returns:  owned string (lib_id) or NULL if cancelled. Caller must free(). */
char *dc_eda_library_browser_run(GtkWindow *parent,
                                   struct DC_ELibrary *lib,
                                   const char *kind);

#endif /* DC_EDA_LIBRARY_BROWSER_H */
