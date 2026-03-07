#ifndef DC_CODE_EDITOR_H
#define DC_CODE_EDITOR_H

/*
 * code_editor.h — GtkSourceView-based OpenSCAD code editor panel.
 *
 * Provides a source code editor with OpenSCAD syntax highlighting,
 * line numbers, undo/redo, file open/save, and dark theme.
 *
 * Ownership:
 *   - DC_CodeEditor is opaque; created with dc_code_editor_new(),
 *     freed with dc_code_editor_free().
 *   - dc_code_editor_widget() returns a borrowed GtkWidget*.
 */

#include <gtk/gtk.h>

typedef struct DC_CodeEditor DC_CodeEditor;

/* Create a new code editor. Returns NULL on failure. */
DC_CodeEditor *dc_code_editor_new(void);

/* Free the editor. Safe with NULL. */
void dc_code_editor_free(DC_CodeEditor *ed);

/* Get the top-level widget (GtkBox containing toolbar + source view). */
GtkWidget *dc_code_editor_widget(DC_CodeEditor *ed);

/* Get/set the full text content. Returned string is owned by caller (free it). */
char *dc_code_editor_get_text(DC_CodeEditor *ed);
void  dc_code_editor_set_text(DC_CodeEditor *ed, const char *text);

/* File operations. Path is copied internally.
 * Returns 0 on success, -1 on failure. */
int  dc_code_editor_open_file(DC_CodeEditor *ed, const char *path);
int  dc_code_editor_save(DC_CodeEditor *ed);
int  dc_code_editor_save_as(DC_CodeEditor *ed, const char *path);

/* Get the current file path (borrowed, may be NULL if untitled). */
const char *dc_code_editor_get_path(const DC_CodeEditor *ed);

/* Set the window reference for file dialogs. */
void dc_code_editor_set_window(DC_CodeEditor *ed, GtkWidget *window);

/* Select (highlight) a range of lines (1-based, inclusive).
 * Scrolls the editor to show the selection. */
void dc_code_editor_select_lines(DC_CodeEditor *ed, int line_start, int line_end);

#endif /* DC_CODE_EDITOR_H */
