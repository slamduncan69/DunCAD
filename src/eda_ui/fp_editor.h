#ifndef DC_FP_EDITOR_H
#define DC_FP_EDITOR_H

/*
 * fp_editor.h — KiCad footprint editor dialog.
 *
 * Two-pane layout: pad/properties panel (left) + preview canvas (right).
 * Edits a cloned AST; saves back to the original .kicad_mod file.
 *
 * Usage:
 *   int changed = dc_fp_editor_run(parent, fp_def, fp_path);
 *   // changed: 1 if saved, 0 if cancelled
 */

#include <gtk/gtk.h>
#include "eda/sexpr.h"

/* Run the footprint editor dialog.
 * parent:  parent window
 * fp_def:  borrowed footprint definition (will be cloned for editing)
 * fp_path: path to the .kicad_mod file (for save-back)
 * Returns: 1 if saved, 0 if cancelled. */
int dc_fp_editor_run(GtkWindow *parent, const DC_Sexpr *fp_def,
                      const char *fp_path);

#endif /* DC_FP_EDITOR_H */
