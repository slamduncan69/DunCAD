#define _POSIX_C_SOURCE 200809L

#include "eda_cubeiform_export.h"

#include "core/string_builder.h"
#include "eda/eda_schematic.h"
#include "eda/eda_pcb.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Schematic → Cubeiform
 * ========================================================================= */
char *
dc_eschematic_to_cubeiform(const DC_ESchematic *sch, DC_Error *err)
{
    if (!sch) {
        DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL schematic");
        return NULL;
    }

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) {
        DC_SET_ERROR(err, DC_ERROR_MEMORY, "alloc StringBuilder");
        return NULL;
    }

    dc_sb_append(sb, "schematic {\n");

    /* Components */
    for (size_t i = 0; i < dc_eschematic_symbol_count(sch); i++) {
        DC_SchSymbol *sym = dc_eschematic_get_symbol(sch, i);
        dc_sb_appendf(sb, "    component %s = \"%s\" at %.0f, %.0f",
                       sym->reference ? sym->reference : "?",
                       sym->lib_id ? sym->lib_id : "",
                       sym->x, sym->y);

        /* Check for value and footprint properties */
        const char *val = dc_eschematic_symbol_property(sym, "Value");
        const char *fp = dc_eschematic_symbol_property(sym, "Footprint");

        if (val && val[0]) {
            dc_sb_appendf(sb, " >> value(\"%s\")", val);
        }
        if (fp && fp[0]) {
            dc_sb_appendf(sb, " >> footprint(\"%s\")", fp);
        }

        dc_sb_append(sb, ";\n");
    }

    if (dc_eschematic_symbol_count(sch) > 0 &&
        (dc_eschematic_label_count(sch) > 0 || dc_eschematic_power_port_count(sch) > 0)) {
        dc_sb_append(sb, "\n");
    }

    /* Net labels — we don't reconstruct wire statements here since
     * the wire topology information (which pins connect) isn't preserved
     * in individual label objects. Just emit labels as comments for context. */

    /* Power ports */
    for (size_t i = 0; i < dc_eschematic_power_port_count(sch); i++) {
        DC_SchPowerPort *pp = dc_eschematic_get_power_port(sch, i);
        dc_sb_appendf(sb, "    power %s at %.0f, %.0f;\n",
                       pp->name ? pp->name : "?",
                       pp->x, pp->y);
    }

    dc_sb_append(sb, "}\n");

    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}

/* =========================================================================
 * PCB → Cubeiform
 * ========================================================================= */
char *
dc_epcb_to_cubeiform(const DC_EPcb *pcb, DC_Error *err)
{
    if (!pcb) {
        DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL pcb");
        return NULL;
    }

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) {
        DC_SET_ERROR(err, DC_ERROR_MEMORY, "alloc StringBuilder");
        return NULL;
    }

    dc_sb_append(sb, "pcb {\n");

    /* Design rules */
    DC_PcbDesignRules *rules = dc_epcb_get_design_rules((DC_EPcb *)pcb);
    if (rules) {
        dc_sb_append(sb, "    rules {\n");
        if (rules->clearance > 0)
            dc_sb_appendf(sb, "        clearance = %.2f;\n", rules->clearance);
        if (rules->track_width > 0)
            dc_sb_appendf(sb, "        track_width = %.2f;\n", rules->track_width);
        if (rules->via_size > 0)
            dc_sb_appendf(sb, "        via_size = %.2f;\n", rules->via_size);
        if (rules->via_drill > 0)
            dc_sb_appendf(sb, "        via_drill = %.2f;\n", rules->via_drill);
        dc_sb_append(sb, "    }\n\n");
    }

    /* Footprint placement */
    for (size_t i = 0; i < dc_epcb_footprint_count(pcb); i++) {
        DC_PcbFootprint *fp = dc_epcb_get_footprint(pcb, i);
        const char *layer_name = dc_pcb_layer_to_name(fp->layer);

        dc_sb_appendf(sb, "    place %s at %.2f, %.2f on %s",
                       fp->reference ? fp->reference : "?",
                       fp->x, fp->y,
                       layer_name);

        if (fp->angle != 0.0) {
            dc_sb_appendf(sb, " >> rotate(%.0f)", fp->angle);
        }

        dc_sb_append(sb, ";\n");
    }

    /* Tracks — group by net and emit as route blocks where possible */
    if (dc_epcb_track_count(pcb) > 0) {
        dc_sb_append(sb, "\n");
        /* Simple: emit individual track segments as comments + raw info.
         * A smarter implementation would reconstruct route blocks. */
        for (size_t i = 0; i < dc_epcb_track_count(pcb); i++) {
            DC_PcbTrack *t = dc_epcb_get_track(pcb, i);
            const char *layer_name = dc_pcb_layer_to_name(t->layer);

            /* Find net name */
            const char *net_name = "?";
            for (size_t j = 0; j < dc_epcb_net_count(pcb); j++) {
                DC_PcbNet *n = dc_epcb_get_net(pcb, j);
                if (n->id == t->net_id && n->name) {
                    net_name = n->name;
                    break;
                }
            }

            /* Skip edge cuts tracks (board outline) */
            if (t->layer == DC_PCB_LAYER_EDGE_CUTS) continue;

            dc_sb_appendf(sb,
                "    // track: net=%s layer=%s width=%.2f (%.2f,%.2f)-(%.2f,%.2f)\n",
                net_name, layer_name, t->width,
                t->x1, t->y1, t->x2, t->y2);
        }
    }

    dc_sb_append(sb, "}\n");

    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}
