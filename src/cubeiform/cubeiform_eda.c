#define _POSIX_C_SOURCE 200809L

#include "cubeiform_eda.h"

#include "core/array.h"
#include "core/error.h"
#include "core/log.h"
#include "eda/eda_schematic.h"
#include "eda/eda_pcb.h"
#include "eda/eda_library.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * DC_CubeiformEda — internal structure
 * ========================================================================= */
struct DC_CubeiformEda {
    DC_Array *sch_ops;  /* DC_SchOp elements */
    DC_Array *pcb_ops;  /* DC_PcbOp elements */
    DC_Array *vox_ops;  /* DC_VoxOp elements */
};

/* =========================================================================
 * Tokenizer — minimal, just enough to parse EDA domain blocks
 * ========================================================================= */

typedef enum {
    ETOK_EOF = 0,
    ETOK_IDENT,
    ETOK_STRING,
    ETOK_NUMBER,
    ETOK_LBRACE,
    ETOK_RBRACE,
    ETOK_LPAREN,
    ETOK_RPAREN,
    ETOK_SEMI,
    ETOK_COMMA,
    ETOK_EQ,
    ETOK_DOT,
    ETOK_PIPE,    /* >> */
    ETOK_COLON,
} EToken;

typedef struct {
    EToken type;
    const char *start;
    int len;
    double num_val;
} ETok;

typedef struct {
    const char *src;
    int pos;
    int len;
    ETok cur;
    DC_Error *err;
    int has_error;
} EParser;

/* -------------------------------------------------------------------------
 * Tokenizer helpers
 * ---------------------------------------------------------------------- */
static void skip_ws(EParser *p)
{
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else if (c == '/' && p->pos + 1 < p->len && p->src[p->pos + 1] == '/') {
            /* line comment */
            p->pos += 2;
            while (p->pos < p->len && p->src[p->pos] != '\n') p->pos++;
        } else if (c == '/' && p->pos + 1 < p->len && p->src[p->pos + 1] == '*') {
            /* block comment */
            p->pos += 2;
            while (p->pos + 1 < p->len) {
                if (p->src[p->pos] == '*' && p->src[p->pos + 1] == '/') {
                    p->pos += 2;
                    break;
                }
                p->pos++;
            }
        } else {
            break;
        }
    }
}

static void next_token(EParser *p)
{
    skip_ws(p);

    if (p->pos >= p->len) {
        p->cur = (ETok){ETOK_EOF, p->src + p->pos, 0, 0};
        return;
    }

    char c = p->src[p->pos];

    /* Single-char tokens */
    switch (c) {
        case '{': p->cur = (ETok){ETOK_LBRACE, p->src + p->pos, 1, 0}; p->pos++; return;
        case '}': p->cur = (ETok){ETOK_RBRACE, p->src + p->pos, 1, 0}; p->pos++; return;
        case '(': p->cur = (ETok){ETOK_LPAREN, p->src + p->pos, 1, 0}; p->pos++; return;
        case ')': p->cur = (ETok){ETOK_RPAREN, p->src + p->pos, 1, 0}; p->pos++; return;
        case ';': p->cur = (ETok){ETOK_SEMI, p->src + p->pos, 1, 0}; p->pos++; return;
        case ',': p->cur = (ETok){ETOK_COMMA, p->src + p->pos, 1, 0}; p->pos++; return;
        case '=': p->cur = (ETok){ETOK_EQ, p->src + p->pos, 1, 0}; p->pos++; return;
        case '.': p->cur = (ETok){ETOK_DOT, p->src + p->pos, 1, 0}; p->pos++; return;
        case ':': p->cur = (ETok){ETOK_COLON, p->src + p->pos, 1, 0}; p->pos++; return;
        default: break;
    }

    /* >> pipe operator */
    if (c == '>' && p->pos + 1 < p->len && p->src[p->pos + 1] == '>') {
        p->cur = (ETok){ETOK_PIPE, p->src + p->pos, 2, 0};
        p->pos += 2;
        return;
    }

    /* String literal */
    if (c == '"') {
        int start = p->pos;
        p->pos++; /* skip opening " */
        while (p->pos < p->len && p->src[p->pos] != '"') {
            if (p->src[p->pos] == '\\') p->pos++; /* skip escape */
            p->pos++;
        }
        if (p->pos < p->len) p->pos++; /* skip closing " */
        p->cur = (ETok){ETOK_STRING, p->src + start + 1, p->pos - start - 2, 0};
        return;
    }

    /* Number (possibly negative) */
    if (isdigit((unsigned char)c) || (c == '-' && p->pos + 1 < p->len &&
        isdigit((unsigned char)p->src[p->pos + 1]))) {
        const char *start = p->src + p->pos;
        char *end = NULL;
        double val = strtod(start, &end);
        int nlen = (int)(end - start);
        p->cur = (ETok){ETOK_NUMBER, start, nlen, val};
        p->pos += nlen;
        return;
    }

    /* Identifier (letters, digits, underscore, dot for layer names like F.Cu) */
    if (isalpha((unsigned char)c) || c == '_') {
        int start = p->pos;
        while (p->pos < p->len) {
            char ch = p->src[p->pos];
            if (isalnum((unsigned char)ch) || ch == '_') {
                p->pos++;
            } else if (ch == '.' && p->pos + 1 < p->len &&
                       isalpha((unsigned char)p->src[p->pos + 1])) {
                /* Allow dots in identifiers for layer names like F.Cu, B.SilkS */
                p->pos++;
            } else {
                break;
            }
        }
        p->cur = (ETok){ETOK_IDENT, p->src + start, p->pos - start, 0};
        return;
    }

    /* Unknown char — skip */
    p->cur = (ETok){ETOK_IDENT, p->src + p->pos, 1, 0};
    p->pos++;
}

/* -------------------------------------------------------------------------
 * Parser helpers
 * ---------------------------------------------------------------------- */
static int ident_eq(const ETok *t, const char *s)
{
    int slen = (int)strlen(s);
    return t->type == ETOK_IDENT && t->len == slen &&
           strncmp(t->start, s, (size_t)slen) == 0;
}

static char *tok_strdup(const ETok *t)
{
    if (!t->start || t->len <= 0) return strdup("");
    char *s = malloc((size_t)t->len + 1);
    if (!s) return NULL;
    memcpy(s, t->start, (size_t)t->len);
    s[t->len] = '\0';
    return s;
}

static void eat(EParser *p, EToken type)
{
    if (p->cur.type == type) next_token(p);
}

static void expect(EParser *p, EToken type)
{
    if (p->cur.type != type) {
        if (!p->has_error) {
            DC_SET_ERROR(p->err, DC_ERROR_EDA_PARSE,
                         "expected token %d, got %d at pos %d",
                         (int)type, (int)p->cur.type, p->pos);
            p->has_error = 1;
        }
        return;
    }
    next_token(p);
}

static double eat_number(EParser *p)
{
    double v = p->cur.num_val;
    expect(p, ETOK_NUMBER);
    return v;
}

/* -------------------------------------------------------------------------
 * Free helpers
 * ---------------------------------------------------------------------- */
static void sch_op_free_fields(DC_SchOp *op)
{
    free(op->ref);
    free(op->lib_id);
    free(op->name);
    free(op->str_value);
}

static void pcb_op_free_fields(DC_PcbOp *op)
{
    free(op->ref);
    free(op->name);
    free(op->rule_key);
}

/* -------------------------------------------------------------------------
 * Read a pin reference like "R1.2" or "U1.PA0" — consumes ident[.ident/number]*
 * Returns a malloc'd string. Caller must free.
 * ---------------------------------------------------------------------- */
static char *read_pin_ref(EParser *p)
{
    /* Start with the current ident/number token */
    char buf[256];
    int len = 0;

    /* First part: ident */
    if (p->cur.type == ETOK_IDENT) {
        int n = p->cur.len < (int)(sizeof(buf) - 1) ? p->cur.len : (int)(sizeof(buf) - 1);
        memcpy(buf, p->cur.start, (size_t)n);
        len = n;
        next_token(p);
    }

    /* Consume .PIN parts */
    while (p->cur.type == ETOK_DOT && len < (int)(sizeof(buf) - 2)) {
        buf[len++] = '.';
        next_token(p); /* skip dot */

        int n = 0;
        if (p->cur.type == ETOK_IDENT || p->cur.type == ETOK_NUMBER) {
            n = p->cur.len < (int)(sizeof(buf) - len - 1) ? p->cur.len : (int)(sizeof(buf) - len - 1);
            memcpy(buf + len, p->cur.start, (size_t)n);
            len += n;
            next_token(p);
        }
    }

    buf[len] = '\0';
    return strdup(buf);
}

/* =========================================================================
 * Schematic block parser
 *
 * Syntax handled:
 *   component REF = "lib_id" at X, Y;
 *   wire NAME: REF.PIN, REF.PIN;
 *   power NAME at X, Y;
 *   REF >> value("str");
 *   REF >> footprint("str");
 * ========================================================================= */
static void parse_schematic_block(EParser *p, DC_Array *ops)
{
    expect(p, ETOK_LBRACE);

    while (p->cur.type != ETOK_RBRACE && p->cur.type != ETOK_EOF && !p->has_error) {
        if (ident_eq(&p->cur, "component")) {
            /* component REF = "lib_id" at X, Y; */
            next_token(p); /* skip 'component' */

            DC_SchOp op = {0};
            op.type = DC_SCH_OP_ADD_COMPONENT;
            op.ref = tok_strdup(&p->cur);
            next_token(p); /* skip ref */

            expect(p, ETOK_EQ);

            op.lib_id = tok_strdup(&p->cur);
            next_token(p); /* skip lib_id string */

            if (ident_eq(&p->cur, "at")) {
                next_token(p);
                op.x = eat_number(p);
                eat(p, ETOK_COMMA);
                op.y = eat_number(p);
            }

            /* Check for inline pipe operators before semicolon */
            while (p->cur.type == ETOK_PIPE && !p->has_error) {
                next_token(p); /* skip >> */
                if (ident_eq(&p->cur, "value")) {
                    next_token(p);
                    expect(p, ETOK_LPAREN);
                    DC_SchOp vop = {0};
                    vop.type = DC_SCH_OP_SET_VALUE;
                    vop.ref = strdup(op.ref);
                    vop.str_value = tok_strdup(&p->cur);
                    next_token(p);
                    expect(p, ETOK_RPAREN);
                    dc_array_push(ops, &vop);
                } else if (ident_eq(&p->cur, "footprint")) {
                    next_token(p);
                    expect(p, ETOK_LPAREN);
                    DC_SchOp fop = {0};
                    fop.type = DC_SCH_OP_SET_FOOTPRINT;
                    fop.ref = strdup(op.ref);
                    fop.str_value = tok_strdup(&p->cur);
                    next_token(p);
                    expect(p, ETOK_RPAREN);
                    dc_array_push(ops, &fop);
                } else {
                    /* Skip unknown pipe target */
                    next_token(p);
                    if (p->cur.type == ETOK_LPAREN) {
                        next_token(p);
                        while (p->cur.type != ETOK_RPAREN && p->cur.type != ETOK_EOF)
                            next_token(p);
                        eat(p, ETOK_RPAREN);
                    }
                }
            }

            eat(p, ETOK_SEMI);
            dc_array_push(ops, &op);

        } else if (ident_eq(&p->cur, "wire")) {
            /* wire NAME: REF.PIN, REF.PIN; */
            next_token(p); /* skip 'wire' */

            char *net_name = tok_strdup(&p->cur);
            next_token(p);
            expect(p, ETOK_COLON);

            /* Parse pin references — we record as wires connecting consecutive pins.
             * The actual pin resolution happens during apply. For now store
             * the pin references as "REF.PIN" strings in the name/ref fields. */
            char *first_pin = read_pin_ref(p);

            while (p->cur.type == ETOK_COMMA && !p->has_error) {
                next_token(p); /* skip comma */
                char *second_pin = read_pin_ref(p);

                DC_SchOp op = {0};
                op.type = DC_SCH_OP_ADD_WIRE;
                op.name = strdup(net_name);
                op.ref = strdup(first_pin);
                op.str_value = second_pin;  /* reuse str_value for second pin ref */

                dc_array_push(ops, &op);

                free(first_pin);
                first_pin = strdup(second_pin);
            }

            free(first_pin);
            free(net_name);
            eat(p, ETOK_SEMI);

        } else if (ident_eq(&p->cur, "power")) {
            /* power NAME at X, Y; */
            next_token(p);

            DC_SchOp op = {0};
            op.type = DC_SCH_OP_ADD_POWER;
            op.name = tok_strdup(&p->cur);
            next_token(p);

            if (ident_eq(&p->cur, "at")) {
                next_token(p);
                op.x = eat_number(p);
                eat(p, ETOK_COMMA);
                op.y = eat_number(p);
            }

            eat(p, ETOK_SEMI);
            dc_array_push(ops, &op);

        } else if (p->cur.type == ETOK_IDENT) {
            /* REF >> value("str") or REF >> footprint("str") */
            char *ref = tok_strdup(&p->cur);
            next_token(p);

            while (p->cur.type == ETOK_PIPE && !p->has_error) {
                next_token(p); /* skip >> */
                if (ident_eq(&p->cur, "value")) {
                    next_token(p);
                    expect(p, ETOK_LPAREN);
                    DC_SchOp op = {0};
                    op.type = DC_SCH_OP_SET_VALUE;
                    op.ref = strdup(ref);
                    op.str_value = tok_strdup(&p->cur);
                    next_token(p);
                    expect(p, ETOK_RPAREN);
                    dc_array_push(ops, &op);
                } else if (ident_eq(&p->cur, "footprint")) {
                    next_token(p);
                    expect(p, ETOK_LPAREN);
                    DC_SchOp op = {0};
                    op.type = DC_SCH_OP_SET_FOOTPRINT;
                    op.ref = strdup(ref);
                    op.str_value = tok_strdup(&p->cur);
                    next_token(p);
                    expect(p, ETOK_RPAREN);
                    dc_array_push(ops, &op);
                } else {
                    /* dnp() or other — skip */
                    next_token(p);
                    if (p->cur.type == ETOK_LPAREN) {
                        next_token(p);
                        while (p->cur.type != ETOK_RPAREN && p->cur.type != ETOK_EOF)
                            next_token(p);
                        eat(p, ETOK_RPAREN);
                    }
                }
            }

            eat(p, ETOK_SEMI);
            free(ref);

        } else {
            /* Skip unknown token */
            next_token(p);
        }
    }

    expect(p, ETOK_RBRACE);
}

/* =========================================================================
 * PCB block parser
 *
 * Syntax handled:
 *   outline { rect(W, H); }
 *   rules { clearance = V; track_width = V; via_size = V; via_drill = V; }
 *   place REF at X, Y on LAYER;
 *   place REF at X, Y on LAYER >> rotate(ANGLE);
 *   route NAME layer LAYER width W { from REF.PIN; to X, Y; to REF.PIN; }
 *   zone NAME layer LAYER { rect(X, Y, W, H); }
 * ========================================================================= */
static void parse_pcb_block(EParser *p, DC_Array *ops)
{
    expect(p, ETOK_LBRACE);

    while (p->cur.type != ETOK_RBRACE && p->cur.type != ETOK_EOF && !p->has_error) {
        if (ident_eq(&p->cur, "outline")) {
            next_token(p);
            expect(p, ETOK_LBRACE);

            if (ident_eq(&p->cur, "rect")) {
                next_token(p);
                expect(p, ETOK_LPAREN);
                DC_PcbOp op = {0};
                op.type = DC_PCB_OP_SET_OUTLINE_RECT;
                op.x = eat_number(p);
                eat(p, ETOK_COMMA);
                op.y = eat_number(p);
                expect(p, ETOK_RPAREN);
                eat(p, ETOK_SEMI);
                dc_array_push(ops, &op);
            } else {
                /* Skip unsupported outline types */
                int depth = 1;
                while (depth > 0 && p->cur.type != ETOK_EOF) {
                    if (p->cur.type == ETOK_LBRACE) depth++;
                    if (p->cur.type == ETOK_RBRACE) depth--;
                    if (depth > 0) next_token(p);
                }
            }

            expect(p, ETOK_RBRACE);

        } else if (ident_eq(&p->cur, "rules")) {
            next_token(p);
            expect(p, ETOK_LBRACE);

            while (p->cur.type != ETOK_RBRACE && p->cur.type != ETOK_EOF && !p->has_error) {
                DC_PcbOp op = {0};
                op.type = DC_PCB_OP_SET_RULE;
                op.rule_key = tok_strdup(&p->cur);
                next_token(p);
                expect(p, ETOK_EQ);
                op.value = eat_number(p);
                eat(p, ETOK_SEMI);
                dc_array_push(ops, &op);
            }

            expect(p, ETOK_RBRACE);

        } else if (ident_eq(&p->cur, "place")) {
            /* place REF at X, Y on LAYER [>> rotate(ANGLE)]; */
            next_token(p);

            DC_PcbOp op = {0};
            op.type = DC_PCB_OP_PLACE;
            op.ref = tok_strdup(&p->cur);
            next_token(p);

            if (ident_eq(&p->cur, "at")) {
                next_token(p);
                op.x = eat_number(p);
                eat(p, ETOK_COMMA);
                op.y = eat_number(p);
            }

            if (ident_eq(&p->cur, "on")) {
                next_token(p);
                char *layer_name = tok_strdup(&p->cur);
                op.layer = dc_pcb_layer_from_name(layer_name);
                if (op.layer < 0) op.layer = DC_PCB_LAYER_F_CU;
                free(layer_name);
                next_token(p);
            }

            /* Optional pipe: >> rotate(angle) */
            while (p->cur.type == ETOK_PIPE && !p->has_error) {
                next_token(p);
                if (ident_eq(&p->cur, "rotate")) {
                    next_token(p);
                    expect(p, ETOK_LPAREN);
                    op.angle = eat_number(p);
                    expect(p, ETOK_RPAREN);
                } else {
                    /* Skip unknown pipe target */
                    next_token(p);
                    if (p->cur.type == ETOK_LPAREN) {
                        next_token(p);
                        while (p->cur.type != ETOK_RPAREN && p->cur.type != ETOK_EOF)
                            next_token(p);
                        eat(p, ETOK_RPAREN);
                    }
                }
            }

            eat(p, ETOK_SEMI);
            dc_array_push(ops, &op);

        } else if (ident_eq(&p->cur, "route")) {
            /* route NAME layer LAYER width W { from REF.PIN; to X,Y; ... } */
            next_token(p);

            char *net_name = tok_strdup(&p->cur);
            next_token(p);

            int layer = DC_PCB_LAYER_F_CU;
            double width = 0.2;

            if (ident_eq(&p->cur, "layer")) {
                next_token(p);
                char *ln = tok_strdup(&p->cur);
                layer = dc_pcb_layer_from_name(ln);
                if (layer < 0) layer = DC_PCB_LAYER_F_CU;
                free(ln);
                next_token(p);
            }

            if (ident_eq(&p->cur, "width")) {
                next_token(p);
                width = eat_number(p);
            }

            expect(p, ETOK_LBRACE);

            /* Collect waypoints — each "from" or "to" is a point.
             * We emit route segments between consecutive points. */
            double prev_x = 0, prev_y = 0;
            int have_prev = 0;

            while (p->cur.type != ETOK_RBRACE && p->cur.type != ETOK_EOF && !p->has_error) {
                int is_from = ident_eq(&p->cur, "from");
                int is_to = ident_eq(&p->cur, "to");

                if (is_from || is_to) {
                    next_token(p);

                    double wx, wy;
                    /* Could be a number pair or a REF.PIN reference */
                    if (p->cur.type == ETOK_NUMBER) {
                        wx = eat_number(p);
                        eat(p, ETOK_COMMA);
                        wy = eat_number(p);
                    } else {
                        /* REF.PIN — store as placeholder coordinates (0,0).
                         * In a real implementation, pin positions would be
                         * resolved during apply. For now, skip. */
                        next_token(p); /* skip the ref.pin identifier */
                        wx = 0;
                        wy = 0;
                    }
                    eat(p, ETOK_SEMI);

                    if (have_prev && is_to) {
                        DC_PcbOp op = {0};
                        op.type = DC_PCB_OP_ROUTE_SEGMENT;
                        op.name = strdup(net_name);
                        op.x = prev_x;
                        op.y = prev_y;
                        op.x2 = wx;
                        op.y2 = wy;
                        op.width = width;
                        op.layer = layer;
                        dc_array_push(ops, &op);
                    }

                    prev_x = wx;
                    prev_y = wy;
                    have_prev = 1;
                } else {
                    next_token(p);
                }
            }

            expect(p, ETOK_RBRACE);
            free(net_name);

        } else if (ident_eq(&p->cur, "zone")) {
            /* zone NAME layer LAYER { rect(X, Y, W, H); } */
            next_token(p);

            char *net_name = tok_strdup(&p->cur);
            next_token(p);

            int layer = DC_PCB_LAYER_F_CU;
            if (ident_eq(&p->cur, "layer")) {
                next_token(p);
                char *ln = tok_strdup(&p->cur);
                layer = dc_pcb_layer_from_name(ln);
                if (layer < 0) layer = DC_PCB_LAYER_F_CU;
                free(ln);
                next_token(p);
            }

            expect(p, ETOK_LBRACE);

            if (ident_eq(&p->cur, "rect")) {
                next_token(p);
                expect(p, ETOK_LPAREN);

                DC_PcbOp op = {0};
                op.type = DC_PCB_OP_ADD_ZONE;
                op.name = net_name;
                op.layer = layer;
                op.x = eat_number(p);
                eat(p, ETOK_COMMA);
                op.y = eat_number(p);
                eat(p, ETOK_COMMA);
                op.x2 = eat_number(p);
                eat(p, ETOK_COMMA);
                op.y2 = eat_number(p);

                expect(p, ETOK_RPAREN);
                eat(p, ETOK_SEMI);
                dc_array_push(ops, &op);
            } else {
                free(net_name);
                /* Skip unsupported zone content */
                int depth = 1;
                while (depth > 0 && p->cur.type != ETOK_EOF) {
                    if (p->cur.type == ETOK_LBRACE) depth++;
                    if (p->cur.type == ETOK_RBRACE) depth--;
                    if (depth > 0) next_token(p);
                }
            }

            expect(p, ETOK_RBRACE);

        } else {
            next_token(p);
        }
    }

    expect(p, ETOK_RBRACE);
}

/* =========================================================================
 * Top-level parser — find domain blocks in Cubeiform source
 * ========================================================================= */
/* =========================================================================
 * Voxel statement parser — parses a single SDF statement
 * ========================================================================= */
static void
parse_voxel_block_inner(EParser *p, DC_Array *vox_ops)
{
    /* Parse one SDF statement at the current position */
    if (p->cur.type == ETOK_EOF || p->has_error) return;
    {
        if (ident_eq(&p->cur, "resolution")) {
            next_token(p);
            DC_VoxOp op = { .type = DC_VOX_OP_SET_RESOLUTION };
            op.resolution = (int)eat_number(p);
            expect(p, ETOK_SEMI);
            dc_array_push(vox_ops, &op);
        } else if (ident_eq(&p->cur, "cell_size")) {
            next_token(p);
            DC_VoxOp op = { .type = DC_VOX_OP_SET_CELL_SIZE };
            op.cell_size = (float)eat_number(p);
            expect(p, ETOK_SEMI);
            dc_array_push(vox_ops, &op);
        } else if (ident_eq(&p->cur, "sphere")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_VoxOp op = { .type = DC_VOX_OP_SPHERE };
            op.x = eat_number(p); expect(p, ETOK_COMMA);
            op.y = eat_number(p); expect(p, ETOK_COMMA);
            op.z = eat_number(p); expect(p, ETOK_COMMA);
            op.radius = eat_number(p);
            expect(p, ETOK_RPAREN);
            expect(p, ETOK_SEMI);
            dc_array_push(vox_ops, &op);
        } else if (ident_eq(&p->cur, "box")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_VoxOp op = { .type = DC_VOX_OP_BOX };
            op.x  = eat_number(p); expect(p, ETOK_COMMA);
            op.y  = eat_number(p); expect(p, ETOK_COMMA);
            op.z  = eat_number(p); expect(p, ETOK_COMMA);
            op.x2 = eat_number(p); expect(p, ETOK_COMMA);
            op.y2 = eat_number(p); expect(p, ETOK_COMMA);
            op.z2 = eat_number(p);
            expect(p, ETOK_RPAREN);
            expect(p, ETOK_SEMI);
            dc_array_push(vox_ops, &op);
        } else if (ident_eq(&p->cur, "cylinder")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_VoxOp op = { .type = DC_VOX_OP_CYLINDER };
            op.x = eat_number(p); expect(p, ETOK_COMMA);
            op.y = eat_number(p); expect(p, ETOK_COMMA);
            op.radius = eat_number(p); expect(p, ETOK_COMMA);
            op.z = eat_number(p); expect(p, ETOK_COMMA);
            op.radius2 = eat_number(p);
            expect(p, ETOK_RPAREN);
            expect(p, ETOK_SEMI);
            dc_array_push(vox_ops, &op);
        } else if (ident_eq(&p->cur, "torus")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_VoxOp op = { .type = DC_VOX_OP_TORUS };
            op.x = eat_number(p); expect(p, ETOK_COMMA);
            op.y = eat_number(p); expect(p, ETOK_COMMA);
            op.z = eat_number(p); expect(p, ETOK_COMMA);
            op.radius = eat_number(p); expect(p, ETOK_COMMA);
            op.radius2 = eat_number(p);
            expect(p, ETOK_RPAREN);
            expect(p, ETOK_SEMI);
            dc_array_push(vox_ops, &op);
        } else if (ident_eq(&p->cur, "subtract")) {
            next_token(p);
            DC_VoxOp wrapper = { .type = DC_VOX_OP_SUBTRACT };
            dc_array_push(vox_ops, &wrapper);
            /* The next statement is the operand — parse it as part of the block */
        } else if (ident_eq(&p->cur, "intersect")) {
            next_token(p);
            DC_VoxOp wrapper = { .type = DC_VOX_OP_INTERSECT };
            dc_array_push(vox_ops, &wrapper);
        } else if (ident_eq(&p->cur, "union")) {
            next_token(p);
            DC_VoxOp wrapper = { .type = DC_VOX_OP_UNION };
            dc_array_push(vox_ops, &wrapper);
        } else if (ident_eq(&p->cur, "color")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_VoxOp op = { .type = DC_VOX_OP_COLOR };
            op.r = (uint8_t)eat_number(p); expect(p, ETOK_COMMA);
            op.g = (uint8_t)eat_number(p); expect(p, ETOK_COMMA);
            op.b = (uint8_t)eat_number(p);
            expect(p, ETOK_RPAREN);
            expect(p, ETOK_SEMI);
            dc_array_push(vox_ops, &op);
        } else if (ident_eq(&p->cur, "translate")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_VoxOp op = { .type = DC_VOX_OP_TRANSLATE };
            op.x = eat_number(p); expect(p, ETOK_COMMA);
            op.y = eat_number(p); expect(p, ETOK_COMMA);
            op.z = eat_number(p);
            expect(p, ETOK_RPAREN);
            dc_array_push(vox_ops, &op);
            /* Parse block body */
            if (p->cur.type == ETOK_LBRACE) {
                next_token(p);
                while (p->cur.type != ETOK_RBRACE && p->cur.type != ETOK_EOF && !p->has_error)
                    parse_voxel_block_inner(p, vox_ops);
                if (p->cur.type == ETOK_RBRACE) next_token(p);
            } else {
                /* Single statement form */
                parse_voxel_block_inner(p, vox_ops);
            }
            DC_VoxOp pop = { .type = DC_VOX_OP_POP_TRANSFORM };
            dc_array_push(vox_ops, &pop);
        } else if (ident_eq(&p->cur, "rotate")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_VoxOp op = { .type = DC_VOX_OP_ROTATE };
            op.x = eat_number(p); expect(p, ETOK_COMMA);
            op.y = eat_number(p); expect(p, ETOK_COMMA);
            op.z = eat_number(p);
            /* Optional 4th arg: angle. Default rotation is Euler angles
             * (rotate each axis by x,y,z degrees). */
            if (p->cur.type == ETOK_COMMA) {
                next_token(p);
                op.radius = eat_number(p); /* angle in degrees */
            }
            expect(p, ETOK_RPAREN);
            dc_array_push(vox_ops, &op);
            if (p->cur.type == ETOK_LBRACE) {
                next_token(p);
                while (p->cur.type != ETOK_RBRACE && p->cur.type != ETOK_EOF && !p->has_error)
                    parse_voxel_block_inner(p, vox_ops);
                if (p->cur.type == ETOK_RBRACE) next_token(p);
            } else {
                parse_voxel_block_inner(p, vox_ops);
            }
            DC_VoxOp pop = { .type = DC_VOX_OP_POP_TRANSFORM };
            dc_array_push(vox_ops, &pop);
        } else if (ident_eq(&p->cur, "scale")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_VoxOp op = { .type = DC_VOX_OP_SCALE };
            op.x = eat_number(p);
            if (p->cur.type == ETOK_COMMA) {
                next_token(p);
                op.y = eat_number(p);
                if (p->cur.type == ETOK_COMMA) {
                    next_token(p);
                    op.z = eat_number(p);
                } else {
                    op.z = op.y;
                }
            } else {
                /* Uniform scale */
                op.y = op.x;
                op.z = op.x;
            }
            expect(p, ETOK_RPAREN);
            dc_array_push(vox_ops, &op);
            if (p->cur.type == ETOK_LBRACE) {
                next_token(p);
                while (p->cur.type != ETOK_RBRACE && p->cur.type != ETOK_EOF && !p->has_error)
                    parse_voxel_block_inner(p, vox_ops);
                if (p->cur.type == ETOK_RBRACE) next_token(p);
            } else {
                parse_voxel_block_inner(p, vox_ops);
            }
            DC_VoxOp pop = { .type = DC_VOX_OP_POP_TRANSFORM };
            dc_array_push(vox_ops, &pop);
        } else {
            return; /* not an SDF statement — caller handles */
        }
    }
}

/* Parse a voxel { ... } block — calls inner parser in a loop */
static void
parse_voxel_block(EParser *p, DC_Array *vox_ops)
{
    if (p->cur.type != ETOK_LBRACE) {
        if (!p->has_error) {
            DC_SET_ERROR(p->err, DC_ERROR_EDA_PARSE, "expected '{' after voxel");
            p->has_error = 1;
        }
        return;
    }
    next_token(p); /* skip { */

    while (p->cur.type != ETOK_RBRACE && p->cur.type != ETOK_EOF && !p->has_error) {
        parse_voxel_block_inner(p, vox_ops);
    }

    if (p->cur.type == ETOK_RBRACE) next_token(p);
}

static void find_and_parse_blocks(EParser *p, DC_Array *sch_ops, DC_Array *pcb_ops, DC_Array *vox_ops)
{
    while (p->cur.type != ETOK_EOF && !p->has_error) {
        if (ident_eq(&p->cur, "schematic")) {
            next_token(p);
            parse_schematic_block(p, sch_ops);
        } else if (ident_eq(&p->cur, "pcb")) {
            next_token(p);
            parse_pcb_block(p, pcb_ops);
        } else if (ident_eq(&p->cur, "voxel")) {
            next_token(p);
            parse_voxel_block(p, vox_ops);
        } else if (ident_eq(&p->cur, "sphere") ||
                   ident_eq(&p->cur, "box") ||
                   ident_eq(&p->cur, "cylinder") ||
                   ident_eq(&p->cur, "torus") ||
                   ident_eq(&p->cur, "subtract") ||
                   ident_eq(&p->cur, "intersect") ||
                   ident_eq(&p->cur, "union") ||
                   ident_eq(&p->cur, "resolution") ||
                   ident_eq(&p->cur, "cell_size") ||
                   ident_eq(&p->cur, "color") ||
                   ident_eq(&p->cur, "translate") ||
                   ident_eq(&p->cur, "rotate") ||
                   ident_eq(&p->cur, "scale")) {
            /* Top-level SDF primitives + transforms — all is rendered natively */
            parse_voxel_block_inner(p, vox_ops);
        } else if (ident_eq(&p->cur, "assembly")) {
            /* Skip assembly blocks for now — future */
            next_token(p);
            if (p->cur.type == ETOK_LBRACE) {
                int depth = 1;
                next_token(p);
                while (depth > 0 && p->cur.type != ETOK_EOF) {
                    if (p->cur.type == ETOK_LBRACE) depth++;
                    if (p->cur.type == ETOK_RBRACE) depth--;
                    if (depth > 0) next_token(p);
                }
                if (p->cur.type == ETOK_RBRACE) next_token(p);
            }
        } else {
            /* Skip non-EDA content */
            if (p->cur.type == ETOK_LBRACE) {
                int depth = 1;
                next_token(p);
                while (depth > 0 && p->cur.type != ETOK_EOF) {
                    if (p->cur.type == ETOK_LBRACE) depth++;
                    if (p->cur.type == ETOK_RBRACE) depth--;
                    if (depth > 0) next_token(p);
                }
                if (p->cur.type == ETOK_RBRACE) next_token(p);
            } else {
                next_token(p);
            }
        }
    }
}

/* =========================================================================
 * Public API — Parsing
 * ========================================================================= */
DC_CubeiformEda *
dc_cubeiform_parse_eda(const char *dcad_src, DC_Error *err)
{
    if (!dcad_src) {
        DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL source");
        return NULL;
    }

    DC_CubeiformEda *eda = calloc(1, sizeof(*eda));
    if (!eda) {
        DC_SET_ERROR(err, DC_ERROR_MEMORY, "alloc CubeiformEda");
        return NULL;
    }

    eda->sch_ops = dc_array_new(sizeof(DC_SchOp));
    eda->pcb_ops = dc_array_new(sizeof(DC_PcbOp));
    eda->vox_ops = dc_array_new(sizeof(DC_VoxOp));
    if (!eda->sch_ops || !eda->pcb_ops || !eda->vox_ops) {
        dc_cubeiform_eda_free(eda);
        DC_SET_ERROR(err, DC_ERROR_MEMORY, "alloc op arrays");
        return NULL;
    }

    EParser p = {0};
    p.src = dcad_src;
    p.len = (int)strlen(dcad_src);
    p.pos = 0;
    p.err = err;
    p.has_error = 0;

    next_token(&p);
    find_and_parse_blocks(&p, eda->sch_ops, eda->pcb_ops, eda->vox_ops);

    if (p.has_error) {
        dc_cubeiform_eda_free(eda);
        return NULL;
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA,
           "Cubeiform parsed: %zu sch, %zu pcb, %zu vox ops",
           dc_array_length(eda->sch_ops), dc_array_length(eda->pcb_ops),
           dc_array_length(eda->vox_ops));

    return eda;
}

void
dc_cubeiform_eda_free(DC_CubeiformEda *eda)
{
    if (!eda) return;

    if (eda->sch_ops) {
        for (size_t i = 0; i < dc_array_length(eda->sch_ops); i++) {
            DC_SchOp *op = dc_array_get(eda->sch_ops, i);
            sch_op_free_fields(op);
        }
        dc_array_free(eda->sch_ops);
    }

    if (eda->pcb_ops) {
        for (size_t i = 0; i < dc_array_length(eda->pcb_ops); i++) {
            DC_PcbOp *op = dc_array_get(eda->pcb_ops, i);
            pcb_op_free_fields(op);
        }
        dc_array_free(eda->pcb_ops);
    }

    if (eda->vox_ops)
        dc_array_free(eda->vox_ops);

    free(eda);
}

/* =========================================================================
 * Query
 * ========================================================================= */
size_t dc_cubeiform_eda_sch_op_count(const DC_CubeiformEda *eda)
{
    return eda ? dc_array_length(eda->sch_ops) : 0;
}

size_t dc_cubeiform_eda_pcb_op_count(const DC_CubeiformEda *eda)
{
    return eda ? dc_array_length(eda->pcb_ops) : 0;
}

const DC_SchOp *dc_cubeiform_eda_get_sch_op(const DC_CubeiformEda *eda, size_t i)
{
    if (!eda || i >= dc_array_length(eda->sch_ops)) return NULL;
    return dc_array_get(eda->sch_ops, i);
}

const DC_PcbOp *dc_cubeiform_eda_get_pcb_op(const DC_CubeiformEda *eda, size_t i)
{
    if (!eda || i >= dc_array_length(eda->pcb_ops)) return NULL;
    return dc_array_get(eda->pcb_ops, i);
}

size_t dc_cubeiform_eda_vox_op_count(const DC_CubeiformEda *eda)
{
    return eda && eda->vox_ops ? dc_array_length(eda->vox_ops) : 0;
}

const DC_VoxOp *dc_cubeiform_eda_get_vox_op(const DC_CubeiformEda *eda, size_t i)
{
    if (!eda || !eda->vox_ops || i >= dc_array_length(eda->vox_ops)) return NULL;
    return dc_array_get(eda->vox_ops, i);
}

/* =========================================================================
 * Apply — schematic operations
 * ========================================================================= */
int
dc_cubeiform_eda_apply_schematic(DC_CubeiformEda *eda,
                                  DC_ESchematic *sch,
                                  DC_ELibrary *lib,
                                  DC_Error *err)
{
    if (!eda || !sch) {
        DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL eda or schematic");
        return -1;
    }

    (void)lib; /* Used for symbol resolution in future */

    for (size_t i = 0; i < dc_array_length(eda->sch_ops); i++) {
        const DC_SchOp *op = dc_array_get(eda->sch_ops, i);

        switch (op->type) {
        case DC_SCH_OP_ADD_COMPONENT: {
            size_t idx = dc_eschematic_add_symbol(sch, op->lib_id,
                                                    op->ref, op->x, op->y);
            if (idx == (size_t)-1) {
                DC_SET_ERROR(err, DC_ERROR_EDA_PARSE,
                             "failed to add component %s", op->ref);
                return -1;
            }
            break;
        }
        case DC_SCH_OP_ADD_WIRE: {
            /* Wire ops connect net segments. For now, we use the
             * stored coordinates (0,0 placeholders for pin refs).
             * A full implementation would resolve pin positions. */
            dc_eschematic_add_wire(sch, op->x, op->y, op->x2, op->y2);
            /* Also add a label for the net name if provided */
            if (op->name && op->name[0]) {
                dc_eschematic_add_label(sch, op->name, op->x, op->y);
            }
            break;
        }
        case DC_SCH_OP_ADD_POWER: {
            dc_eschematic_add_power_port(sch, op->name, op->x, op->y);
            break;
        }
        case DC_SCH_OP_SET_VALUE: {
            DC_SchSymbol *sym = dc_eschematic_find_symbol(sch, op->ref);
            if (sym) {
                /* Find the symbol index for set_property */
                for (size_t j = 0; j < dc_eschematic_symbol_count(sch); j++) {
                    DC_SchSymbol *s = dc_eschematic_get_symbol(sch, j);
                    if (s == sym) {
                        dc_eschematic_set_property(sch, j, "Value", op->str_value);
                        break;
                    }
                }
            }
            break;
        }
        case DC_SCH_OP_SET_FOOTPRINT: {
            DC_SchSymbol *sym = dc_eschematic_find_symbol(sch, op->ref);
            if (sym) {
                for (size_t j = 0; j < dc_eschematic_symbol_count(sch); j++) {
                    DC_SchSymbol *s = dc_eschematic_get_symbol(sch, j);
                    if (s == sym) {
                        dc_eschematic_set_property(sch, j, "Footprint", op->str_value);
                        break;
                    }
                }
            }
            break;
        }
        }
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA,
           "Applied %zu schematic ops", dc_array_length(eda->sch_ops));
    return 0;
}

/* =========================================================================
 * Apply — PCB operations
 * ========================================================================= */
int
dc_cubeiform_eda_apply_pcb(DC_CubeiformEda *eda,
                            DC_EPcb *pcb,
                            DC_ELibrary *lib,
                            DC_Error *err)
{
    if (!eda || !pcb) {
        DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL eda or pcb");
        return -1;
    }

    (void)lib; /* Future: footprint resolution */

    DC_PcbDesignRules *rules = dc_epcb_get_design_rules(pcb);

    for (size_t i = 0; i < dc_array_length(eda->pcb_ops); i++) {
        const DC_PcbOp *op = dc_array_get(eda->pcb_ops, i);

        switch (op->type) {
        case DC_PCB_OP_SET_OUTLINE_RECT: {
            /* Add 4 track segments on Edge.Cuts to form rectangle */
            double w = op->x, h = op->y;
            int edge = DC_PCB_LAYER_EDGE_CUTS;
            dc_epcb_add_track(pcb, 0, 0, w, 0, 0.05, edge, 0);
            dc_epcb_add_track(pcb, w, 0, w, h, 0.05, edge, 0);
            dc_epcb_add_track(pcb, w, h, 0, h, 0.05, edge, 0);
            dc_epcb_add_track(pcb, 0, h, 0, 0, 0.05, edge, 0);
            break;
        }
        case DC_PCB_OP_SET_RULE: {
            if (!rules) break;
            if (op->rule_key) {
                if (strcmp(op->rule_key, "clearance") == 0)
                    rules->clearance = op->value;
                else if (strcmp(op->rule_key, "track_width") == 0)
                    rules->track_width = op->value;
                else if (strcmp(op->rule_key, "via_size") == 0)
                    rules->via_size = op->value;
                else if (strcmp(op->rule_key, "via_drill") == 0)
                    rules->via_drill = op->value;
                else if (strcmp(op->rule_key, "min_track_width") == 0)
                    rules->min_track_width = op->value;
                else if (strcmp(op->rule_key, "edge_clearance") == 0)
                    rules->edge_clearance = op->value;
            }
            break;
        }
        case DC_PCB_OP_PLACE: {
            dc_epcb_add_footprint(pcb, "" /* lib_id resolved later */,
                                    op->ref, op->x, op->y, op->layer);
            break;
        }
        case DC_PCB_OP_ROUTE_SEGMENT: {
            /* Find or create the net */
            int net_id = 0;
            if (op->name) {
                net_id = dc_epcb_find_net(pcb, op->name);
                if (net_id < 0) {
                    net_id = dc_epcb_add_net(pcb, op->name);
                }
            }
            dc_epcb_add_track(pcb, op->x, op->y, op->x2, op->y2,
                                op->width, op->layer, net_id);
            break;
        }
        case DC_PCB_OP_ADD_ZONE: {
            double zw = op->x2 - op->x;
            double zh = op->y2 - op->y;
            double clearance = rules ? rules->clearance : 0.2;
            dc_epcb_add_zone(pcb, op->name, op->layer, clearance,
                              op->x, op->y, zw, zh);
            break;
        }
        }
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA,
           "Applied %zu PCB ops", dc_array_length(eda->pcb_ops));
    return 0;
}

/* =========================================================================
 * Unified execute
 * ========================================================================= */
int
dc_cubeiform_execute(const char *dcad_src,
                      DC_ESchematic *sch,
                      DC_EPcb *pcb,
                      void *asm_ctx,
                      DC_ELibrary *lib,
                      DC_Error *err)
{
    (void)asm_ctx; /* Future */

    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(dcad_src, err);
    if (!eda) return -1;

    int rc = 0;

    if (sch && dc_array_length(eda->sch_ops) > 0) {
        rc = dc_cubeiform_eda_apply_schematic(eda, sch, lib, err);
        if (rc != 0) {
            dc_cubeiform_eda_free(eda);
            return rc;
        }
    }

    if (pcb && dc_array_length(eda->pcb_ops) > 0) {
        rc = dc_cubeiform_eda_apply_pcb(eda, pcb, lib, err);
        if (rc != 0) {
            dc_cubeiform_eda_free(eda);
            return rc;
        }
    }

    dc_cubeiform_eda_free(eda);
    return 0;
}

int
dc_cubeiform_execute_full(const char *dcad_src,
                            DC_ESchematic *sch,
                            DC_EPcb *pcb,
                            DC_VoxelGrid **vox_out,
                            DC_ELibrary *lib,
                            DC_Error *err)
{
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(dcad_src, err);
    if (!eda) return -1;

    int rc = 0;

    if (sch && dc_array_length(eda->sch_ops) > 0) {
        rc = dc_cubeiform_eda_apply_schematic(eda, sch, lib, err);
        if (rc != 0) { dc_cubeiform_eda_free(eda); return rc; }
    }

    if (pcb && dc_array_length(eda->pcb_ops) > 0) {
        rc = dc_cubeiform_eda_apply_pcb(eda, pcb, lib, err);
        if (rc != 0) { dc_cubeiform_eda_free(eda); return rc; }
    }

    if (vox_out && eda->vox_ops && dc_array_length(eda->vox_ops) > 0) {
        *vox_out = dc_cubeiform_eda_apply_voxel(eda, err);
    }

    dc_cubeiform_eda_free(eda);
    return 0;
}

/* =========================================================================
 * Apply — voxel operations → DC_VoxelGrid
 * ========================================================================= */
DC_VoxelGrid *
dc_cubeiform_eda_apply_voxel(DC_CubeiformEda *eda, DC_Error *err)
{
    if (!eda || !eda->vox_ops || dc_array_length(eda->vox_ops) == 0)
        return NULL;

    int resolution = 64;
    float user_cell_size = 0; /* 0 = auto-compute */
    uint8_t cr = 180, cg = 180, cb = 180;

    /* First pass: find resolution, cell_size, and compute bounding box.
     * We track a transform stack to correctly bound transformed primitives. */
    size_t nops = dc_array_length(eda->vox_ops);
    float bmin[3] = {1e18f, 1e18f, 1e18f};
    float bmax[3] = {-1e18f, -1e18f, -1e18f};

    /* Transform stack for bounding box pass */
    #define MAX_XFORM_DEPTH 32
    DC_SdfTransform xform_stack[MAX_XFORM_DEPTH];
    int xform_depth = 0;
    dc_sdf_transform_identity(&xform_stack[0]);

    /* Helper: transform 8 corners of a local AABB through forward transform,
     * expand world AABB. Defined as a local macro for code reuse. */
    #define EXPAND_BBOX_TRANSFORMED(lmin0, lmin1, lmin2, lmax0, lmax1, lmax2) \
    do { \
        float corners[8][3] = { \
            {(lmin0),(lmin1),(lmin2)}, {(lmax0),(lmin1),(lmin2)}, \
            {(lmin0),(lmax1),(lmin2)}, {(lmax0),(lmax1),(lmin2)}, \
            {(lmin0),(lmin1),(lmax2)}, {(lmax0),(lmin1),(lmax2)}, \
            {(lmin0),(lmax1),(lmax2)}, {(lmax0),(lmax1),(lmax2)}, \
        }; \
        const DC_SdfTransform *_xf = &xform_stack[xform_depth]; \
        for (int _ci = 0; _ci < 8; _ci++) { \
            float _wx = _xf->mat[0]*corners[_ci][0] + _xf->mat[4]*corners[_ci][1] + _xf->mat[8]*corners[_ci][2] + _xf->mat[12]; \
            float _wy = _xf->mat[1]*corners[_ci][0] + _xf->mat[5]*corners[_ci][1] + _xf->mat[9]*corners[_ci][2] + _xf->mat[13]; \
            float _wz = _xf->mat[2]*corners[_ci][0] + _xf->mat[6]*corners[_ci][1] + _xf->mat[10]*corners[_ci][2] + _xf->mat[14]; \
            if (_wx < bmin[0]) { bmin[0] = _wx; } if (_wx > bmax[0]) { bmax[0] = _wx; } \
            if (_wy < bmin[1]) { bmin[1] = _wy; } if (_wy > bmax[1]) { bmax[1] = _wy; } \
            if (_wz < bmin[2]) { bmin[2] = _wz; } if (_wz > bmax[2]) { bmax[2] = _wz; } \
        } \
    } while (0)

    for (size_t i = 0; i < nops; i++) {
        DC_VoxOp *op = dc_array_get(eda->vox_ops, i);
        if (op->type == DC_VOX_OP_SET_RESOLUTION) resolution = op->resolution;
        if (op->type == DC_VOX_OP_SET_CELL_SIZE) user_cell_size = op->cell_size;

        /* Transform stack management */
        if (op->type == DC_VOX_OP_TRANSLATE && xform_depth + 1 < MAX_XFORM_DEPTH) {
            xform_depth++;
            xform_stack[xform_depth] = xform_stack[xform_depth - 1];
            dc_sdf_transform_translate(&xform_stack[xform_depth],
                                       (float)op->x, (float)op->y, (float)op->z);
            continue;
        }
        if (op->type == DC_VOX_OP_ROTATE && xform_depth + 1 < MAX_XFORM_DEPTH) {
            xform_depth++;
            xform_stack[xform_depth] = xform_stack[xform_depth - 1];
            if (op->radius != 0) {
                dc_sdf_transform_rotate(&xform_stack[xform_depth],
                                        (float)op->x, (float)op->y, (float)op->z, (float)op->radius);
            } else {
                /* Euler angles: rotate Z, then Y, then X */
                if ((float)op->z != 0) dc_sdf_transform_rotate(&xform_stack[xform_depth], 0, 0, 1, (float)op->z);
                if ((float)op->y != 0) dc_sdf_transform_rotate(&xform_stack[xform_depth], 0, 1, 0, (float)op->y);
                if ((float)op->x != 0) dc_sdf_transform_rotate(&xform_stack[xform_depth], 1, 0, 0, (float)op->x);
            }
            continue;
        }
        if (op->type == DC_VOX_OP_SCALE && xform_depth + 1 < MAX_XFORM_DEPTH) {
            xform_depth++;
            xform_stack[xform_depth] = xform_stack[xform_depth - 1];
            dc_sdf_transform_scale(&xform_stack[xform_depth],
                                   (float)op->x, (float)op->y, (float)op->z);
            continue;
        }
        if (op->type == DC_VOX_OP_POP_TRANSFORM) {
            if (xform_depth > 0) xform_depth--;
            continue;
        }

        /* Accumulate bounding box from primitives (transformed) */
        float r;
        switch (op->type) {
        case DC_VOX_OP_SPHERE:
            r = (float)op->radius;
            EXPAND_BBOX_TRANSFORMED((float)op->x - r, (float)op->y - r, (float)op->z - r,
                                    (float)op->x + r, (float)op->y + r, (float)op->z + r);
            break;
        case DC_VOX_OP_BOX:
            EXPAND_BBOX_TRANSFORMED((float)op->x, (float)op->y, (float)op->z,
                                    (float)op->x2, (float)op->y2, (float)op->z2);
            break;
        case DC_VOX_OP_CYLINDER:
            r = (float)op->radius;
            EXPAND_BBOX_TRANSFORMED((float)op->x - r, (float)op->y - r, (float)op->z,
                                    (float)op->x + r, (float)op->y + r, (float)op->radius2);
            break;
        case DC_VOX_OP_TORUS:
            r = (float)(op->radius + op->radius2);
            EXPAND_BBOX_TRANSFORMED((float)op->x - r, (float)op->y - r, (float)op->z - (float)op->radius2,
                                    (float)op->x + r, (float)op->y + r, (float)op->z + (float)op->radius2);
            break;
        default: break;
        }
    }
    #undef EXPAND_BBOX_TRANSFORMED

    if (resolution < 8) resolution = 8;
    if (resolution > 512) resolution = 512;

    /* Add padding around bounding box */
    float extent[3] = { bmax[0]-bmin[0], bmax[1]-bmin[1], bmax[2]-bmin[2] };
    float max_extent = extent[0];
    if (extent[1] > max_extent) max_extent = extent[1];
    if (extent[2] > max_extent) max_extent = extent[2];
    if (max_extent < 0.001f) max_extent = 1.0f;

    float pad = max_extent * 0.1f;
    for (int a = 0; a < 3; a++) { bmin[a] -= pad; bmax[a] += pad; }
    for (int a = 0; a < 3; a++) extent[a] = bmax[a] - bmin[a];
    max_extent = extent[0];
    if (extent[1] > max_extent) max_extent = extent[1];
    if (extent[2] > max_extent) max_extent = extent[2];

    /* Compute cell_size from resolution (cells per longest axis) */
    float cell_size = user_cell_size > 0 ? user_cell_size : max_extent / (float)resolution;
    int sx = (int)ceilf(extent[0] / cell_size) + 1;
    int sy = (int)ceilf(extent[1] / cell_size) + 1;
    int sz = (int)ceilf(extent[2] / cell_size) + 1;
    if (sx > 512) sx = 512;
    if (sy > 512) sy = 512;
    if (sz > 512) sz = 512;

    DC_VoxelGrid *grid = dc_voxel_grid_new(sx, sy, sz, cell_size);
    if (!grid) {
        DC_SET_ERROR(err, DC_ERROR_MEMORY, "voxel grid alloc");
        return NULL;
    }

    /* Offset: grid coords = world coords - bmin.
     * All SDF primitives must be shifted by -bmin. */
    #define OX(v) ((float)(v) - bmin[0])
    #define OY(v) ((float)(v) - bmin[1])
    #define OZ(v) ((float)(v) - bmin[2])

    DC_VoxelGrid *temp = NULL;
    int csg_pending = 0;

    /* Reset transform stack for second pass */
    xform_depth = 0;
    dc_sdf_transform_identity(&xform_stack[0]);

    for (size_t i = 0; i < nops; i++) {
        DC_VoxOp *op = dc_array_get(eda->vox_ops, i);

        switch (op->type) {
        case DC_VOX_OP_SET_RESOLUTION:
        case DC_VOX_OP_SET_CELL_SIZE:
            break;

        case DC_VOX_OP_SUBTRACT:  csg_pending = 1; break;
        case DC_VOX_OP_INTERSECT: csg_pending = 2; break;
        case DC_VOX_OP_UNION:     csg_pending = 3; break;

        case DC_VOX_OP_COLOR:
            cr = op->r; cg = op->g; cb = op->b;
            break;

        case DC_VOX_OP_TRANSLATE:
            if (xform_depth + 1 < MAX_XFORM_DEPTH) {
                xform_depth++;
                xform_stack[xform_depth] = xform_stack[xform_depth - 1];
                dc_sdf_transform_translate(&xform_stack[xform_depth],
                                           (float)op->x, (float)op->y, (float)op->z);
            }
            break;
        case DC_VOX_OP_ROTATE:
            if (xform_depth + 1 < MAX_XFORM_DEPTH) {
                xform_depth++;
                xform_stack[xform_depth] = xform_stack[xform_depth - 1];
                if (op->radius != 0) {
                    dc_sdf_transform_rotate(&xform_stack[xform_depth],
                                            (float)op->x, (float)op->y, (float)op->z, (float)op->radius);
                } else {
                    if ((float)op->z != 0) dc_sdf_transform_rotate(&xform_stack[xform_depth], 0, 0, 1, (float)op->z);
                    if ((float)op->y != 0) dc_sdf_transform_rotate(&xform_stack[xform_depth], 0, 1, 0, (float)op->y);
                    if ((float)op->x != 0) dc_sdf_transform_rotate(&xform_stack[xform_depth], 1, 0, 0, (float)op->x);
                }
            }
            break;
        case DC_VOX_OP_SCALE:
            if (xform_depth + 1 < MAX_XFORM_DEPTH) {
                xform_depth++;
                xform_stack[xform_depth] = xform_stack[xform_depth - 1];
                dc_sdf_transform_scale(&xform_stack[xform_depth],
                                       (float)op->x, (float)op->y, (float)op->z);
            }
            break;
        case DC_VOX_OP_POP_TRANSFORM:
            if (xform_depth > 0) xform_depth--;
            break;

        case DC_VOX_OP_SPHERE:
        case DC_VOX_OP_BOX:
        case DC_VOX_OP_CYLINDER:
        case DC_VOX_OP_TORUS: {
            DC_VoxelGrid *target = grid;
            if (csg_pending) {
                temp = dc_voxel_grid_new(sx, sy, sz, cell_size);
                if (!temp) break;
                target = temp;
            }

            /* Build the full transform: grid offset (bmin shift) + user transform.
             * The grid offset translates from world coords to grid coords.
             * User transform is applied on top of that. */
            DC_SdfTransform full;
            dc_sdf_transform_identity(&full);
            dc_sdf_transform_translate(&full, -bmin[0], -bmin[1], -bmin[2]);
            DC_SdfTransform composed;
            dc_sdf_transform_compose(&full, &xform_stack[xform_depth], &composed);

            int has_xform = xform_depth > 0;

            if (op->type == DC_VOX_OP_SPHERE) {
                if (has_xform)
                    dc_sdf_sphere_t(target, (float)op->x, (float)op->y, (float)op->z,
                                    (float)op->radius, &composed);
                else
                    dc_sdf_sphere(target, OX(op->x), OY(op->y), OZ(op->z), (float)op->radius);
            } else if (op->type == DC_VOX_OP_BOX) {
                if (has_xform)
                    dc_sdf_box_t(target, (float)op->x, (float)op->y, (float)op->z,
                                 (float)op->x2, (float)op->y2, (float)op->z2, &composed);
                else
                    dc_sdf_box(target, OX(op->x), OY(op->y), OZ(op->z),
                                       OX(op->x2), OY(op->y2), OZ(op->z2));
            } else if (op->type == DC_VOX_OP_CYLINDER) {
                if (has_xform)
                    dc_sdf_cylinder_t(target, (float)op->x, (float)op->y, (float)op->radius,
                                      (float)op->z, (float)op->radius2, &composed);
                else
                    dc_sdf_cylinder(target, OX(op->x), OY(op->y), (float)op->radius,
                                            OZ(op->z), OZ(op->radius2));
            } else if (op->type == DC_VOX_OP_TORUS) {
                if (has_xform)
                    dc_sdf_torus_t(target, (float)op->x, (float)op->y, (float)op->z,
                                   (float)op->radius, (float)op->radius2, &composed);
                else
                    dc_sdf_torus(target, OX(op->x), OY(op->y), OZ(op->z),
                                         (float)op->radius, (float)op->radius2);
            }

            if (csg_pending && temp) {
                DC_VoxelGrid *out = dc_voxel_grid_new(sx, sy, sz, cell_size);
                if (out) {
                    if (csg_pending == 1) dc_sdf_subtract(grid, temp, out);
                    else if (csg_pending == 2) dc_sdf_intersect(grid, temp, out);
                    else dc_sdf_union(grid, temp, out);
                    dc_voxel_grid_free(grid);
                    grid = out;
                }
                dc_voxel_grid_free(temp);
                temp = NULL;
                csg_pending = 0;
            }
            break;
        }
        }
    }
    #undef OX
    #undef OY
    #undef OZ
    #undef MAX_XFORM_DEPTH

    /* Activate voxels and fill ALL cells with the solid color.
     * The SDF texture determines the surface boundary — color must
     * be present everywhere so trilinear interpolation doesn't blend
     * with black at the zero-crossing. */
    dc_sdf_activate(grid);
    {
        for (int iz = 0; iz < sz; iz++)
        for (int iy = 0; iy < sy; iy++)
        for (int ix = 0; ix < sx; ix++) {
            DC_Voxel *v = dc_voxel_grid_get(grid, ix, iy, iz);
            if (v) { v->r = cr; v->g = cg; v->b = cb; }
        }
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA,
           "Cubeiform voxel: %dx%dx%d grid, %zu active",
           resolution, resolution, resolution,
           dc_voxel_grid_active_count(grid));

    return grid;
}
