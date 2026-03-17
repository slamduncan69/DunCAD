#ifndef DC_EDA_CUBEIFORM_EXPORT_H
#define DC_EDA_CUBEIFORM_EXPORT_H

/*
 * eda_cubeiform_export.h — Export EDA data models to Cubeiform source.
 *
 * Generates readable .dcad source from schematic and PCB state, making
 * Cubeiform the bidirectional source of truth: GUI edits → export → .dcad
 * file → version control → agent reads/modifies → re-execute.
 *
 * Ownership: Returned strings are malloc'd. Caller must free().
 */

#include "core/error.h"
#include "eda/eda_schematic.h"
#include "eda/eda_pcb.h"

/* Generate Cubeiform source from current schematic state.
 * Returns a malloc'd string, or NULL on error. Caller must free(). */
char *dc_eschematic_to_cubeiform(const DC_ESchematic *sch, DC_Error *err);

/* Generate Cubeiform source from current PCB state.
 * Returns a malloc'd string, or NULL on error. Caller must free(). */
char *dc_epcb_to_cubeiform(const DC_EPcb *pcb, DC_Error *err);

#endif /* DC_EDA_CUBEIFORM_EXPORT_H */
