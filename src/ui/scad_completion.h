#ifndef DC_SCAD_COMPLETION_H
#define DC_SCAD_COMPLETION_H

#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

#define DC_TYPE_SCAD_COMPLETION (dc_scad_completion_get_type())

G_DECLARE_FINAL_TYPE(DcScadCompletion, dc_scad_completion,
                     DC, SCAD_COMPLETION, GObject)

DcScadCompletion *dc_scad_completion_new(void);

G_END_DECLS

#endif /* DC_SCAD_COMPLETION_H */
