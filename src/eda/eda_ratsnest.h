#ifndef DC_EDA_RATSNEST_H
#define DC_EDA_RATSNEST_H

/*
 * eda_ratsnest.h — Ratsnest (unrouted connections) for DunCAD EDA.
 *
 * Computes minimum spanning tree per net from pad positions and existing
 * copper (tracks/vias). The ratsnest lines show which connections still
 * need routing.
 *
 * Pure geometry — no GTK dependency. Added to dc_core.
 *
 * Ownership: DC_Ratsnest is heap-allocated. dc_ratsnest_free() releases all.
 */

#include "eda/eda_pcb.h"
#include <stddef.h>

/* A single ratsnest line: unrouted connection between two points. */
typedef struct {
    double x1, y1;
    double x2, y2;
    int    net_id;
} DC_RatsnestLine;

/* Opaque ratsnest container. */
typedef struct DC_Ratsnest DC_Ratsnest;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Compute ratsnest from a PCB. Returns NULL on error. */
DC_Ratsnest *dc_ratsnest_compute(const DC_EPcb *pcb);

/* Free ratsnest data. NULL is a no-op. */
void dc_ratsnest_free(DC_Ratsnest *rn);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Get the number of unrouted connections. */
size_t dc_ratsnest_line_count(const DC_Ratsnest *rn);

/* Get a ratsnest line by index. Borrowed pointer. */
const DC_RatsnestLine *dc_ratsnest_get_line(const DC_Ratsnest *rn, size_t i);

/* Get count of incomplete nets (nets with at least one ratsnest line). */
size_t dc_ratsnest_incomplete_net_count(const DC_Ratsnest *rn);

#endif /* DC_EDA_RATSNEST_H */
