#define _POSIX_C_SOURCE 200809L
/*
 * eda_schematic.c — Schematic data model: load, save, manipulate, netlist.
 *
 * Parses KiCad 6+ .kicad_sch s-expression format into a structured model.
 * The model can be mutated programmatically and serialized back.
 */

#include "eda/eda_schematic.h"
#include "core/string_builder.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal structure
 * ========================================================================= */
struct DC_ESchematic {
    DC_Array *symbols;       /* DC_SchSymbol */
    DC_Array *wires;         /* DC_SchWire */
    DC_Array *labels;        /* DC_SchLabel */
    DC_Array *junctions;     /* DC_SchJunction */
    DC_Array *power_ports;   /* DC_SchPowerPort */

    /* Raw AST for lossless roundtrip of header/version/uuid */
    DC_Sexpr *raw_ast;       /* owned, or NULL */
    char     *version;       /* owned, e.g. "20230121" */
    char     *uuid;          /* owned, schematic UUID */
};

/* ---- Cleanup helpers ---- */

static void
property_cleanup(DC_SchProperty *prop)
{
    free(prop->key);
    free(prop->value);
}

static void
pin_cleanup(DC_SchPin *pin)
{
    free(pin->number);
    free(pin->name);
}

static void
symbol_cleanup(DC_SchSymbol *sym)
{
    free(sym->lib_id);
    free(sym->reference);
    free(sym->uuid);
    if (sym->properties) {
        for (size_t i = 0; i < dc_array_length(sym->properties); i++)
            property_cleanup(dc_array_get(sym->properties, i));
        dc_array_free(sym->properties);
    }
    if (sym->pins) {
        for (size_t i = 0; i < dc_array_length(sym->pins); i++)
            pin_cleanup(dc_array_get(sym->pins, i));
        dc_array_free(sym->pins);
    }
}

static void
wire_cleanup(DC_SchWire *w)
{
    free(w->uuid);
}

static void
label_cleanup(DC_SchLabel *l)
{
    free(l->name);
    free(l->uuid);
}

static void
junction_cleanup(DC_SchJunction *j)
{
    free(j->uuid);
}

static void
power_port_cleanup(DC_SchPowerPort *pp)
{
    free(pp->name);
    free(pp->lib_id);
    free(pp->uuid);
}

/* ---- UUID generation (simple counter-based for programmatic use) ---- */
static int s_uuid_counter = 0;

static char *
generate_uuid(void)
{
    char buf[64];
    unsigned a = (unsigned)s_uuid_counter++;
    snprintf(buf, sizeof(buf), "dcad-%08x-0000-0000-0000-%012x", a, a + 1);
    return strdup(buf);
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_ESchematic *
dc_eschematic_new(void)
{
    DC_ESchematic *sch = calloc(1, sizeof(DC_ESchematic));
    if (!sch) return NULL;

    sch->symbols     = dc_array_new(sizeof(DC_SchSymbol));
    sch->wires       = dc_array_new(sizeof(DC_SchWire));
    sch->labels      = dc_array_new(sizeof(DC_SchLabel));
    sch->junctions   = dc_array_new(sizeof(DC_SchJunction));
    sch->power_ports = dc_array_new(sizeof(DC_SchPowerPort));

    if (!sch->symbols || !sch->wires || !sch->labels ||
        !sch->junctions || !sch->power_ports) {
        dc_eschematic_free(sch);
        return NULL;
    }
    return sch;
}

void
dc_eschematic_free(DC_ESchematic *sch)
{
    if (!sch) return;
    if (sch->symbols) {
        for (size_t i = 0; i < dc_array_length(sch->symbols); i++)
            symbol_cleanup(dc_array_get(sch->symbols, i));
        dc_array_free(sch->symbols);
    }
    if (sch->wires) {
        for (size_t i = 0; i < dc_array_length(sch->wires); i++)
            wire_cleanup(dc_array_get(sch->wires, i));
        dc_array_free(sch->wires);
    }
    if (sch->labels) {
        for (size_t i = 0; i < dc_array_length(sch->labels); i++)
            label_cleanup(dc_array_get(sch->labels, i));
        dc_array_free(sch->labels);
    }
    if (sch->junctions) {
        for (size_t i = 0; i < dc_array_length(sch->junctions); i++)
            junction_cleanup(dc_array_get(sch->junctions, i));
        dc_array_free(sch->junctions);
    }
    if (sch->power_ports) {
        for (size_t i = 0; i < dc_array_length(sch->power_ports); i++)
            power_port_cleanup(dc_array_get(sch->power_ports, i));
        dc_array_free(sch->power_ports);
    }
    dc_sexpr_free(sch->raw_ast);
    free(sch->version);
    free(sch->uuid);
    free(sch);
}

/* =========================================================================
 * S-expression loading helpers
 * ========================================================================= */

static double
parse_double(const char *s)
{
    return s ? strtod(s, NULL) : 0.0;
}

/* Extract (at x y) from a node. */
static void
parse_at(const DC_Sexpr *node, double *x, double *y, double *angle)
{
    DC_Sexpr *at_node = dc_sexpr_find(node, "at");
    if (!at_node) return;
    if (x) *x = parse_double(dc_sexpr_value_at(at_node, 0));
    if (y) *y = parse_double(dc_sexpr_value_at(at_node, 1));
    if (angle) {
        const char *a = dc_sexpr_value_at(at_node, 2);
        *angle = a ? parse_double(a) : 0.0;
    }
}

/* Extract (uuid "...") from a node. Returns strdup'd string. */
static char *
parse_uuid(const DC_Sexpr *node)
{
    DC_Sexpr *uuid_node = dc_sexpr_find(node, "uuid");
    if (!uuid_node) return generate_uuid();
    const char *val = dc_sexpr_value(uuid_node);
    return val ? strdup(val) : generate_uuid();
}

/* Parse (property "key" "value" (at x y angle) ...) */
static DC_SchProperty
parse_property(const DC_Sexpr *prop_node)
{
    DC_SchProperty prop = {0};
    prop.key = strdup(dc_sexpr_value_at(prop_node, 0) ? dc_sexpr_value_at(prop_node, 0) : "");
    prop.value = strdup(dc_sexpr_value_at(prop_node, 1) ? dc_sexpr_value_at(prop_node, 1) : "");
    parse_at(prop_node, &prop.x, &prop.y, &prop.angle);

    /* Check for (effects (hide yes)) or similar visibility */
    prop.visible = true;
    DC_Sexpr *effects = dc_sexpr_find(prop_node, "effects");
    if (effects) {
        DC_Sexpr *hide = dc_sexpr_find(effects, "hide");
        if (hide) prop.visible = false;
        /* KiCad 8+ uses (hide yes) as direct child */
        for (size_t i = 0; i < dc_sexpr_child_count(effects); i++) {
            DC_Sexpr *c = effects->children[i];
            if (c->type == DC_SEXPR_ATOM && c->value &&
                strcmp(c->value, "hide") == 0) {
                prop.visible = false;
            }
        }
    }
    return prop;
}

/* Parse a symbol instance from (symbol ...) s-expression */
static DC_SchSymbol
parse_symbol(const DC_Sexpr *sym_node)
{
    DC_SchSymbol sym = {0};
    sym.properties = dc_array_new(sizeof(DC_SchProperty));
    sym.pins = dc_array_new(sizeof(DC_SchPin));

    /* (symbol (lib_id "Device:R_Small") ...) */
    DC_Sexpr *lib_node = dc_sexpr_find(sym_node, "lib_id");
    if (lib_node) {
        const char *v = dc_sexpr_value(lib_node);
        sym.lib_id = v ? strdup(v) : strdup("");
    } else {
        sym.lib_id = strdup("");
    }

    parse_at(sym_node, &sym.x, &sym.y, &sym.angle);
    sym.uuid = parse_uuid(sym_node);

    /* Mirror: (mirror x) or (mirror y) */
    DC_Sexpr *mirror = dc_sexpr_find(sym_node, "mirror");
    if (mirror) sym.mirror = true;

    /* Properties */
    size_t prop_count = 0;
    DC_Sexpr **props = dc_sexpr_find_all(sym_node, "property", &prop_count);
    if (props) {
        for (size_t i = 0; i < prop_count; i++) {
            DC_SchProperty prop = parse_property(props[i]);
            dc_array_push(sym.properties, &prop);

            /* Extract reference from properties */
            if (strcmp(prop.key, "Reference") == 0) {
                free(sym.reference);
                sym.reference = strdup(prop.value);
            }
        }
        free(props);
    }

    if (!sym.reference) sym.reference = strdup("");
    return sym;
}

/* Parse (wire (pts (xy x1 y1) (xy x2 y2)) ...) */
static DC_SchWire
parse_wire(const DC_Sexpr *wire_node)
{
    DC_SchWire w = {0};
    w.uuid = parse_uuid(wire_node);

    DC_Sexpr *pts = dc_sexpr_find(wire_node, "pts");
    if (pts) {
        size_t xy_count = 0;
        DC_Sexpr **xys = dc_sexpr_find_all(pts, "xy", &xy_count);
        if (xys) {
            if (xy_count >= 1) {
                w.x1 = parse_double(dc_sexpr_value_at(xys[0], 0));
                w.y1 = parse_double(dc_sexpr_value_at(xys[0], 1));
            }
            if (xy_count >= 2) {
                w.x2 = parse_double(dc_sexpr_value_at(xys[1], 0));
                w.y2 = parse_double(dc_sexpr_value_at(xys[1], 1));
            }
            free(xys);
        }
    }
    return w;
}

/* Parse (label "name" (at x y angle) ...) */
static DC_SchLabel
parse_label(const DC_Sexpr *label_node)
{
    DC_SchLabel l = {0};
    const char *name = dc_sexpr_value(label_node);
    l.name = name ? strdup(name) : strdup("");
    parse_at(label_node, &l.x, &l.y, &l.angle);
    l.uuid = parse_uuid(label_node);
    return l;
}

/* Parse (junction (at x y) ...) */
static DC_SchJunction
parse_junction(const DC_Sexpr *j_node)
{
    DC_SchJunction j = {0};
    parse_at(j_node, &j.x, &j.y, NULL);
    j.uuid = parse_uuid(j_node);
    return j;
}

/* Parse power_port from (symbol ...) that is a power symbol */
static DC_SchPowerPort
parse_power_port_from_symbol(const DC_SchSymbol *sym)
{
    DC_SchPowerPort pp = {0};
    /* Power symbols have lib_id starting with "power:" */
    pp.lib_id = strdup(sym->lib_id);
    pp.x = sym->x;
    pp.y = sym->y;
    pp.angle = sym->angle;
    pp.uuid = strdup(sym->uuid);

    /* The value property is the power net name */
    const char *val = dc_eschematic_symbol_property(sym, "Value");
    pp.name = val ? strdup(val) : strdup(sym->reference);
    return pp;
}

/* =========================================================================
 * I/O
 * ========================================================================= */

DC_ESchematic *
dc_eschematic_from_sexpr(DC_Sexpr *ast, DC_Error *err)
{
    if (!ast) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL AST");
        return NULL;
    }

    const char *tag = dc_sexpr_tag(ast);
    if (!tag || strcmp(tag, "kicad_sch") != 0) {
        if (err) DC_SET_ERROR(err, DC_ERROR_PARSE, "expected (kicad_sch ...), got (%s ...)",
                               tag ? tag : "NULL");
        dc_sexpr_free(ast);
        return NULL;
    }

    DC_ESchematic *sch = dc_eschematic_new();
    if (!sch) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "schematic alloc");
        dc_sexpr_free(ast);
        return NULL;
    }

    /* Version */
    DC_Sexpr *ver = dc_sexpr_find(ast, "version");
    if (ver) sch->version = strdup(dc_sexpr_value(ver));

    /* UUID */
    sch->uuid = parse_uuid(ast);

    /* Symbols */
    size_t sym_count = 0;
    DC_Sexpr **syms = dc_sexpr_find_all(ast, "symbol", &sym_count);
    if (syms) {
        for (size_t i = 0; i < sym_count; i++) {
            DC_SchSymbol sym = parse_symbol(syms[i]);

            /* Check if this is a power symbol → also add as power port */
            if (sym.lib_id && strncmp(sym.lib_id, "power:", 6) == 0) {
                DC_SchPowerPort pp = parse_power_port_from_symbol(&sym);
                dc_array_push(sch->power_ports, &pp);
            }

            dc_array_push(sch->symbols, &sym);
        }
        free(syms);
    }

    /* Wires */
    size_t wire_count = 0;
    DC_Sexpr **wires = dc_sexpr_find_all(ast, "wire", &wire_count);
    if (wires) {
        for (size_t i = 0; i < wire_count; i++) {
            DC_SchWire w = parse_wire(wires[i]);
            dc_array_push(sch->wires, &w);
        }
        free(wires);
    }

    /* Labels */
    size_t label_count = 0;
    DC_Sexpr **labels = dc_sexpr_find_all(ast, "label", &label_count);
    if (labels) {
        for (size_t i = 0; i < label_count; i++) {
            DC_SchLabel l = parse_label(labels[i]);
            dc_array_push(sch->labels, &l);
        }
        free(labels);
    }

    /* Global labels */
    size_t glabel_count = 0;
    DC_Sexpr **glabels = dc_sexpr_find_all(ast, "global_label", &glabel_count);
    if (glabels) {
        for (size_t i = 0; i < glabel_count; i++) {
            DC_SchLabel l = parse_label(glabels[i]);
            dc_array_push(sch->labels, &l);
        }
        free(glabels);
    }

    /* Junctions */
    size_t junc_count = 0;
    DC_Sexpr **juncs = dc_sexpr_find_all(ast, "junction", &junc_count);
    if (juncs) {
        for (size_t i = 0; i < junc_count; i++) {
            DC_SchJunction j = parse_junction(juncs[i]);
            dc_array_push(sch->junctions, &j);
        }
        free(juncs);
    }

    sch->raw_ast = ast;
    return sch;
}

DC_ESchematic *
dc_eschematic_load(const char *path, DC_Error *err)
{
    if (!path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL path");
        return NULL;
    }

    /* Read file */
    FILE *f = fopen(path, "r");
    if (!f) {
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "cannot open %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *text = malloc((size_t)size + 1);
    if (!text) {
        fclose(f);
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "file read alloc");
        return NULL;
    }

    size_t read = fread(text, 1, (size_t)size, f);
    text[read] = '\0';
    fclose(f);

    /* Parse s-expression */
    DC_Sexpr *ast = dc_sexpr_parse(text, err);
    free(text);
    if (!ast) return NULL;

    return dc_eschematic_from_sexpr(ast, err);
}

int
dc_eschematic_save(const DC_ESchematic *sch, const char *path, DC_Error *err)
{
    if (!sch || !path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL arg");
        return -1;
    }

    char *text = dc_eschematic_to_sexpr_string(sch, err);
    if (!text) return -1;

    FILE *f = fopen(path, "w");
    if (!f) {
        free(text);
        if (err) DC_SET_ERROR(err, DC_ERROR_IO, "cannot write %s", path);
        return -1;
    }

    fputs(text, f);
    fclose(f);
    free(text);
    return 0;
}

char *
dc_eschematic_to_sexpr_string(const DC_ESchematic *sch, DC_Error *err)
{
    if (!sch) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL schematic");
        return NULL;
    }

    /* If we have the original AST, roundtrip it */
    if (sch->raw_ast) {
        return dc_sexpr_write(sch->raw_ast, err);
    }

    /* Otherwise, generate from scratch */
    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "sb alloc");
        return NULL;
    }

    dc_sb_append(sb, "(kicad_sch\n");
    dc_sb_appendf(sb, "  (version %s)\n", sch->version ? sch->version : "20230121");
    dc_sb_appendf(sb, "  (generator \"duncad\")\n");
    dc_sb_appendf(sb, "  (uuid \"%s\")\n", sch->uuid ? sch->uuid : "00000000-0000-0000-0000-000000000000");
    dc_sb_append(sb, "  (paper \"A4\")\n");
    dc_sb_append(sb, "  (lib_symbols)\n");

    /* Symbols */
    for (size_t i = 0; i < dc_array_length(sch->symbols); i++) {
        DC_SchSymbol *sym = dc_array_get(sch->symbols, i);
        dc_sb_appendf(sb, "  (symbol\n");
        dc_sb_appendf(sb, "    (lib_id \"%s\")\n", sym->lib_id);
        dc_sb_appendf(sb, "    (at %.2f %.2f %.0f)\n", sym->x, sym->y, sym->angle);
        if (sym->mirror) dc_sb_append(sb, "    (mirror x)\n");
        dc_sb_appendf(sb, "    (uuid \"%s\")\n", sym->uuid);

        /* Properties */
        if (sym->properties) {
            for (size_t j = 0; j < dc_array_length(sym->properties); j++) {
                DC_SchProperty *prop = dc_array_get(sym->properties, j);
                dc_sb_appendf(sb, "    (property \"%s\" \"%s\"", prop->key, prop->value);
                dc_sb_appendf(sb, " (at %.2f %.2f %.0f)", prop->x, prop->y, prop->angle);
                dc_sb_append(sb, ")\n");
            }
        }
        dc_sb_append(sb, "  )\n");
    }

    /* Wires */
    for (size_t i = 0; i < dc_array_length(sch->wires); i++) {
        DC_SchWire *w = dc_array_get(sch->wires, i);
        dc_sb_appendf(sb, "  (wire (pts (xy %.2f %.2f) (xy %.2f %.2f))\n",
                       w->x1, w->y1, w->x2, w->y2);
        dc_sb_appendf(sb, "    (uuid \"%s\")\n", w->uuid);
        dc_sb_append(sb, "  )\n");
    }

    /* Labels */
    for (size_t i = 0; i < dc_array_length(sch->labels); i++) {
        DC_SchLabel *l = dc_array_get(sch->labels, i);
        dc_sb_appendf(sb, "  (label \"%s\" (at %.2f %.2f %.0f)\n",
                       l->name, l->x, l->y, l->angle);
        dc_sb_appendf(sb, "    (uuid \"%s\")\n", l->uuid);
        dc_sb_append(sb, "  )\n");
    }

    /* Junctions */
    for (size_t i = 0; i < dc_array_length(sch->junctions); i++) {
        DC_SchJunction *j = dc_array_get(sch->junctions, i);
        dc_sb_appendf(sb, "  (junction (at %.2f %.2f)\n", j->x, j->y);
        dc_sb_appendf(sb, "    (uuid \"%s\")\n", j->uuid);
        dc_sb_append(sb, "  )\n");
    }

    dc_sb_append(sb, ")\n");

    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}

/* =========================================================================
 * Queries
 * ========================================================================= */

size_t dc_eschematic_symbol_count(const DC_ESchematic *sch) {
    return sch ? dc_array_length(sch->symbols) : 0;
}
size_t dc_eschematic_wire_count(const DC_ESchematic *sch) {
    return sch ? dc_array_length(sch->wires) : 0;
}
size_t dc_eschematic_label_count(const DC_ESchematic *sch) {
    return sch ? dc_array_length(sch->labels) : 0;
}
size_t dc_eschematic_junction_count(const DC_ESchematic *sch) {
    return sch ? dc_array_length(sch->junctions) : 0;
}
size_t dc_eschematic_power_port_count(const DC_ESchematic *sch) {
    return sch ? dc_array_length(sch->power_ports) : 0;
}

DC_SchSymbol *dc_eschematic_get_symbol(const DC_ESchematic *sch, size_t i) {
    return sch ? dc_array_get(sch->symbols, i) : NULL;
}
DC_SchWire *dc_eschematic_get_wire(const DC_ESchematic *sch, size_t i) {
    return sch ? dc_array_get(sch->wires, i) : NULL;
}
DC_SchLabel *dc_eschematic_get_label(const DC_ESchematic *sch, size_t i) {
    return sch ? dc_array_get(sch->labels, i) : NULL;
}
DC_SchJunction *dc_eschematic_get_junction(const DC_ESchematic *sch, size_t i) {
    return sch ? dc_array_get(sch->junctions, i) : NULL;
}
DC_SchPowerPort *dc_eschematic_get_power_port(const DC_ESchematic *sch, size_t i) {
    return sch ? dc_array_get(sch->power_ports, i) : NULL;
}

DC_SchSymbol *
dc_eschematic_find_symbol(const DC_ESchematic *sch, const char *ref)
{
    if (!sch || !ref) return NULL;
    for (size_t i = 0; i < dc_array_length(sch->symbols); i++) {
        DC_SchSymbol *sym = dc_array_get(sch->symbols, i);
        if (sym->reference && strcmp(sym->reference, ref) == 0)
            return sym;
    }
    return NULL;
}

const char *
dc_eschematic_symbol_property(const DC_SchSymbol *sym, const char *key)
{
    if (!sym || !key || !sym->properties) return NULL;
    for (size_t i = 0; i < dc_array_length(sym->properties); i++) {
        DC_SchProperty *prop = dc_array_get(sym->properties, i);
        if (strcmp(prop->key, key) == 0) return prop->value;
    }
    return NULL;
}

/* =========================================================================
 * Mutation
 * ========================================================================= */

size_t
dc_eschematic_add_symbol(DC_ESchematic *sch, const char *lib_id,
                           const char *reference, double x, double y)
{
    if (!sch || !lib_id || !reference) return (size_t)-1;

    DC_SchSymbol sym = {0};
    sym.lib_id = strdup(lib_id);
    sym.reference = strdup(reference);
    sym.x = x;
    sym.y = y;
    sym.uuid = generate_uuid();
    sym.properties = dc_array_new(sizeof(DC_SchProperty));
    sym.pins = dc_array_new(sizeof(DC_SchPin));

    if (!sym.lib_id || !sym.reference || !sym.uuid ||
        !sym.properties || !sym.pins) {
        symbol_cleanup(&sym);
        return (size_t)-1;
    }

    /* Add default properties */
    DC_SchProperty ref_prop = {
        .key = strdup("Reference"),
        .value = strdup(reference),
        .x = x, .y = y - 2.54,
        .visible = true,
    };
    dc_array_push(sym.properties, &ref_prop);

    DC_SchProperty val_prop = {
        .key = strdup("Value"),
        .value = strdup(lib_id),
        .x = x, .y = y + 2.54,
        .visible = true,
    };
    dc_array_push(sym.properties, &val_prop);

    size_t idx = dc_array_length(sch->symbols);
    if (dc_array_push(sch->symbols, &sym) != 0) return (size_t)-1;
    return idx;
}

size_t
dc_eschematic_add_wire(DC_ESchematic *sch,
                         double x1, double y1, double x2, double y2)
{
    if (!sch) return (size_t)-1;
    DC_SchWire w = { .x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2,
                      .uuid = generate_uuid() };
    if (!w.uuid) return (size_t)-1;
    size_t idx = dc_array_length(sch->wires);
    if (dc_array_push(sch->wires, &w) != 0) return (size_t)-1;
    return idx;
}

size_t
dc_eschematic_add_label(DC_ESchematic *sch, const char *name,
                          double x, double y)
{
    if (!sch || !name) return (size_t)-1;
    DC_SchLabel l = { .name = strdup(name), .x = x, .y = y,
                       .uuid = generate_uuid() };
    if (!l.name || !l.uuid) { free(l.name); free(l.uuid); return (size_t)-1; }
    size_t idx = dc_array_length(sch->labels);
    if (dc_array_push(sch->labels, &l) != 0) return (size_t)-1;
    return idx;
}

size_t
dc_eschematic_add_junction(DC_ESchematic *sch, double x, double y)
{
    if (!sch) return (size_t)-1;
    DC_SchJunction j = { .x = x, .y = y, .uuid = generate_uuid() };
    if (!j.uuid) return (size_t)-1;
    size_t idx = dc_array_length(sch->junctions);
    if (dc_array_push(sch->junctions, &j) != 0) return (size_t)-1;
    return idx;
}

size_t
dc_eschematic_add_power_port(DC_ESchematic *sch, const char *name,
                               double x, double y)
{
    if (!sch || !name) return (size_t)-1;
    DC_SchPowerPort pp = {
        .name = strdup(name),
        .lib_id = strdup("power:VCC"),  /* default; caller can change */
        .x = x, .y = y,
        .uuid = generate_uuid(),
    };
    if (!pp.name || !pp.lib_id || !pp.uuid) {
        power_port_cleanup(&pp);
        return (size_t)-1;
    }
    size_t idx = dc_array_length(sch->power_ports);
    if (dc_array_push(sch->power_ports, &pp) != 0) return (size_t)-1;
    return idx;
}

int
dc_eschematic_set_property(DC_ESchematic *sch, size_t symbol_index,
                            const char *key, const char *value)
{
    if (!sch || !key || !value) return -1;
    if (symbol_index >= dc_array_length(sch->symbols)) return -1;

    DC_SchSymbol *sym = dc_array_get(sch->symbols, symbol_index);
    if (!sym->properties) return -1;

    /* Update existing property */
    for (size_t i = 0; i < dc_array_length(sym->properties); i++) {
        DC_SchProperty *prop = dc_array_get(sym->properties, i);
        if (strcmp(prop->key, key) == 0) {
            free(prop->value);
            prop->value = strdup(value);
            return 0;
        }
    }

    /* Add new property */
    DC_SchProperty prop = {
        .key = strdup(key),
        .value = strdup(value),
        .x = sym->x, .y = sym->y,
        .visible = true,
    };
    return dc_array_push(sym->properties, &prop);
}

int
dc_eschematic_remove_symbol(DC_ESchematic *sch, size_t index)
{
    if (!sch || index >= dc_array_length(sch->symbols)) return -1;
    DC_SchSymbol *sym = dc_array_get(sch->symbols, index);
    symbol_cleanup(sym);
    return dc_array_remove(sch->symbols, index);
}

int
dc_eschematic_remove_wire(DC_ESchematic *sch, size_t index)
{
    if (!sch || index >= dc_array_length(sch->wires)) return -1;
    DC_SchWire *w = dc_array_get(sch->wires, index);
    wire_cleanup(w);
    return dc_array_remove(sch->wires, index);
}

int
dc_eschematic_remove_label(DC_ESchematic *sch, size_t index)
{
    if (!sch || index >= dc_array_length(sch->labels)) return -1;
    DC_SchLabel *l = dc_array_get(sch->labels, index);
    label_cleanup(l);
    return dc_array_remove(sch->labels, index);
}

int
dc_eschematic_remove_junction(DC_ESchematic *sch, size_t index)
{
    if (!sch || index >= dc_array_length(sch->junctions)) return -1;
    DC_SchJunction *j = dc_array_get(sch->junctions, index);
    junction_cleanup(j);
    return dc_array_remove(sch->junctions, index);
}

int
dc_eschematic_remove_power_port(DC_ESchematic *sch, size_t index)
{
    if (!sch || index >= dc_array_length(sch->power_ports)) return -1;
    DC_SchPowerPort *pp = dc_array_get(sch->power_ports, index);
    power_port_cleanup(pp);
    return dc_array_remove(sch->power_ports, index);
}

/* =========================================================================
 * Netlist generation
 *
 * Simple connectivity algorithm:
 * 1. Build a list of all pin endpoints (symbol pins at absolute positions)
 * 2. Build a list of all wire endpoints
 * 3. Build a list of all labels at positions
 * 4. Connect points that share the same coordinates
 * 5. Group connected components into nets
 * ========================================================================= */

/* Point with an associated net id for union-find */
typedef struct {
    double x, y;
    int    net_id;
    char  *comp_ref;    /* NULL for wire endpoints */
    char  *pin_num;     /* NULL for wire endpoints */
    char  *label_name;  /* NULL for non-label points */
} ConnPoint;

static int
points_equal(double x1, double y1, double x2, double y2)
{
    return fabs(x1 - x2) < 0.01 && fabs(y1 - y2) < 0.01;
}

/* Simple union-find on net_id */
static int
find_root(int *parent, int i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

static void
union_sets(int *parent, int a, int b)
{
    int ra = find_root(parent, a);
    int rb = find_root(parent, b);
    if (ra != rb) parent[ra] = rb;
}

DC_Netlist *
dc_eschematic_generate_netlist(const DC_ESchematic *sch, DC_Error *err)
{
    if (!sch) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL schematic");
        return NULL;
    }

    /* Collect all connection points */
    DC_Array *points = dc_array_new(sizeof(ConnPoint));
    if (!points) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "points alloc");
        return NULL;
    }

    /* Symbol pins — for now use symbol position as single pin point.
     * A full implementation would resolve pin positions from library data. */
    for (size_t i = 0; i < dc_array_length(sch->symbols); i++) {
        DC_SchSymbol *sym = dc_array_get(sch->symbols, i);
        /* If we have resolved pins, use them */
        if (sym->pins && dc_array_length(sym->pins) > 0) {
            for (size_t j = 0; j < dc_array_length(sym->pins); j++) {
                DC_SchPin *pin = dc_array_get(sym->pins, j);
                ConnPoint cp = {
                    .x = pin->x, .y = pin->y,
                    .comp_ref = sym->reference,
                    .pin_num = pin->number,
                };
                dc_array_push(points, &cp);
            }
        }
    }

    /* Wire endpoints */
    for (size_t i = 0; i < dc_array_length(sch->wires); i++) {
        DC_SchWire *w = dc_array_get(sch->wires, i);
        ConnPoint cp1 = { .x = w->x1, .y = w->y1 };
        ConnPoint cp2 = { .x = w->x2, .y = w->y2 };
        dc_array_push(points, &cp1);
        dc_array_push(points, &cp2);
    }

    /* Labels */
    for (size_t i = 0; i < dc_array_length(sch->labels); i++) {
        DC_SchLabel *l = dc_array_get(sch->labels, i);
        ConnPoint cp = { .x = l->x, .y = l->y, .label_name = l->name };
        dc_array_push(points, &cp);
    }

    /* Power ports */
    for (size_t i = 0; i < dc_array_length(sch->power_ports); i++) {
        DC_SchPowerPort *pp = dc_array_get(sch->power_ports, i);
        ConnPoint cp = { .x = pp->x, .y = pp->y, .label_name = pp->name };
        dc_array_push(points, &cp);
    }

    size_t n = dc_array_length(points);

    /* Initialize union-find */
    int *parent = malloc(n * sizeof(int));
    if (!parent) {
        dc_array_free(points);
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "union-find alloc");
        return NULL;
    }
    for (size_t i = 0; i < n; i++) parent[i] = (int)i;

    /* Merge points at same coordinates */
    for (size_t i = 0; i < n; i++) {
        ConnPoint *a = dc_array_get(points, i);
        for (size_t j = i + 1; j < n; j++) {
            ConnPoint *b = dc_array_get(points, j);
            if (points_equal(a->x, a->y, b->x, b->y))
                union_sets(parent, (int)i, (int)j);
        }
    }

    /* Merge labels with same name */
    for (size_t i = 0; i < n; i++) {
        ConnPoint *a = dc_array_get(points, i);
        if (!a->label_name) continue;
        for (size_t j = i + 1; j < n; j++) {
            ConnPoint *b = dc_array_get(points, j);
            if (!b->label_name) continue;
            if (strcmp(a->label_name, b->label_name) == 0)
                union_sets(parent, (int)i, (int)j);
        }
    }

    /* Build netlist from connected components */
    DC_Netlist *nl = dc_netlist_new();
    if (!nl) {
        free(parent);
        dc_array_free(points);
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "netlist alloc");
        return NULL;
    }

    /* Map root id → net index */
    int *root_to_net = malloc(n * sizeof(int));
    if (!root_to_net) {
        free(parent);
        dc_array_free(points);
        dc_netlist_free(nl);
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "root map alloc");
        return NULL;
    }
    for (size_t i = 0; i < n; i++) root_to_net[i] = -1;

    for (size_t i = 0; i < n; i++) {
        ConnPoint *cp = dc_array_get(points, i);
        if (!cp->comp_ref) continue;  /* only care about pin points */

        int root = find_root(parent, (int)i);
        if (root_to_net[root] < 0) {
            /* Find a name for this net */
            const char *net_name = NULL;
            for (size_t j = 0; j < n; j++) {
                if (find_root(parent, (int)j) == root) {
                    ConnPoint *cp2 = dc_array_get(points, j);
                    if (cp2->label_name) { net_name = cp2->label_name; break; }
                }
            }
            if (!net_name) {
                /* Auto-name: Net-{ref}-{pin} */
                char buf[128];
                snprintf(buf, sizeof(buf), "Net-%s-%s", cp->comp_ref, cp->pin_num);
                dc_netlist_add_net(nl, buf);
            } else {
                dc_netlist_add_net(nl, net_name);
            }
            root_to_net[root] = (int)(dc_netlist_net_count(nl) - 1);
        }

        dc_netlist_add_pin(nl, (size_t)root_to_net[root],
                            cp->comp_ref, cp->pin_num);
    }

    /* Add components */
    for (size_t i = 0; i < dc_array_length(sch->symbols); i++) {
        DC_SchSymbol *sym = dc_array_get(sch->symbols, i);
        const char *fp = dc_eschematic_symbol_property(sym, "Footprint");
        const char *val = dc_eschematic_symbol_property(sym, "Value");
        dc_netlist_add_component(nl, sym->reference, sym->lib_id, fp, val);
    }

    free(root_to_net);
    free(parent);
    dc_array_free(points);
    return nl;
}
