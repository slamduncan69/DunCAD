#ifndef DC_EDA_NETLIST_H
#define DC_EDA_NETLIST_H

/*
 * eda_netlist.h — Net structures for EDA schematic-to-PCB flow.
 *
 * A netlist is the bridge between schematic and PCB: it describes which
 * component pins are electrically connected. Each net has a name and a list
 * of pin references (component_ref:pin_number).
 *
 * Ownership: DC_Netlist and all its contents are heap-allocated.
 * dc_netlist_free() releases everything.
 */

#include "core/array.h"
#include "core/error.h"
#include <stddef.h>

/* -------------------------------------------------------------------------
 * DC_NetPin — a single pin reference within a net.
 * ---------------------------------------------------------------------- */
typedef struct {
    char *component_ref;   /* e.g. "R1", "U1" — owned */
    char *pin_number;      /* e.g. "1", "2", "PA0" — owned */
} DC_NetPin;

/* -------------------------------------------------------------------------
 * DC_Net — a named net containing connected pins.
 * ---------------------------------------------------------------------- */
typedef struct {
    char     *name;        /* net name, e.g. "VCC", "GND", "Net-R1-2" — owned */
    DC_Array *pins;        /* DC_NetPin elements */
} DC_Net;

/* -------------------------------------------------------------------------
 * DC_Netlist — complete netlist: array of nets + component list.
 * ---------------------------------------------------------------------- */
typedef struct {
    DC_Array *nets;        /* DC_Net elements */
    DC_Array *components;  /* DC_NetlistComponent elements */
} DC_Netlist;

/* -------------------------------------------------------------------------
 * DC_NetlistComponent — component entry in the netlist.
 * ---------------------------------------------------------------------- */
typedef struct {
    char *ref;             /* reference designator, e.g. "R1" — owned */
    char *lib_id;          /* library identifier, e.g. "Device:R_Small" — owned */
    char *footprint;       /* footprint, e.g. "Resistor_SMD:R_0402_..." — owned, may be NULL */
    char *value;           /* value string, e.g. "10k" — owned, may be NULL */
} DC_NetlistComponent;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/* Create an empty netlist. Returns NULL on allocation failure. */
DC_Netlist *dc_netlist_new(void);

/* Free a netlist and all owned data. NULL is a no-op. */
void dc_netlist_free(DC_Netlist *nl);

/* Add a net to the netlist. Name is copied. Returns 0 on success. */
int dc_netlist_add_net(DC_Netlist *nl, const char *name);

/* Add a pin to an existing net (by net index). Strings are copied. */
int dc_netlist_add_pin(DC_Netlist *nl, size_t net_index,
                        const char *comp_ref, const char *pin_number);

/* Add a component to the netlist. Strings are copied; footprint/value may be NULL. */
int dc_netlist_add_component(DC_Netlist *nl, const char *ref,
                              const char *lib_id, const char *footprint,
                              const char *value);

/* Find a net by name. Returns index, or (size_t)-1 if not found. */
size_t dc_netlist_find_net(const DC_Netlist *nl, const char *name);

/* Get net count. */
size_t dc_netlist_net_count(const DC_Netlist *nl);

/* Get a net by index (borrowed pointer). */
DC_Net *dc_netlist_get_net(const DC_Netlist *nl, size_t index);

/* Export netlist as JSON string. Caller must free(). */
char *dc_netlist_to_json(const DC_Netlist *nl, DC_Error *err);

#endif /* DC_EDA_NETLIST_H */
