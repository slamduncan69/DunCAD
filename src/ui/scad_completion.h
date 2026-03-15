#ifndef DC_SCAD_COMPLETION_H
#define DC_SCAD_COMPLETION_H

#include <gtksourceview/gtksource.h>

typedef struct DC_ScadCompletion DC_ScadCompletion;

typedef enum {
    DC_LANG_OPENSCAD = 0,
    DC_LANG_CUBEIFORM,
} DC_LangMode;

DC_ScadCompletion *dc_scad_completion_new(GtkSourceView *view,
                                          GtkSourceBuffer *buffer);
void dc_scad_completion_free(DC_ScadCompletion *comp);

void dc_scad_completion_set_lang_mode(DC_ScadCompletion *comp, DC_LangMode mode);
DC_LangMode dc_scad_completion_get_lang_mode(DC_ScadCompletion *comp);

/* Returns the syntax hint label widget — caller adds it to their layout */
GtkWidget *dc_scad_completion_syntax_label(DC_ScadCompletion *comp);

#endif /* DC_SCAD_COMPLETION_H */
