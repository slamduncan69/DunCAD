#define _POSIX_C_SOURCE 200809L
/*
 * eda_netlist.c — Net structures for EDA schematic-to-PCB flow.
 */

#include "eda/eda_netlist.h"
#include "core/string_builder.h"

#include <stdlib.h>
#include <string.h>

/* ---- Internal cleanup helpers ---- */

static void
net_pin_cleanup(DC_NetPin *pin)
{
    if (!pin) return;
    free(pin->component_ref);
    free(pin->pin_number);
}

static void
net_cleanup(DC_Net *net)
{
    if (!net) return;
    free(net->name);
    if (net->pins) {
        for (size_t i = 0; i < dc_array_length(net->pins); i++) {
            DC_NetPin *pin = dc_array_get(net->pins, i);
            net_pin_cleanup(pin);
        }
        dc_array_free(net->pins);
    }
}

static void
component_cleanup(DC_NetlistComponent *comp)
{
    if (!comp) return;
    free(comp->ref);
    free(comp->lib_id);
    free(comp->footprint);
    free(comp->value);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

DC_Netlist *
dc_netlist_new(void)
{
    DC_Netlist *nl = calloc(1, sizeof(DC_Netlist));
    if (!nl) return NULL;

    nl->nets = dc_array_new(sizeof(DC_Net));
    nl->components = dc_array_new(sizeof(DC_NetlistComponent));
    if (!nl->nets || !nl->components) {
        dc_netlist_free(nl);
        return NULL;
    }
    return nl;
}

void
dc_netlist_free(DC_Netlist *nl)
{
    if (!nl) return;
    if (nl->nets) {
        for (size_t i = 0; i < dc_array_length(nl->nets); i++) {
            DC_Net *net = dc_array_get(nl->nets, i);
            net_cleanup(net);
        }
        dc_array_free(nl->nets);
    }
    if (nl->components) {
        for (size_t i = 0; i < dc_array_length(nl->components); i++) {
            DC_NetlistComponent *comp = dc_array_get(nl->components, i);
            component_cleanup(comp);
        }
        dc_array_free(nl->components);
    }
    free(nl);
}

int
dc_netlist_add_net(DC_Netlist *nl, const char *name)
{
    if (!nl || !name) return -1;
    DC_Net net = {
        .name = strdup(name),
        .pins = dc_array_new(sizeof(DC_NetPin)),
    };
    if (!net.name || !net.pins) {
        free(net.name);
        dc_array_free(net.pins);
        return -1;
    }
    return dc_array_push(nl->nets, &net);
}

int
dc_netlist_add_pin(DC_Netlist *nl, size_t net_index,
                    const char *comp_ref, const char *pin_number)
{
    if (!nl || !comp_ref || !pin_number) return -1;
    if (net_index >= dc_array_length(nl->nets)) return -1;

    DC_Net *net = dc_array_get(nl->nets, net_index);
    DC_NetPin pin = {
        .component_ref = strdup(comp_ref),
        .pin_number    = strdup(pin_number),
    };
    if (!pin.component_ref || !pin.pin_number) {
        free(pin.component_ref);
        free(pin.pin_number);
        return -1;
    }
    return dc_array_push(net->pins, &pin);
}

int
dc_netlist_add_component(DC_Netlist *nl, const char *ref,
                          const char *lib_id, const char *footprint,
                          const char *value)
{
    if (!nl || !ref || !lib_id) return -1;
    DC_NetlistComponent comp = {
        .ref       = strdup(ref),
        .lib_id    = strdup(lib_id),
        .footprint = footprint ? strdup(footprint) : NULL,
        .value     = value ? strdup(value) : NULL,
    };
    if (!comp.ref || !comp.lib_id) {
        component_cleanup(&comp);
        return -1;
    }
    return dc_array_push(nl->components, &comp);
}

size_t
dc_netlist_find_net(const DC_Netlist *nl, const char *name)
{
    if (!nl || !name) return (size_t)-1;
    for (size_t i = 0; i < dc_array_length(nl->nets); i++) {
        DC_Net *net = dc_array_get(nl->nets, i);
        if (strcmp(net->name, name) == 0) return i;
    }
    return (size_t)-1;
}

size_t
dc_netlist_net_count(const DC_Netlist *nl)
{
    if (!nl) return 0;
    return dc_array_length(nl->nets);
}

DC_Net *
dc_netlist_get_net(const DC_Netlist *nl, size_t index)
{
    if (!nl) return NULL;
    return dc_array_get(nl->nets, index);
}

char *
dc_netlist_to_json(const DC_Netlist *nl, DC_Error *err)
{
    if (!nl) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL netlist");
        return NULL;
    }

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "sb alloc");
        return NULL;
    }

    dc_sb_append(sb, "{\n  \"nets\": [\n");
    for (size_t i = 0; i < dc_array_length(nl->nets); i++) {
        DC_Net *net = dc_array_get(nl->nets, i);
        dc_sb_appendf(sb, "    {\"name\": \"%s\", \"pins\": [", net->name);
        for (size_t j = 0; j < dc_array_length(net->pins); j++) {
            DC_NetPin *pin = dc_array_get(net->pins, j);
            if (j > 0) dc_sb_append(sb, ", ");
            dc_sb_appendf(sb, "\"%s:%s\"", pin->component_ref, pin->pin_number);
        }
        dc_sb_append(sb, "]}");
        if (i + 1 < dc_array_length(nl->nets)) dc_sb_append(sb, ",");
        dc_sb_append(sb, "\n");
    }

    dc_sb_append(sb, "  ],\n  \"components\": [\n");
    for (size_t i = 0; i < dc_array_length(nl->components); i++) {
        DC_NetlistComponent *comp = dc_array_get(nl->components, i);
        dc_sb_appendf(sb, "    {\"ref\": \"%s\", \"lib_id\": \"%s\"",
                       comp->ref, comp->lib_id);
        if (comp->footprint)
            dc_sb_appendf(sb, ", \"footprint\": \"%s\"", comp->footprint);
        if (comp->value)
            dc_sb_appendf(sb, ", \"value\": \"%s\"", comp->value);
        dc_sb_append(sb, "}");
        if (i + 1 < dc_array_length(nl->components)) dc_sb_append(sb, ",");
        dc_sb_append(sb, "\n");
    }
    dc_sb_append(sb, "  ]\n}");

    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}
