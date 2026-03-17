#ifndef DC_EDA_SCHEMATIC_H
#define DC_EDA_SCHEMATIC_H

/*
 * eda_schematic.h — Schematic data model for DunCAD EDA.
 *
 * Represents a KiCad-compatible schematic: symbols, wires, labels,
 * junctions, and power ports. Supports:
 *   - Loading from KiCad .kicad_sch s-expression files
 *   - Saving back to .kicad_sch format
 *   - Programmatic manipulation (add/remove/move elements)
 *   - Netlist generation
 *
 * Coordinates use KiCad's convention: mils (1/1000 inch), Y-down.
 *
 * Ownership: DC_ESchematic is opaque, heap-allocated. All strings within
 * are owned. dc_eschematic_free() releases everything.
 */

#include "core/array.h"
#include "core/error.h"
#include "eda/eda_netlist.h"
#include "eda/sexpr.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * Property — key/value pair attached to symbols
 * ---------------------------------------------------------------------- */
typedef struct {
    char  *key;        /* e.g. "Reference", "Value", "Footprint" — owned */
    char  *value;      /* e.g. "R1", "10k", "R_0402" — owned */
    double x, y;       /* position */
    double angle;      /* rotation in degrees */
    bool   visible;
} DC_SchProperty;

/* -------------------------------------------------------------------------
 * Pin — resolved pin info from a symbol instance
 * ---------------------------------------------------------------------- */
typedef struct {
    char  *number;     /* pin number/name, e.g. "1", "PA0" — owned */
    char  *name;       /* pin display name — owned */
    double x, y;       /* absolute position (after symbol transforms) */
} DC_SchPin;

/* -------------------------------------------------------------------------
 * Symbol instance on the schematic
 * ---------------------------------------------------------------------- */
typedef struct {
    char   *lib_id;          /* library identifier, e.g. "Device:R_Small" — owned */
    char   *reference;       /* e.g. "R1" — owned */
    double  x, y;            /* position */
    double  angle;           /* rotation in degrees (0, 90, 180, 270) */
    bool    mirror;          /* horizontal mirror */
    char   *uuid;            /* unique identifier — owned */

    DC_Array *properties;    /* DC_SchProperty elements */
    DC_Array *pins;          /* DC_SchPin elements (resolved positions) */

    /* Original s-expression for lossless roundtrip of unmodeled data */
    DC_Sexpr *raw;           /* borrowed from loaded file, or NULL */
} DC_SchSymbol;

/* -------------------------------------------------------------------------
 * Wire segment
 * ---------------------------------------------------------------------- */
typedef struct {
    double x1, y1;
    double x2, y2;
    char  *uuid;            /* owned */
} DC_SchWire;

/* -------------------------------------------------------------------------
 * Net label — attaches a net name to a wire at a position
 * ---------------------------------------------------------------------- */
typedef struct {
    char  *name;            /* net name — owned */
    double x, y;
    double angle;
    char  *uuid;            /* owned */
} DC_SchLabel;

/* -------------------------------------------------------------------------
 * Junction — explicit wire crossing connection point
 * ---------------------------------------------------------------------- */
typedef struct {
    double x, y;
    char  *uuid;            /* owned */
} DC_SchJunction;

/* -------------------------------------------------------------------------
 * Power port — global power symbol (VCC, GND, etc.)
 * ---------------------------------------------------------------------- */
typedef struct {
    char  *name;            /* power net name — owned */
    char  *lib_id;          /* symbol lib_id — owned */
    double x, y;
    double angle;
    char  *uuid;            /* owned */
} DC_SchPowerPort;

/* -------------------------------------------------------------------------
 * DC_ESchematic — opaque schematic container
 * ---------------------------------------------------------------------- */
typedef struct DC_ESchematic DC_ESchematic;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Create an empty schematic. */
DC_ESchematic *dc_eschematic_new(void);

/* Free a schematic and all owned data. NULL is a no-op. */
void dc_eschematic_free(DC_ESchematic *sch);

/* =========================================================================
 * I/O — KiCad format
 * ========================================================================= */

/* Load from KiCad .kicad_sch file. */
DC_ESchematic *dc_eschematic_load(const char *path, DC_Error *err);

/* Load from s-expression AST (takes ownership of ast). */
DC_ESchematic *dc_eschematic_from_sexpr(DC_Sexpr *ast, DC_Error *err);

/* Save to KiCad .kicad_sch file. */
int dc_eschematic_save(const DC_ESchematic *sch, const char *path, DC_Error *err);

/* Serialize to s-expression string. Caller must free(). */
char *dc_eschematic_to_sexpr_string(const DC_ESchematic *sch, DC_Error *err);

/* =========================================================================
 * Queries
 * ========================================================================= */

size_t dc_eschematic_symbol_count(const DC_ESchematic *sch);
size_t dc_eschematic_wire_count(const DC_ESchematic *sch);
size_t dc_eschematic_label_count(const DC_ESchematic *sch);
size_t dc_eschematic_junction_count(const DC_ESchematic *sch);
size_t dc_eschematic_power_port_count(const DC_ESchematic *sch);

/* Borrowed pointers — valid until next mutation. */
DC_SchSymbol    *dc_eschematic_get_symbol(const DC_ESchematic *sch, size_t i);
DC_SchWire      *dc_eschematic_get_wire(const DC_ESchematic *sch, size_t i);
DC_SchLabel     *dc_eschematic_get_label(const DC_ESchematic *sch, size_t i);
DC_SchJunction  *dc_eschematic_get_junction(const DC_ESchematic *sch, size_t i);
DC_SchPowerPort *dc_eschematic_get_power_port(const DC_ESchematic *sch, size_t i);

/* Find symbol by reference designator. Returns NULL if not found. Borrowed. */
DC_SchSymbol *dc_eschematic_find_symbol(const DC_ESchematic *sch, const char *ref);

/* Get a property value from a symbol. Returns NULL if not found. Borrowed. */
const char *dc_eschematic_symbol_property(const DC_SchSymbol *sym, const char *key);

/* =========================================================================
 * Mutation
 * ========================================================================= */

/* Add a symbol. Strings are copied. Returns index, or (size_t)-1 on error. */
size_t dc_eschematic_add_symbol(DC_ESchematic *sch, const char *lib_id,
                                 const char *reference, double x, double y);

/* Add a wire segment. Returns index, or (size_t)-1 on error. */
size_t dc_eschematic_add_wire(DC_ESchematic *sch,
                               double x1, double y1, double x2, double y2);

/* Add a net label. Returns index, or (size_t)-1 on error. */
size_t dc_eschematic_add_label(DC_ESchematic *sch, const char *name,
                                double x, double y);

/* Add a junction. Returns index, or (size_t)-1 on error. */
size_t dc_eschematic_add_junction(DC_ESchematic *sch, double x, double y);

/* Add a power port. Returns index, or (size_t)-1 on error. */
size_t dc_eschematic_add_power_port(DC_ESchematic *sch, const char *name,
                                     double x, double y);

/* Set a property on a symbol. Key and value are copied. */
int dc_eschematic_set_property(DC_ESchematic *sch, size_t symbol_index,
                                const char *key, const char *value);

/* Remove element by index. Returns 0 on success. */
int dc_eschematic_remove_symbol(DC_ESchematic *sch, size_t index);
int dc_eschematic_remove_wire(DC_ESchematic *sch, size_t index);

/* =========================================================================
 * Netlist generation
 * ========================================================================= */

/* Generate a netlist from the current schematic state.
 * The netlist is a standalone object; caller must dc_netlist_free() it. */
DC_Netlist *dc_eschematic_generate_netlist(const DC_ESchematic *sch, DC_Error *err);

#endif /* DC_EDA_SCHEMATIC_H */
