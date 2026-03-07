#ifndef DC_SCAD_COMPLETION_H
#define DC_SCAD_COMPLETION_H

#include <gtksourceview/gtksource.h>

typedef struct DC_ScadCompletion DC_ScadCompletion;

DC_ScadCompletion *dc_scad_completion_new(GtkSourceView *view,
                                          GtkSourceBuffer *buffer);
void dc_scad_completion_free(DC_ScadCompletion *comp);

/* Returns the syntax hint label widget — caller adds it to their layout */
GtkWidget *dc_scad_completion_syntax_label(DC_ScadCompletion *comp);

#endif /* DC_SCAD_COMPLETION_H */
