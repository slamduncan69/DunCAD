#ifndef DC_CUBEIFORM_EDA_H
#define DC_CUBEIFORM_EDA_H

/*
 * cubeiform_eda.h — EDA intermediate representation for Cubeiform.
 *
 * Parses schematic/pcb/assembly domain blocks from Cubeiform source into
 * typed operation lists. Apply functions execute operations against the
 * live EDA data models.
 *
 * Usage:
 *   1. dc_cubeiform_parse_eda() — parse Cubeiform source, extract EDA ops
 *   2. dc_cubeiform_eda_apply_schematic() — apply schematic ops to DC_ESchematic
 *   3. dc_cubeiform_eda_apply_pcb() — apply PCB ops to DC_EPcb
 *
 * Or use dc_cubeiform_execute() for a one-shot parse + apply all domains.
 *
 * Ownership: DC_CubeiformEda is heap-allocated and owns all internal data.
 * Free with dc_cubeiform_eda_free().
 */

#include "core/error.h"
#include "eda/eda_schematic.h"
#include "eda/eda_pcb.h"
#include "eda/eda_library.h"

/* -------------------------------------------------------------------------
 * EDA operation types
 * ---------------------------------------------------------------------- */

/* Schematic operations */
typedef enum {
    DC_SCH_OP_ADD_COMPONENT,
    DC_SCH_OP_ADD_WIRE,
    DC_SCH_OP_ADD_POWER,
    DC_SCH_OP_SET_VALUE,
    DC_SCH_OP_SET_FOOTPRINT,
} DC_SchOpType;

typedef struct {
    DC_SchOpType type;
    char  *ref;          /* component reference, e.g. "R1" — owned */
    char  *lib_id;       /* library id — owned, may be NULL */
    char  *name;         /* net/power name — owned, may be NULL */
    char  *str_value;    /* value/footprint string — owned, may be NULL */
    double x, y;         /* position */
    double x2, y2;       /* second point (for wire) */
} DC_SchOp;

/* PCB operations */
typedef enum {
    DC_PCB_OP_SET_OUTLINE_RECT,
    DC_PCB_OP_SET_RULE,
    DC_PCB_OP_PLACE,
    DC_PCB_OP_ROUTE_SEGMENT,
    DC_PCB_OP_ADD_ZONE,
} DC_PcbOpType;

typedef struct {
    DC_PcbOpType type;
    char  *ref;          /* component ref — owned, may be NULL */
    char  *name;         /* net name — owned, may be NULL */
    char  *rule_key;     /* rule name — owned, may be NULL */
    double x, y;         /* position or size */
    double x2, y2;       /* second point */
    double width;        /* track width, zone clearance, etc. */
    double value;        /* rule value */
    int    layer;        /* layer id */
    double angle;        /* rotation */
} DC_PcbOp;

/* -------------------------------------------------------------------------
 * DC_CubeiformEda — parsed EDA operations from Cubeiform source
 * ---------------------------------------------------------------------- */
typedef struct DC_CubeiformEda DC_CubeiformEda;

/* =========================================================================
 * Parsing
 * ========================================================================= */

/* Parse Cubeiform source, extracting EDA domain blocks.
 * Returns NULL on error. Caller must free with dc_cubeiform_eda_free(). */
DC_CubeiformEda *dc_cubeiform_parse_eda(const char *dcad_src, DC_Error *err);

/* Free parsed EDA operations. NULL is a no-op. */
void dc_cubeiform_eda_free(DC_CubeiformEda *eda);

/* =========================================================================
 * Apply — execute parsed operations against live data models
 * ========================================================================= */

/* Apply schematic operations to a schematic.
 * Library may be NULL (symbol resolution skipped). */
int dc_cubeiform_eda_apply_schematic(DC_CubeiformEda *eda,
                                      DC_ESchematic *sch,
                                      DC_ELibrary *lib,
                                      DC_Error *err);

/* Apply PCB operations to a PCB.
 * Library may be NULL (footprint resolution skipped). */
int dc_cubeiform_eda_apply_pcb(DC_CubeiformEda *eda,
                                DC_EPcb *pcb,
                                DC_ELibrary *lib,
                                DC_Error *err);

/* =========================================================================
 * Unified execute — parse and apply all domains at once
 * ========================================================================= */

/* Parse Cubeiform source and apply all EDA operations.
 * Any model pointer may be NULL to skip that domain. */
int dc_cubeiform_execute(const char *dcad_src,
                          DC_ESchematic *sch,
                          DC_EPcb *pcb,
                          void *asm_ctx,     /* DC_Assembly* — future */
                          DC_ELibrary *lib,
                          DC_Error *err);

/* =========================================================================
 * Query — inspect parsed operations
 * ========================================================================= */

/* Get counts of parsed operations. */
size_t dc_cubeiform_eda_sch_op_count(const DC_CubeiformEda *eda);
size_t dc_cubeiform_eda_pcb_op_count(const DC_CubeiformEda *eda);

/* Get individual operations (borrowed pointers). */
const DC_SchOp *dc_cubeiform_eda_get_sch_op(const DC_CubeiformEda *eda, size_t i);
const DC_PcbOp *dc_cubeiform_eda_get_pcb_op(const DC_CubeiformEda *eda, size_t i);

#endif /* DC_CUBEIFORM_EDA_H */
