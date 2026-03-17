#ifndef DC_SYM_EDITOR_H
#define DC_SYM_EDITOR_H

/*
 * sym_editor.h — KiCad symbol editor dialog.
 *
 * Two-pane layout: properties panel (left) + interactive canvas (right).
 * Edits a cloned AST; saves back to the original library file.
 *
 * Usage:
 *   int changed = dc_sym_editor_run(parent, sym_def, lib_path, lib);
 *   // changed: 1 if saved, 0 if cancelled
 */

#include <gtk/gtk.h>
#include "eda/sexpr.h"

struct DC_ELibrary;

/* Run the symbol editor dialog.
 * parent:   parent window
 * sym_def:  borrowed symbol definition (will be cloned for editing)
 * lib_path: path to the .kicad_sym file (for save-back)
 * lib:      library (for re-indexing after save)
 * Returns: 1 if saved, 0 if cancelled. */
int dc_sym_editor_run(GtkWindow *parent, const DC_Sexpr *sym_def,
                       const char *lib_path, struct DC_ELibrary *lib);

#endif /* DC_SYM_EDITOR_H */
