#define _POSIX_C_SOURCE 200809L
/*
 * eda_pcb.c — PCB data model: load, save, manipulate.
 *
 * Parses KiCad 6+ .kicad_pcb s-expression format into a structured model.
 */

#include "eda/eda_pcb.h"
#include "core/string_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Layer name table
 * ========================================================================= */
typedef struct {
    int         id;
    const char *name;
} LayerEntry;

static const LayerEntry s_layers[] = {
    { DC_PCB_LAYER_F_CU,       "F.Cu" },
    { DC_PCB_LAYER_B_CU,       "B.Cu" },
    { DC_PCB_LAYER_F_SILKS,    "F.SilkS" },
    { DC_PCB_LAYER_B_SILKS,    "B.SilkS" },
    { DC_PCB_LAYER_F_MASK,     "F.Mask" },
    { DC_PCB_LAYER_B_MASK,     "B.Mask" },
    { DC_PCB_LAYER_F_PASTE,    "F.Paste" },
    { DC_PCB_LAYER_B_PASTE,    "B.Paste" },
    { DC_PCB_LAYER_F_FAB,      "F.Fab" },
    { DC_PCB_LAYER_B_FAB,      "B.Fab" },
    { DC_PCB_LAYER_EDGE_CUTS,  "Edge.Cuts" },
    { DC_PCB_LAYER_DWGS_USER,  "Dwgs.User" },
    { DC_PCB_LAYER_CMTS_USER,  "Cmts.User" },
    { -1, NULL }
};

int
dc_pcb_layer_from_name(const char *name)
{
    if (!name) return -1;
    for (int i = 0; s_layers[i].name; i++) {
        if (strcmp(s_layers[i].name, name) == 0)
            return s_layers[i].id;
    }
    /* Inner copper layers: In1.Cu=1, In2.Cu=2, etc. */
    if (strncmp(name, "In", 2) == 0) {
        int n = atoi(name + 2);
        if (n >= 1 && n <= 30) return n;
    }
    return -1;
}

const char *
dc_pcb_layer_to_name(int layer_id)
{
    for (int i = 0; s_layers[i].name; i++) {
        if (s_layers[i].id == layer_id)
            return s_layers[i].name;
    }
    return "Unknown";
}

/* =========================================================================
 * Internal structure
 * ========================================================================= */
struct DC_EPcb {
    DC_Array        *footprints;   /* DC_PcbFootprint */
    DC_Array        *tracks;       /* DC_PcbTrack */
    DC_Array        *vias;         /* DC_PcbVia */
    DC_Array        *zones;        /* DC_PcbZone */
    DC_Array        *nets;         /* DC_PcbNet */
    DC_PcbDesignRules rules;

    DC_Sexpr        *raw_ast;      /* owned, or NULL */
    char            *version;      /* owned */
    char            *uuid;         /* owned */
};

/* ---- Cleanup helpers ---- */

static void
pad_cleanup(DC_PcbPad *pad)
{
    free(pad->number);
    free(pad->net_name);
}

static void
footprint_cleanup(DC_PcbFootprint *fp)
{
    free(fp->lib_id);
    free(fp->reference);
    free(fp->value);
    free(fp->uuid);
    if (fp->pads) {
        for (size_t i = 0; i < dc_array_length(fp->pads); i++)
            pad_cleanup(dc_array_get(fp->pads, i));
        dc_array_free(fp->pads);
    }
}

static void
track_cleanup(DC_PcbTrack *t)
{
    free(t->uuid);
}

static void
via_cleanup(DC_PcbVia *v)
{
    free(v->uuid);
}

static void
zone_cleanup(DC_PcbZone *z)
{
    free(z->net_name);
    free(z->uuid);
    dc_array_free(z->outline);
}

static void
net_cleanup(DC_PcbNet *n)
{
    free(n->name);
}

/* ---- UUID generation ---- */
static int s_pcb_uuid_counter = 1000;

static char *
generate_pcb_uuid(void)
{
    char buf[64];
    unsigned a = (unsigned)s_pcb_uuid_counter++;
    snprintf(buf, sizeof(buf), "dcad-pcb-%08x-0000-0000-%012x", a, a + 1);
    return strdup(buf);
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

DC_EPcb *
dc_epcb_new(void)
{
    DC_EPcb *pcb = calloc(1, sizeof(DC_EPcb));
    if (!pcb) return NULL;

    pcb->footprints = dc_array_new(sizeof(DC_PcbFootprint));
    pcb->tracks     = dc_array_new(sizeof(DC_PcbTrack));
    pcb->vias       = dc_array_new(sizeof(DC_PcbVia));
    pcb->zones      = dc_array_new(sizeof(DC_PcbZone));
    pcb->nets       = dc_array_new(sizeof(DC_PcbNet));

    if (!pcb->footprints || !pcb->tracks || !pcb->vias ||
        !pcb->zones || !pcb->nets) {
        dc_epcb_free(pcb);
        return NULL;
    }

    /* Default design rules */
    pcb->rules.clearance       = 0.2;
    pcb->rules.track_width     = 0.25;
    pcb->rules.via_size        = 0.8;
    pcb->rules.via_drill       = 0.4;
    pcb->rules.min_track_width = 0.15;
    pcb->rules.edge_clearance  = 0.5;

    /* Net 0 is always "unconnected" */
    DC_PcbNet net0 = { .id = 0, .name = strdup("") };
    dc_array_push(pcb->nets, &net0);

    return pcb;
}

void
dc_epcb_free(DC_EPcb *pcb)
{
    if (!pcb) return;
    if (pcb->footprints) {
        for (size_t i = 0; i < dc_array_length(pcb->footprints); i++)
            footprint_cleanup(dc_array_get(pcb->footprints, i));
        dc_array_free(pcb->footprints);
    }
    if (pcb->tracks) {
        for (size_t i = 0; i < dc_array_length(pcb->tracks); i++)
            track_cleanup(dc_array_get(pcb->tracks, i));
        dc_array_free(pcb->tracks);
    }
    if (pcb->vias) {
        for (size_t i = 0; i < dc_array_length(pcb->vias); i++)
            via_cleanup(dc_array_get(pcb->vias, i));
        dc_array_free(pcb->vias);
    }
    if (pcb->zones) {
        for (size_t i = 0; i < dc_array_length(pcb->zones); i++)
            zone_cleanup(dc_array_get(pcb->zones, i));
        dc_array_free(pcb->zones);
    }
    if (pcb->nets) {
        for (size_t i = 0; i < dc_array_length(pcb->nets); i++)
            net_cleanup(dc_array_get(pcb->nets, i));
        dc_array_free(pcb->nets);
    }
    dc_sexpr_free(pcb->raw_ast);
    free(pcb->version);
    free(pcb->uuid);
    free(pcb);
}

/* =========================================================================
 * S-expression loading helpers
 * ========================================================================= */

static double
parse_double(const char *s)
{
    return s ? strtod(s, NULL) : 0.0;
}

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

static char *
parse_uuid(const DC_Sexpr *node)
{
    DC_Sexpr *uuid_node = dc_sexpr_find(node, "uuid");
    if (!uuid_node) return generate_pcb_uuid();
    const char *val = dc_sexpr_value(uuid_node);
    return val ? strdup(val) : generate_pcb_uuid();
}

/* Parse pad type from string */
static DC_PadType
parse_pad_type(const char *s)
{
    if (!s) return DC_PAD_SMD;
    if (strcmp(s, "smd") == 0) return DC_PAD_SMD;
    if (strcmp(s, "thru_hole") == 0) return DC_PAD_THRU_HOLE;
    if (strcmp(s, "connect") == 0) return DC_PAD_CONNECT;
    if (strcmp(s, "np_thru_hole") == 0) return DC_PAD_NP_THRU_HOLE;
    return DC_PAD_SMD;
}

/* Parse pad shape from string */
static DC_PadShape
parse_pad_shape(const char *s)
{
    if (!s) return DC_PAD_SHAPE_CIRCLE;
    if (strcmp(s, "circle") == 0) return DC_PAD_SHAPE_CIRCLE;
    if (strcmp(s, "rect") == 0) return DC_PAD_SHAPE_RECT;
    if (strcmp(s, "oval") == 0) return DC_PAD_SHAPE_OVAL;
    if (strcmp(s, "roundrect") == 0) return DC_PAD_SHAPE_ROUNDRECT;
    if (strcmp(s, "custom") == 0) return DC_PAD_SHAPE_CUSTOM;
    return DC_PAD_SHAPE_CIRCLE;
}

/* Parse a (pad ...) node */
static DC_PcbPad
parse_pad(const DC_Sexpr *pad_node)
{
    DC_PcbPad pad = {0};

    /* (pad "1" smd rect (at 0 0) (size 1.0 1.0) (layers "F.Cu" "F.Paste" "F.Mask") (net 1 "VCC")) */
    pad.number = strdup(dc_sexpr_value_at(pad_node, 0) ?
                         dc_sexpr_value_at(pad_node, 0) : "");
    pad.type = parse_pad_type(dc_sexpr_value_at(pad_node, 1));
    pad.shape = parse_pad_shape(dc_sexpr_value_at(pad_node, 2));

    parse_at(pad_node, &pad.x, &pad.y, NULL);

    DC_Sexpr *size_node = dc_sexpr_find(pad_node, "size");
    if (size_node) {
        pad.size_x = parse_double(dc_sexpr_value_at(size_node, 0));
        pad.size_y = parse_double(dc_sexpr_value_at(size_node, 1));
    }

    DC_Sexpr *drill_node = dc_sexpr_find(pad_node, "drill");
    if (drill_node) {
        pad.drill = parse_double(dc_sexpr_value(drill_node));
    }

    DC_Sexpr *layers = dc_sexpr_find(pad_node, "layers");
    if (layers && dc_sexpr_child_count(layers) > 1) {
        const char *layer_name = dc_sexpr_value(layers);
        pad.layer = dc_pcb_layer_from_name(layer_name);
    }

    DC_Sexpr *net_node = dc_sexpr_find(pad_node, "net");
    if (net_node) {
        pad.net_id = (int)parse_double(dc_sexpr_value(net_node));
        const char *nname = dc_sexpr_value_at(net_node, 1);
        pad.net_name = nname ? strdup(nname) : NULL;
    }

    return pad;
}

/* Parse a (footprint ...) node */
static DC_PcbFootprint
parse_footprint(const DC_Sexpr *fp_node)
{
    DC_PcbFootprint fp = {0};
    fp.pads = dc_array_new(sizeof(DC_PcbPad));

    /* (footprint "Resistor_SMD:R_0402..." (at x y angle) (layer "F.Cu") ...) */
    const char *lid = dc_sexpr_value(fp_node);
    fp.lib_id = lid ? strdup(lid) : strdup("");

    parse_at(fp_node, &fp.x, &fp.y, &fp.angle);

    DC_Sexpr *layer = dc_sexpr_find(fp_node, "layer");
    if (layer) {
        const char *lname = dc_sexpr_value(layer);
        fp.layer = dc_pcb_layer_from_name(lname);
    }

    fp.uuid = parse_uuid(fp_node);

    /* Reference and value from properties */
    size_t prop_count = 0;
    DC_Sexpr **props = dc_sexpr_find_all(fp_node, "property", &prop_count);
    if (props) {
        for (size_t i = 0; i < prop_count; i++) {
            const char *key = dc_sexpr_value_at(props[i], 0);
            const char *val = dc_sexpr_value_at(props[i], 1);
            if (key && val) {
                if (strcmp(key, "Reference") == 0) fp.reference = strdup(val);
                else if (strcmp(key, "Value") == 0) fp.value = strdup(val);
            }
        }
        free(props);
    }

    /* Also check fp_text for older format */
    size_t text_count = 0;
    DC_Sexpr **texts = dc_sexpr_find_all(fp_node, "fp_text", &text_count);
    if (texts) {
        for (size_t i = 0; i < text_count; i++) {
            const char *type = dc_sexpr_value_at(texts[i], 0);
            const char *val = dc_sexpr_value_at(texts[i], 1);
            if (type && val) {
                if (strcmp(type, "reference") == 0 && !fp.reference)
                    fp.reference = strdup(val);
                else if (strcmp(type, "value") == 0 && !fp.value)
                    fp.value = strdup(val);
            }
        }
        free(texts);
    }

    if (!fp.reference) fp.reference = strdup("");
    if (!fp.value) fp.value = strdup("");

    /* Pads */
    size_t pad_count = 0;
    DC_Sexpr **pads = dc_sexpr_find_all(fp_node, "pad", &pad_count);
    if (pads) {
        for (size_t i = 0; i < pad_count; i++) {
            DC_PcbPad pad = parse_pad(pads[i]);
            dc_array_push(fp.pads, &pad);
        }
        free(pads);
    }

    return fp;
}

/* Parse a (segment ...) track node */
static DC_PcbTrack
parse_track(const DC_Sexpr *track_node)
{
    DC_PcbTrack t = {0};

    DC_Sexpr *start = dc_sexpr_find(track_node, "start");
    if (start) {
        t.x1 = parse_double(dc_sexpr_value_at(start, 0));
        t.y1 = parse_double(dc_sexpr_value_at(start, 1));
    }

    DC_Sexpr *end = dc_sexpr_find(track_node, "end");
    if (end) {
        t.x2 = parse_double(dc_sexpr_value_at(end, 0));
        t.y2 = parse_double(dc_sexpr_value_at(end, 1));
    }

    DC_Sexpr *width = dc_sexpr_find(track_node, "width");
    if (width) t.width = parse_double(dc_sexpr_value(width));

    DC_Sexpr *layer = dc_sexpr_find(track_node, "layer");
    if (layer) t.layer = dc_pcb_layer_from_name(dc_sexpr_value(layer));

    DC_Sexpr *net = dc_sexpr_find(track_node, "net");
    if (net) t.net_id = (int)parse_double(dc_sexpr_value(net));

    t.uuid = parse_uuid(track_node);
    return t;
}

/* Parse a (via ...) node */
static DC_PcbVia
parse_via(const DC_Sexpr *via_node)
{
    DC_PcbVia v = {0};

    parse_at(via_node, &v.x, &v.y, NULL);

    DC_Sexpr *size = dc_sexpr_find(via_node, "size");
    if (size) v.size = parse_double(dc_sexpr_value(size));

    DC_Sexpr *drill = dc_sexpr_find(via_node, "drill");
    if (drill) v.drill = parse_double(dc_sexpr_value(drill));

    DC_Sexpr *net = dc_sexpr_find(via_node, "net");
    if (net) v.net_id = (int)parse_double(dc_sexpr_value(net));

    DC_Sexpr *layers = dc_sexpr_find(via_node, "layers");
    if (layers) {
        const char *l1 = dc_sexpr_value_at(layers, 0);
        const char *l2 = dc_sexpr_value_at(layers, 1);
        v.layer_start = dc_pcb_layer_from_name(l1);
        v.layer_end   = dc_pcb_layer_from_name(l2);
    }

    v.uuid = parse_uuid(via_node);
    return v;
}

/* =========================================================================
 * I/O
 * ========================================================================= */

DC_EPcb *
dc_epcb_from_sexpr(DC_Sexpr *ast, DC_Error *err)
{
    if (!ast) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL AST");
        return NULL;
    }

    const char *tag = dc_sexpr_tag(ast);
    if (!tag || strcmp(tag, "kicad_pcb") != 0) {
        if (err) DC_SET_ERROR(err, DC_ERROR_PARSE, "expected (kicad_pcb ...), got (%s ...)",
                               tag ? tag : "NULL");
        dc_sexpr_free(ast);
        return NULL;
    }

    DC_EPcb *pcb = dc_epcb_new();
    if (!pcb) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "pcb alloc");
        dc_sexpr_free(ast);
        return NULL;
    }

    /* Version */
    DC_Sexpr *ver = dc_sexpr_find(ast, "version");
    if (ver) {
        free(pcb->version);
        pcb->version = strdup(dc_sexpr_value(ver));
    }

    /* UUID */
    free(pcb->uuid);
    pcb->uuid = parse_uuid(ast);

    /* Nets */
    size_t net_count = 0;
    DC_Sexpr **net_nodes = dc_sexpr_find_all(ast, "net", &net_count);
    if (net_nodes) {
        for (size_t i = 0; i < net_count; i++) {
            const char *id_str = dc_sexpr_value(net_nodes[i]);
            const char *name = dc_sexpr_value_at(net_nodes[i], 1);
            if (id_str && name) {
                int net_id = (int)strtol(id_str, NULL, 10);
                /* Skip net 0 (already added) */
                if (net_id > 0) {
                    DC_PcbNet net = { .id = net_id, .name = strdup(name) };
                    dc_array_push(pcb->nets, &net);
                }
            }
        }
        free(net_nodes);
    }

    /* Footprints */
    size_t fp_count = 0;
    DC_Sexpr **fps = dc_sexpr_find_all(ast, "footprint", &fp_count);
    if (fps) {
        for (size_t i = 0; i < fp_count; i++) {
            DC_PcbFootprint fp = parse_footprint(fps[i]);
            dc_array_push(pcb->footprints, &fp);
        }
        free(fps);
    }

    /* Tracks (segments) */
    size_t seg_count = 0;
    DC_Sexpr **segs = dc_sexpr_find_all(ast, "segment", &seg_count);
    if (segs) {
        for (size_t i = 0; i < seg_count; i++) {
            DC_PcbTrack t = parse_track(segs[i]);
            dc_array_push(pcb->tracks, &t);
        }
        free(segs);
    }

    /* Vias */
    size_t via_count = 0;
    DC_Sexpr **via_nodes = dc_sexpr_find_all(ast, "via", &via_count);
    if (via_nodes) {
        for (size_t i = 0; i < via_count; i++) {
            DC_PcbVia v = parse_via(via_nodes[i]);
            dc_array_push(pcb->vias, &v);
        }
        free(via_nodes);
    }

    /* Design rules from (setup ...) */
    DC_Sexpr *setup = dc_sexpr_find(ast, "setup");
    if (setup) {
        DC_Sexpr *defaults = dc_sexpr_find(setup, "pad_to_mask_clearance");
        (void)defaults;
        /* Extract track width and clearance from setup if present */
    }

    pcb->raw_ast = ast;
    return pcb;
}

DC_EPcb *
dc_epcb_load(const char *path, DC_Error *err)
{
    if (!path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL path");
        return NULL;
    }

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

    DC_Sexpr *ast = dc_sexpr_parse(text, err);
    free(text);
    if (!ast) return NULL;

    return dc_epcb_from_sexpr(ast, err);
}

int
dc_epcb_save(const DC_EPcb *pcb, const char *path, DC_Error *err)
{
    if (!pcb || !path) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL arg");
        return -1;
    }

    char *text = dc_epcb_to_sexpr_string(pcb, err);
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
dc_epcb_to_sexpr_string(const DC_EPcb *pcb, DC_Error *err)
{
    if (!pcb) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL pcb");
        return NULL;
    }

    if (pcb->raw_ast) {
        return dc_sexpr_write(pcb->raw_ast, err);
    }

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "sb alloc");
        return NULL;
    }

    dc_sb_append(sb, "(kicad_pcb\n");
    dc_sb_appendf(sb, "  (version %s)\n", pcb->version ? pcb->version : "20221018");
    dc_sb_append(sb, "  (generator \"duncad\")\n");

    /* Nets */
    for (size_t i = 0; i < dc_array_length(pcb->nets); i++) {
        DC_PcbNet *net = dc_array_get(pcb->nets, i);
        dc_sb_appendf(sb, "  (net %d \"%s\")\n", net->id, net->name);
    }

    /* Footprints */
    for (size_t i = 0; i < dc_array_length(pcb->footprints); i++) {
        DC_PcbFootprint *fp = dc_array_get(pcb->footprints, i);
        dc_sb_appendf(sb, "  (footprint \"%s\"\n", fp->lib_id);
        dc_sb_appendf(sb, "    (layer \"%s\")\n", dc_pcb_layer_to_name(fp->layer));
        dc_sb_appendf(sb, "    (at %.4f %.4f %.1f)\n", fp->x, fp->y, fp->angle);
        dc_sb_appendf(sb, "    (uuid \"%s\")\n", fp->uuid);

        if (fp->reference)
            dc_sb_appendf(sb, "    (property \"Reference\" \"%s\")\n", fp->reference);
        if (fp->value)
            dc_sb_appendf(sb, "    (property \"Value\" \"%s\")\n", fp->value);

        /* Pads */
        if (fp->pads) {
            for (size_t j = 0; j < dc_array_length(fp->pads); j++) {
                DC_PcbPad *pad = dc_array_get(fp->pads, j);
                dc_sb_appendf(sb, "    (pad \"%s\" smd rect (at %.4f %.4f) (size %.4f %.4f))\n",
                               pad->number, pad->x, pad->y, pad->size_x, pad->size_y);
            }
        }
        dc_sb_append(sb, "  )\n");
    }

    /* Tracks */
    for (size_t i = 0; i < dc_array_length(pcb->tracks); i++) {
        DC_PcbTrack *t = dc_array_get(pcb->tracks, i);
        dc_sb_appendf(sb, "  (segment (start %.4f %.4f) (end %.4f %.4f) (width %.4f) (layer \"%s\") (net %d))\n",
                       t->x1, t->y1, t->x2, t->y2, t->width,
                       dc_pcb_layer_to_name(t->layer), t->net_id);
    }

    /* Vias */
    for (size_t i = 0; i < dc_array_length(pcb->vias); i++) {
        DC_PcbVia *v = dc_array_get(pcb->vias, i);
        dc_sb_appendf(sb, "  (via (at %.4f %.4f) (size %.4f) (drill %.4f) (layers \"%s\" \"%s\") (net %d))\n",
                       v->x, v->y, v->size, v->drill,
                       dc_pcb_layer_to_name(v->layer_start),
                       dc_pcb_layer_to_name(v->layer_end),
                       v->net_id);
    }

    dc_sb_append(sb, ")\n");

    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}

/* =========================================================================
 * Queries
 * ========================================================================= */

size_t dc_epcb_footprint_count(const DC_EPcb *pcb) {
    return pcb ? dc_array_length(pcb->footprints) : 0;
}
size_t dc_epcb_track_count(const DC_EPcb *pcb) {
    return pcb ? dc_array_length(pcb->tracks) : 0;
}
size_t dc_epcb_via_count(const DC_EPcb *pcb) {
    return pcb ? dc_array_length(pcb->vias) : 0;
}
size_t dc_epcb_zone_count(const DC_EPcb *pcb) {
    return pcb ? dc_array_length(pcb->zones) : 0;
}
size_t dc_epcb_net_count(const DC_EPcb *pcb) {
    return pcb ? dc_array_length(pcb->nets) : 0;
}

DC_PcbFootprint *dc_epcb_get_footprint(const DC_EPcb *pcb, size_t i) {
    return pcb ? dc_array_get(pcb->footprints, i) : NULL;
}
DC_PcbTrack *dc_epcb_get_track(const DC_EPcb *pcb, size_t i) {
    return pcb ? dc_array_get(pcb->tracks, i) : NULL;
}
DC_PcbVia *dc_epcb_get_via(const DC_EPcb *pcb, size_t i) {
    return pcb ? dc_array_get(pcb->vias, i) : NULL;
}
DC_PcbZone *dc_epcb_get_zone(const DC_EPcb *pcb, size_t i) {
    return pcb ? dc_array_get(pcb->zones, i) : NULL;
}
DC_PcbNet *dc_epcb_get_net(const DC_EPcb *pcb, size_t i) {
    return pcb ? dc_array_get(pcb->nets, i) : NULL;
}

DC_PcbDesignRules *
dc_epcb_get_design_rules(DC_EPcb *pcb)
{
    return pcb ? &pcb->rules : NULL;
}

DC_PcbFootprint *
dc_epcb_find_footprint(const DC_EPcb *pcb, const char *ref)
{
    if (!pcb || !ref) return NULL;
    for (size_t i = 0; i < dc_array_length(pcb->footprints); i++) {
        DC_PcbFootprint *fp = dc_array_get(pcb->footprints, i);
        if (fp->reference && strcmp(fp->reference, ref) == 0)
            return fp;
    }
    return NULL;
}

int
dc_epcb_find_net(const DC_EPcb *pcb, const char *name)
{
    if (!pcb || !name) return -1;
    for (size_t i = 0; i < dc_array_length(pcb->nets); i++) {
        DC_PcbNet *net = dc_array_get(pcb->nets, i);
        if (net->name && strcmp(net->name, name) == 0) return net->id;
    }
    return -1;
}

/* =========================================================================
 * Mutation
 * ========================================================================= */

size_t
dc_epcb_add_footprint(DC_EPcb *pcb, const char *lib_id,
                         const char *reference, double x, double y, int layer)
{
    if (!pcb || !lib_id || !reference) return (size_t)-1;

    DC_PcbFootprint fp = {0};
    fp.lib_id    = strdup(lib_id);
    fp.reference = strdup(reference);
    fp.value     = strdup("");
    fp.x = x;
    fp.y = y;
    fp.layer = layer;
    fp.uuid = generate_pcb_uuid();
    fp.pads = dc_array_new(sizeof(DC_PcbPad));

    if (!fp.lib_id || !fp.reference || !fp.uuid || !fp.pads) {
        footprint_cleanup(&fp);
        return (size_t)-1;
    }

    size_t idx = dc_array_length(pcb->footprints);
    if (dc_array_push(pcb->footprints, &fp) != 0) return (size_t)-1;
    return idx;
}

size_t
dc_epcb_add_track(DC_EPcb *pcb, double x1, double y1,
                     double x2, double y2, double width, int layer, int net_id)
{
    if (!pcb) return (size_t)-1;
    DC_PcbTrack t = {
        .x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2,
        .width = width, .layer = layer, .net_id = net_id,
        .uuid = generate_pcb_uuid(),
    };
    if (!t.uuid) return (size_t)-1;
    size_t idx = dc_array_length(pcb->tracks);
    if (dc_array_push(pcb->tracks, &t) != 0) return (size_t)-1;
    return idx;
}

size_t
dc_epcb_add_via(DC_EPcb *pcb, double x, double y,
                   double size, double drill, int net_id)
{
    if (!pcb) return (size_t)-1;
    DC_PcbVia v = {
        .x = x, .y = y, .size = size, .drill = drill,
        .net_id = net_id,
        .layer_start = DC_PCB_LAYER_F_CU,
        .layer_end = DC_PCB_LAYER_B_CU,
        .uuid = generate_pcb_uuid(),
    };
    if (!v.uuid) return (size_t)-1;
    size_t idx = dc_array_length(pcb->vias);
    if (dc_array_push(pcb->vias, &v) != 0) return (size_t)-1;
    return idx;
}

int
dc_epcb_add_net(DC_EPcb *pcb, const char *name)
{
    if (!pcb || !name) return -1;
    int next_id = (int)dc_array_length(pcb->nets);
    DC_PcbNet net = { .id = next_id, .name = strdup(name) };
    if (!net.name) return -1;
    if (dc_array_push(pcb->nets, &net) != 0) return -1;
    return next_id;
}

size_t
dc_epcb_add_zone(DC_EPcb *pcb, const char *net_name,
                   int layer, double clearance,
                   double x, double y, double w, double h)
{
    if (!pcb) return (size_t)-1;

    int net_id = 0;
    if (net_name) {
        net_id = dc_epcb_find_net(pcb, net_name);
        if (net_id < 0) net_id = dc_epcb_add_net(pcb, net_name);
    }

    DC_PcbZone zone = {0};
    zone.net_id = net_id;
    zone.net_name = net_name ? strdup(net_name) : NULL;
    zone.layer = layer;
    zone.clearance = clearance;
    zone.uuid = generate_pcb_uuid();
    zone.outline = dc_array_new(sizeof(DC_PcbZoneVertex));

    if (!zone.uuid || !zone.outline) {
        free(zone.net_name);
        free(zone.uuid);
        dc_array_free(zone.outline);
        return (size_t)-1;
    }

    /* Rectangle outline: 4 vertices */
    DC_PcbZoneVertex v0 = { x, y };
    DC_PcbZoneVertex v1 = { x + w, y };
    DC_PcbZoneVertex v2 = { x + w, y + h };
    DC_PcbZoneVertex v3 = { x, y + h };
    dc_array_push(zone.outline, &v0);
    dc_array_push(zone.outline, &v1);
    dc_array_push(zone.outline, &v2);
    dc_array_push(zone.outline, &v3);

    size_t idx = dc_array_length(pcb->zones);
    if (dc_array_push(pcb->zones, &zone) != 0) return (size_t)-1;
    return idx;
}

int
dc_epcb_import_netlist(DC_EPcb *pcb, const DC_Netlist *nl, DC_Error *err)
{
    if (!pcb || !nl) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL arg");
        return -1;
    }

    /* Add nets */
    for (size_t i = 0; i < dc_netlist_net_count(nl); i++) {
        DC_Net *net = dc_netlist_get_net(nl, i);
        if (dc_epcb_find_net(pcb, net->name) < 0) {
            dc_epcb_add_net(pcb, net->name);
        }
    }

    /* Add/update footprints from components */
    for (size_t i = 0; i < dc_array_length(nl->components); i++) {
        DC_NetlistComponent *comp = dc_array_get(nl->components, i);
        DC_PcbFootprint *existing = dc_epcb_find_footprint(pcb, comp->ref);
        if (!existing) {
            /* Create new footprint with default position */
            double x = 100.0 + (double)(i % 10) * 10.0;
            double y = 100.0 + (double)(i / 10) * 10.0;
            dc_epcb_add_footprint(pcb, comp->lib_id ? comp->lib_id : "",
                                   comp->ref, x, y, DC_PCB_LAYER_F_CU);
        }
    }

    return 0;
}
