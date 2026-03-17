#define _POSIX_C_SOURCE 200809L

#include "cubeiform_eda.h"

#include "core/array.h"
#include "core/error.h"
#include "core/log.h"
#include "eda/eda_schematic.h"
#include "eda/eda_pcb.h"
#include "eda/eda_library.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * DC_CubeiformEda — internal structure
 * ========================================================================= */
struct DC_CubeiformEda {
    DC_Array *sch_ops;  /* DC_SchOp elements */
    DC_Array *pcb_ops;  /* DC_PcbOp elements */
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
static void find_and_parse_blocks(EParser *p, DC_Array *sch_ops, DC_Array *pcb_ops)
{
    while (p->cur.type != ETOK_EOF && !p->has_error) {
        if (ident_eq(&p->cur, "schematic")) {
            next_token(p);
            parse_schematic_block(p, sch_ops);
        } else if (ident_eq(&p->cur, "pcb")) {
            next_token(p);
            parse_pcb_block(p, pcb_ops);
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
    if (!eda->sch_ops || !eda->pcb_ops) {
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
    find_and_parse_blocks(&p, eda->sch_ops, eda->pcb_ops);

    if (p.has_error) {
        dc_cubeiform_eda_free(eda);
        return NULL;
    }

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA,
           "Cubeiform EDA parsed: %zu sch ops, %zu pcb ops",
           dc_array_length(eda->sch_ops), dc_array_length(eda->pcb_ops));

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
