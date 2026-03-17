#ifndef DC_EDA_PCB_H
#define DC_EDA_PCB_H

/*
 * eda_pcb.h — PCB data model for DunCAD EDA.
 *
 * Represents a KiCad-compatible PCB: footprints, tracks, vias, zones,
 * nets, layers, and design rules. Supports:
 *   - Loading from KiCad .kicad_pcb s-expression files
 *   - Saving back to .kicad_pcb format
 *   - Programmatic manipulation
 *   - Netlist import
 *
 * Coordinates use mm, Y-down (KiCad convention).
 *
 * Ownership: DC_EPcb is opaque, heap-allocated. All strings within
 * are owned. dc_epcb_free() releases everything.
 */

#include "core/array.h"
#include "core/error.h"
#include "eda/eda_netlist.h"
#include "eda/sexpr.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * Layer identifiers — standard KiCad PCB layers
 * ---------------------------------------------------------------------- */
typedef enum {
    DC_PCB_LAYER_F_CU = 0,
    DC_PCB_LAYER_B_CU = 31,
    DC_PCB_LAYER_F_SILKS = 36,
    DC_PCB_LAYER_B_SILKS = 37,
    DC_PCB_LAYER_F_MASK = 38,
    DC_PCB_LAYER_B_MASK = 39,
    DC_PCB_LAYER_F_PASTE = 34,
    DC_PCB_LAYER_B_PASTE = 35,
    DC_PCB_LAYER_F_FAB = 40,
    DC_PCB_LAYER_B_FAB = 41,
    DC_PCB_LAYER_EDGE_CUTS = 44,
    DC_PCB_LAYER_DWGS_USER = 46,
    DC_PCB_LAYER_CMTS_USER = 48,
    DC_PCB_LAYER_COUNT = 64
} DC_PcbLayerId;

/* Convert layer name string (e.g. "F.Cu") to layer id. Returns -1 if unknown. */
int dc_pcb_layer_from_name(const char *name);

/* Convert layer id to name string. Returns "Unknown" if invalid. Borrowed pointer. */
const char *dc_pcb_layer_to_name(int layer_id);

/* -------------------------------------------------------------------------
 * Net — an electrical net (id + name)
 * ---------------------------------------------------------------------- */
typedef struct {
    int   id;           /* net number (0 = unconnected) */
    char *name;         /* net name — owned */
} DC_PcbNet;

/* -------------------------------------------------------------------------
 * Pad — component pad within a footprint
 * ---------------------------------------------------------------------- */
typedef enum {
    DC_PAD_SMD,
    DC_PAD_THRU_HOLE,
    DC_PAD_CONNECT,
    DC_PAD_NP_THRU_HOLE
} DC_PadType;

typedef enum {
    DC_PAD_SHAPE_CIRCLE,
    DC_PAD_SHAPE_RECT,
    DC_PAD_SHAPE_OVAL,
    DC_PAD_SHAPE_ROUNDRECT,
    DC_PAD_SHAPE_CUSTOM
} DC_PadShape;

typedef struct {
    char       *number;       /* pad number/name — owned */
    DC_PadType  type;
    DC_PadShape shape;
    double      x, y;         /* position relative to footprint */
    double      size_x, size_y;
    double      drill;        /* drill diameter (0 for SMD) */
    int         layer;        /* primary layer */
    int         net_id;       /* assigned net */
    char       *net_name;     /* assigned net name — owned, may be NULL */
} DC_PcbPad;

/* -------------------------------------------------------------------------
 * Footprint — component placed on the PCB
 * ---------------------------------------------------------------------- */
typedef struct {
    char     *lib_id;        /* library identifier — owned */
    char     *reference;     /* e.g. "R1" — owned */
    char     *value;         /* e.g. "10k" — owned, may be NULL */
    double    x, y;          /* position in mm */
    double    angle;         /* rotation in degrees */
    int       layer;         /* placement layer (F.Cu or B.Cu) */
    char     *uuid;          /* owned */
    DC_Array *pads;          /* DC_PcbPad elements */
} DC_PcbFootprint;

/* -------------------------------------------------------------------------
 * Track — copper trace segment
 * ---------------------------------------------------------------------- */
typedef struct {
    double x1, y1;
    double x2, y2;
    double width;
    int    layer;
    int    net_id;
    char  *uuid;             /* owned */
} DC_PcbTrack;

/* -------------------------------------------------------------------------
 * Via — layer-to-layer connection
 * ---------------------------------------------------------------------- */
typedef struct {
    double x, y;
    double size;             /* annular ring diameter */
    double drill;            /* drill diameter */
    int    net_id;
    int    layer_start;
    int    layer_end;
    char  *uuid;             /* owned */
} DC_PcbVia;

/* -------------------------------------------------------------------------
 * Zone — copper pour area
 * ---------------------------------------------------------------------- */
typedef struct {
    double x, y;             /* vertex position */
} DC_PcbZoneVertex;

typedef struct {
    int       net_id;
    char     *net_name;      /* owned */
    int       layer;
    double    clearance;
    DC_Array *outline;       /* DC_PcbZoneVertex elements */
    char     *uuid;          /* owned */
} DC_PcbZone;

/* -------------------------------------------------------------------------
 * Design rules
 * ---------------------------------------------------------------------- */
typedef struct {
    double clearance;        /* minimum copper clearance (mm) */
    double track_width;      /* default track width (mm) */
    double via_size;         /* default via outer diameter (mm) */
    double via_drill;        /* default via drill diameter (mm) */
    double min_track_width;  /* minimum track width (mm) */
    double edge_clearance;   /* minimum distance to board edge (mm) */
} DC_PcbDesignRules;

/* -------------------------------------------------------------------------
 * DC_EPcb — opaque PCB container
 * ---------------------------------------------------------------------- */
typedef struct DC_EPcb DC_EPcb;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_EPcb *dc_epcb_new(void);
void dc_epcb_free(DC_EPcb *pcb);

/* =========================================================================
 * I/O — KiCad format
 * ========================================================================= */

DC_EPcb *dc_epcb_load(const char *path, DC_Error *err);
DC_EPcb *dc_epcb_from_sexpr(DC_Sexpr *ast, DC_Error *err);
int dc_epcb_save(const DC_EPcb *pcb, const char *path, DC_Error *err);
char *dc_epcb_to_sexpr_string(const DC_EPcb *pcb, DC_Error *err);

/* =========================================================================
 * Queries
 * ========================================================================= */

size_t dc_epcb_footprint_count(const DC_EPcb *pcb);
size_t dc_epcb_track_count(const DC_EPcb *pcb);
size_t dc_epcb_via_count(const DC_EPcb *pcb);
size_t dc_epcb_zone_count(const DC_EPcb *pcb);
size_t dc_epcb_net_count(const DC_EPcb *pcb);

DC_PcbFootprint *dc_epcb_get_footprint(const DC_EPcb *pcb, size_t i);
DC_PcbTrack     *dc_epcb_get_track(const DC_EPcb *pcb, size_t i);
DC_PcbVia       *dc_epcb_get_via(const DC_EPcb *pcb, size_t i);
DC_PcbZone      *dc_epcb_get_zone(const DC_EPcb *pcb, size_t i);
DC_PcbNet       *dc_epcb_get_net(const DC_EPcb *pcb, size_t i);

DC_PcbDesignRules *dc_epcb_get_design_rules(DC_EPcb *pcb);

/* Find footprint by reference. Borrowed pointer. */
DC_PcbFootprint *dc_epcb_find_footprint(const DC_EPcb *pcb, const char *ref);

/* Find net by name. Returns net id, or -1 if not found. */
int dc_epcb_find_net(const DC_EPcb *pcb, const char *name);

/* =========================================================================
 * Mutation
 * ========================================================================= */

size_t dc_epcb_add_footprint(DC_EPcb *pcb, const char *lib_id,
                               const char *reference, double x, double y,
                               int layer);

size_t dc_epcb_add_track(DC_EPcb *pcb, double x1, double y1,
                           double x2, double y2, double width,
                           int layer, int net_id);

size_t dc_epcb_add_via(DC_EPcb *pcb, double x, double y,
                         double size, double drill, int net_id);

int dc_epcb_add_net(DC_EPcb *pcb, const char *name);

/* Add a zone (copper pour). Outline is a rectangle (x,y)→(x+w,y+h).
 * Returns index, or (size_t)-1 on error. */
size_t dc_epcb_add_zone(DC_EPcb *pcb, const char *net_name,
                          int layer, double clearance,
                          double x, double y, double w, double h);

/* Remove element by index. Returns 0 on success. */
int dc_epcb_remove_footprint(DC_EPcb *pcb, size_t index);
int dc_epcb_remove_track(DC_EPcb *pcb, size_t index);
int dc_epcb_remove_via(DC_EPcb *pcb, size_t index);
int dc_epcb_remove_zone(DC_EPcb *pcb, size_t index);

/* Import a netlist — creates footprints and assigns nets.
 * Existing footprints with matching references are updated, not duplicated. */
int dc_epcb_import_netlist(DC_EPcb *pcb, const DC_Netlist *nl, DC_Error *err);

#endif /* DC_EDA_PCB_H */
