#define _POSIX_C_SOURCE 200809L

#include "cubeiform_eda.h"

#include "core/array.h"
#include "core/error.h"
#include "core/log.h"
#include "eda/eda_schematic.h"
#include "eda/eda_pcb.h"
#include "eda/eda_library.h"

#include "../../talmud-main/talmud/sacred/trinity_site/ts_bezier_primitives.h"
#include "voxel/voxelize_bezier.h"
#include "voxel/voxelize_gpu.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * DC_CubeiformEda — internal structure
 * ========================================================================= */
struct DC_CubeiformEda {
    DC_Array *sch_ops;   /* DC_SchOp elements */
    DC_Array *pcb_ops;   /* DC_PcbOp elements */
    DC_Array *vox_ops;   /* DC_VoxOp elements */
    DC_Array *bmesh_ops; /* DC_BMeshOp elements */
    int       to_solid;  /* 1 if bmesh wrapped in to_solid() — voxelize result */
    int       to_solid_resolution; /* voxel resolution for to_solid conversion */
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
    ETOK_PIPE,      /* >> */
    ETOK_COLON,
    ETOK_PLUS,      /* + */
    ETOK_MINUS,     /* - */
    ETOK_AMP,       /* & */
    ETOK_STAR,      /* * */
    ETOK_SLASH,     /* / (not comment) */
    ETOK_LBRACKET,  /* [ */
    ETOK_RBRACKET,  /* ] */
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
        case '+': p->cur = (ETok){ETOK_PLUS, p->src + p->pos, 1, 0}; p->pos++; return;
        case '-': p->cur = (ETok){ETOK_MINUS, p->src + p->pos, 1, 0}; p->pos++; return;
        case '&': p->cur = (ETok){ETOK_AMP, p->src + p->pos, 1, 0}; p->pos++; return;
        case '*': p->cur = (ETok){ETOK_STAR, p->src + p->pos, 1, 0}; p->pos++; return;
        case '[': p->cur = (ETok){ETOK_LBRACKET, p->src + p->pos, 1, 0}; p->pos++; return;
        case ']': p->cur = (ETok){ETOK_RBRACKET, p->src + p->pos, 1, 0}; p->pos++; return;
        default: break;
    }

    /* >> pipe operator */
    if (c == '>' && p->pos + 1 < p->len && p->src[p->pos + 1] == '>') {
        p->cur = (ETok){ETOK_PIPE, p->src + p->pos, 2, 0};
        p->pos += 2;
        return;
    }

    /* / as division (comments already consumed by skip_ws) */
    if (c == '/') {
        p->cur = (ETok){ETOK_SLASH, p->src + p->pos, 1, 0};
        p->pos++;
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

    /* Number literal (leading minus handled by arithmetic parser or eat_number) */
    if (isdigit((unsigned char)c)) {
        const char *start = p->src + p->pos;
        char *end = NULL;
        double val = strtod(start, &end);
        int nlen = (int)(end - start);
        p->cur = (ETok){ETOK_NUMBER, start, nlen, val};
        p->pos += nlen;
        return;
    }

    /* Identifier (letters, digits, underscore, $, dot for layer names like F.Cu) */
    if (isalpha((unsigned char)c) || c == '_' || c == '$') {
        int start = p->pos;
        while (p->pos < p->len) {
            char ch = p->src[p->pos];
            if (isalnum((unsigned char)ch) || ch == '_' || ch == '$') {
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
    int neg = 0;
    if (p->cur.type == ETOK_MINUS) {
        neg = 1;
        next_token(p);
    }
    double v = p->cur.num_val;
    expect(p, ETOK_NUMBER);
    return neg ? -v : v;
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
 * Cubeiform voxel parser — pipe syntax, operator CSG, variables, for loops
 *
 * Grammar:
 *   statement   = settings | assignment | for_loop | expr_stmt
 *   settings    = "resolution" NUMBER ";" | "cell_size" NUMBER ";"
 *   assignment  = IDENT "=" expr ";"
 *   for_loop    = "for" IDENT "in" "[" expr ":" expr "]" "{" statements "}"
 *   expr_stmt   = expr ";"
 *
 *   expr        = pipe ( ("+"|"-"|"&") pipe )*
 *   pipe        = primary ( ">>" transform )*
 *   primary     = primitive | "(" expr ")" | IDENT (variable ref)
 *   primitive   = ("sphere"|"cube"|"cylinder"|"torus") "(" args ")"
 *   transform   = ("move"|"rotate"|"scale"|"color") "(" args ")"
 *   args        = arg ("," arg)*
 *   arg         = [IDENT "="] arith_expr
 *
 *   arith_expr  = arith_term (("+"|"-") arith_term)*
 *   arith_term  = arith_factor (("*"|"/") arith_factor)*
 *   arith_factor = NUMBER | IDENT | "-" arith_factor | "(" arith_expr ")"
 * ========================================================================= */

/* Variable table for voxel parser */
#define VOX_MAX_VARS 64
typedef struct {
    char name[64];
    DC_Array *ops; /* owned DC_VoxOp array */
} VoxVarDef;

typedef struct {
    VoxVarDef vars[VOX_MAX_VARS];
    int nvar;
    /* For-loop variable bindings for arithmetic evaluation */
    char loop_var[64];
    double loop_val;
    int has_loop_var;
} VoxParseCtx;

static void vox_ctx_init(VoxParseCtx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

static void vox_ctx_free(VoxParseCtx *ctx)
{
    for (int i = 0; i < ctx->nvar; i++)
        dc_array_free(ctx->vars[i].ops);
}

static VoxVarDef *vox_ctx_find(VoxParseCtx *ctx, const char *name, int len)
{
    for (int i = 0; i < ctx->nvar; i++) {
        if ((int)strlen(ctx->vars[i].name) == len &&
            strncmp(ctx->vars[i].name, name, (size_t)len) == 0)
            return &ctx->vars[i];
    }
    return NULL;
}

static VoxVarDef *vox_ctx_add(VoxParseCtx *ctx, const char *name, int len)
{
    /* Overwrite existing */
    VoxVarDef *v = vox_ctx_find(ctx, name, len);
    if (v) {
        dc_array_clear(v->ops);
        return v;
    }
    if (ctx->nvar >= VOX_MAX_VARS) return NULL;
    v = &ctx->vars[ctx->nvar++];
    int n = len < 63 ? len : 63;
    memcpy(v->name, name, (size_t)n);
    v->name[n] = '\0';
    v->ops = dc_array_new(sizeof(DC_VoxOp));
    return v;
}

/* Copy all ops from a var def into target array */
static void vox_copy_var_ops(DC_Array *dst, const VoxVarDef *v)
{
    size_t n = dc_array_length(v->ops);
    for (size_t i = 0; i < n; i++) {
        DC_VoxOp *op = dc_array_get(v->ops, i);
        dc_array_push(dst, op);
    }
}

/* Copy all ops from one array to another */
static void vox_copy_ops(DC_Array *dst, DC_Array *src)
{
    size_t n = dc_array_length(src);
    for (size_t i = 0; i < n; i++) {
        DC_VoxOp *op = dc_array_get(src, i);
        dc_array_push(dst, op);
    }
}

/* -------------------------------------------------------------------------
 * Arithmetic expression evaluator (for argument positions)
 * ---------------------------------------------------------------------- */
static double eval_arith_expr(EParser *p, VoxParseCtx *ctx);

static double eval_arith_factor(EParser *p, VoxParseCtx *ctx)
{
    if (p->has_error) return 0;

    if (p->cur.type == ETOK_NUMBER) {
        double v = p->cur.num_val;
        next_token(p);
        return v;
    }
    if (p->cur.type == ETOK_MINUS) {
        next_token(p);
        return -eval_arith_factor(p, ctx);
    }
    if (p->cur.type == ETOK_LPAREN) {
        next_token(p);
        double v = eval_arith_expr(p, ctx);
        expect(p, ETOK_RPAREN);
        return v;
    }
    if (p->cur.type == ETOK_IDENT) {
        /* Check loop variable */
        if (ctx->has_loop_var &&
            (int)strlen(ctx->loop_var) == p->cur.len &&
            strncmp(ctx->loop_var, p->cur.start, (size_t)p->cur.len) == 0) {
            next_token(p);
            return ctx->loop_val;
        }
        /* Unknown variable — return 0 */
        next_token(p);
        return 0;
    }
    return 0;
}

static double eval_arith_term(EParser *p, VoxParseCtx *ctx)
{
    double v = eval_arith_factor(p, ctx);
    while (!p->has_error) {
        if (p->cur.type == ETOK_STAR) {
            next_token(p);
            v *= eval_arith_factor(p, ctx);
        } else if (p->cur.type == ETOK_SLASH) {
            next_token(p);
            double d = eval_arith_factor(p, ctx);
            v = (d != 0) ? v / d : 0;
        } else break;
    }
    return v;
}

static double eval_arith_expr(EParser *p, VoxParseCtx *ctx)
{
    double v = eval_arith_term(p, ctx);
    while (!p->has_error) {
        if (p->cur.type == ETOK_PLUS) {
            next_token(p);
            v += eval_arith_term(p, ctx);
        } else if (p->cur.type == ETOK_MINUS) {
            next_token(p);
            v -= eval_arith_term(p, ctx);
        } else break;
    }
    return v;
}

/* -------------------------------------------------------------------------
 * Argument parser — mixed positional/named params
 * ---------------------------------------------------------------------- */
typedef struct {
    double vals[16];
    char   names[16][32];
    int    count;
} VoxArgs;

static double vox_arg_find(const VoxArgs *a, const char *name, double def)
{
    for (int i = 0; i < a->count; i++) {
        if (a->names[i][0] && strcmp(a->names[i], name) == 0)
            return a->vals[i];
    }
    return def;
}

static double vox_arg_pos(const VoxArgs *a, int idx, double def)
{
    return (idx < a->count && a->names[idx][0] == '\0') ? a->vals[idx] : def;
}

static int vox_arg_has(const VoxArgs *a, const char *name)
{
    for (int i = 0; i < a->count; i++) {
        if (a->names[i][0] && strcmp(a->names[i], name) == 0)
            return 1;
    }
    return 0;
}

static void parse_vox_args(EParser *p, VoxParseCtx *ctx, VoxArgs *args)
{
    memset(args, 0, sizeof(*args));
    expect(p, ETOK_LPAREN);

    while (p->cur.type != ETOK_RPAREN && p->cur.type != ETOK_EOF && !p->has_error) {
        if (args->count >= 16) break;

        /* Check for name=value */
        if (p->cur.type == ETOK_IDENT) {
            /* Peek: is next token '='? Save state to restore if not. */
            int save_pos = p->pos;
            ETok save_tok = p->cur;
            char name_buf[32] = {0};
            int nlen = p->cur.len < 31 ? p->cur.len : 31;
            memcpy(name_buf, p->cur.start, (size_t)nlen);
            next_token(p);
            if (p->cur.type == ETOK_EQ) {
                next_token(p);
                args->vals[args->count] = eval_arith_expr(p, ctx);
                memcpy(args->names[args->count], name_buf, 32);
                args->count++;
                if (p->cur.type == ETOK_COMMA) next_token(p);
                continue;
            }
            /* Not named — restore and parse as expression */
            p->pos = save_pos;
            p->cur = save_tok;
        }

        args->vals[args->count] = eval_arith_expr(p, ctx);
        args->names[args->count][0] = '\0';
        args->count++;
        if (p->cur.type == ETOK_COMMA) next_token(p);
    }
    expect(p, ETOK_RPAREN);
}

/* -------------------------------------------------------------------------
 * Primary parser — origin-centered primitives and variable refs
 * ---------------------------------------------------------------------- */
static void parse_vox_expr(EParser *p, VoxParseCtx *ctx, DC_Array *out);

static void parse_vox_primary(EParser *p, VoxParseCtx *ctx, DC_Array *out)
{
    if (p->has_error) return;

    if (ident_eq(&p->cur, "sphere")) {
        next_token(p);
        VoxArgs args;
        parse_vox_args(p, ctx, &args);

        DC_VoxOp op = { .type = DC_VOX_OP_SPHERE };
        /* sphere(r) or sphere(d=10) or sphere(r=5) */
        if (vox_arg_has(&args, "d")) {
            op.radius = vox_arg_find(&args, "d", 10) / 2.0;
        } else if (vox_arg_has(&args, "r")) {
            op.radius = vox_arg_find(&args, "r", 5);
        } else {
            op.radius = vox_arg_pos(&args, 0, 5);
        }
        /* Origin-centered */
        op.x = 0; op.y = 0; op.z = 0;
        dc_array_push(out, &op);

    } else if (ident_eq(&p->cur, "cube")) {
        next_token(p);
        VoxArgs args;
        parse_vox_args(p, ctx, &args);

        DC_VoxOp op = { .type = DC_VOX_OP_BOX };
        double sx, sy, sz;
        if (args.count >= 3) {
            sx = vox_arg_pos(&args, 0, 10);
            sy = vox_arg_pos(&args, 1, 10);
            sz = vox_arg_pos(&args, 2, 10);
        } else {
            sx = sy = sz = vox_arg_pos(&args, 0, 10);
        }
        op.x = -sx / 2; op.y = -sy / 2; op.z = -sz / 2;
        op.x2 = sx / 2; op.y2 = sy / 2; op.z2 = sz / 2;
        dc_array_push(out, &op);

    } else if (ident_eq(&p->cur, "cylinder")) {
        next_token(p);
        VoxArgs args;
        parse_vox_args(p, ctx, &args);

        DC_VoxOp op = { .type = DC_VOX_OP_CYLINDER };
        double h, r;
        if (vox_arg_has(&args, "h")) {
            h = vox_arg_find(&args, "h", 10);
        } else {
            h = vox_arg_pos(&args, 0, 10);
        }
        if (vox_arg_has(&args, "d")) {
            r = vox_arg_find(&args, "d", 10) / 2.0;
        } else if (vox_arg_has(&args, "r")) {
            r = vox_arg_find(&args, "r", 5);
        } else {
            r = vox_arg_pos(&args, 1, 5);
        }
        op.x = 0; op.y = 0;
        op.radius = r;
        op.z = -h / 2;       /* z0 */
        op.radius2 = h / 2;  /* z1 */
        dc_array_push(out, &op);

    } else if (ident_eq(&p->cur, "torus")) {
        next_token(p);
        VoxArgs args;
        parse_vox_args(p, ctx, &args);

        DC_VoxOp op = { .type = DC_VOX_OP_TORUS };
        double R = vox_arg_pos(&args, 0, 10);
        double r = vox_arg_pos(&args, 1, 3);
        if (vox_arg_has(&args, "R")) R = vox_arg_find(&args, "R", 10);
        if (vox_arg_has(&args, "r")) r = vox_arg_find(&args, "r", 3);
        op.x = 0; op.y = 0; op.z = 0;
        op.radius = R;
        op.radius2 = r;
        dc_array_push(out, &op);

    } else if (p->cur.type == ETOK_LPAREN) {
        /* Parenthesized sub-expression */
        next_token(p);
        parse_vox_expr(p, ctx, out);
        expect(p, ETOK_RPAREN);

    } else if (p->cur.type == ETOK_IDENT) {
        /* Variable reference */
        VoxVarDef *v = vox_ctx_find(ctx, p->cur.start, p->cur.len);
        if (v) {
            vox_copy_var_ops(out, v);
            next_token(p);
        } else {
            /* Unknown identifier — error */
            if (!p->has_error) {
                DC_SET_ERROR(p->err, DC_ERROR_EDA_PARSE,
                             "unknown voxel variable at pos %d", p->pos);
                p->has_error = 1;
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Pipe transform parser
 * ---------------------------------------------------------------------- */
static void parse_vox_pipe(EParser *p, VoxParseCtx *ctx, DC_Array *out)
{
    /* Parse primary into scratch buffer */
    DC_Array *scratch = dc_array_new(sizeof(DC_VoxOp));
    parse_vox_primary(p, ctx, scratch);

    if (p->has_error) { dc_array_free(scratch); return; }

    /* Collect >> transforms */
    DC_VoxOp xforms[32];
    int nxform = 0;

    while (p->cur.type == ETOK_PIPE && !p->has_error && nxform < 32) {
        next_token(p); /* skip >> */

        if (ident_eq(&p->cur, "move")) {
            next_token(p);
            VoxArgs args;
            parse_vox_args(p, ctx, &args);
            DC_VoxOp op = { .type = DC_VOX_OP_TRANSLATE };
            if (vox_arg_has(&args, "x") || vox_arg_has(&args, "y") || vox_arg_has(&args, "z")) {
                op.x = vox_arg_find(&args, "x", 0);
                op.y = vox_arg_find(&args, "y", 0);
                op.z = vox_arg_find(&args, "z", 0);
            } else {
                op.x = vox_arg_pos(&args, 0, 0);
                op.y = vox_arg_pos(&args, 1, 0);
                op.z = vox_arg_pos(&args, 2, 0);
            }
            xforms[nxform++] = op;

        } else if (ident_eq(&p->cur, "rotate")) {
            next_token(p);
            VoxArgs args;
            parse_vox_args(p, ctx, &args);
            DC_VoxOp op = { .type = DC_VOX_OP_ROTATE };
            if (vox_arg_has(&args, "x") || vox_arg_has(&args, "y") || vox_arg_has(&args, "z")) {
                op.x = vox_arg_find(&args, "x", 0);
                op.y = vox_arg_find(&args, "y", 0);
                op.z = vox_arg_find(&args, "z", 0);
            } else {
                op.x = vox_arg_pos(&args, 0, 0);
                op.y = vox_arg_pos(&args, 1, 0);
                op.z = vox_arg_pos(&args, 2, 0);
            }
            xforms[nxform++] = op;

        } else if (ident_eq(&p->cur, "scale")) {
            next_token(p);
            VoxArgs args;
            parse_vox_args(p, ctx, &args);
            DC_VoxOp op = { .type = DC_VOX_OP_SCALE };
            if (vox_arg_has(&args, "x") || vox_arg_has(&args, "y") || vox_arg_has(&args, "z")) {
                op.x = vox_arg_find(&args, "x", 1);
                op.y = vox_arg_find(&args, "y", 1);
                op.z = vox_arg_find(&args, "z", 1);
            } else if (args.count >= 3) {
                op.x = vox_arg_pos(&args, 0, 1);
                op.y = vox_arg_pos(&args, 1, 1);
                op.z = vox_arg_pos(&args, 2, 1);
            } else {
                double s = vox_arg_pos(&args, 0, 1);
                op.x = s; op.y = s; op.z = s;
            }
            xforms[nxform++] = op;

        } else if (ident_eq(&p->cur, "color")) {
            next_token(p);
            VoxArgs args;
            parse_vox_args(p, ctx, &args);
            DC_VoxOp op = { .type = DC_VOX_OP_COLOR };
            op.r = (uint8_t)vox_arg_pos(&args, 0, 180);
            op.g = (uint8_t)vox_arg_pos(&args, 1, 180);
            op.b = (uint8_t)vox_arg_pos(&args, 2, 180);
            /* Color is not a transform — emit directly before the primary */
            dc_array_push(out, &op);
            continue; /* don't add to xforms stack */

        } else if (ident_eq(&p->cur, "to_mesh")) {
            next_token(p);
            VoxArgs args;
            parse_vox_args(p, ctx, &args);
            DC_VoxOp tmop = { .type = DC_VOX_OP_TO_MESH };
            tmop.resolution = (int)vox_arg_pos(&args, 0, 4); /* patch_rows */
            tmop.x = vox_arg_pos(&args, 1, 4);               /* patch_cols */
            /* to_mesh is terminal — not a transform, handled below */
            xforms[nxform++] = tmop;
            break;

        } else {
            /* Unknown pipe target — skip */
            next_token(p);
            if (p->cur.type == ETOK_LPAREN) {
                next_token(p);
                while (p->cur.type != ETOK_RPAREN && p->cur.type != ETOK_EOF)
                    next_token(p);
                eat(p, ETOK_RPAREN);
            }
            continue;
        }
    }

    /* Emit: transforms in pipe order (push), then primary, then N POPs.
     * to_mesh is special: it goes AFTER everything as a terminal op. */
    DC_VoxOp *to_mesh_op = NULL;
    for (int i = 0; i < nxform; i++) {
        if (xforms[i].type == DC_VOX_OP_TO_MESH) {
            to_mesh_op = &xforms[i];
        } else {
            dc_array_push(out, &xforms[i]);
        }
    }

    vox_copy_ops(out, scratch);

    int pop_count = 0;
    for (int i = 0; i < nxform; i++) {
        if (xforms[i].type != DC_VOX_OP_TO_MESH)
            pop_count++;
    }
    for (int i = 0; i < pop_count; i++) {
        DC_VoxOp pop = { .type = DC_VOX_OP_POP_TRANSFORM };
        dc_array_push(out, &pop);
    }

    /* Emit to_mesh as the very last operation */
    if (to_mesh_op)
        dc_array_push(out, to_mesh_op);

    dc_array_free(scratch);
}

/* -------------------------------------------------------------------------
 * Expression parser — CSG operators with precedence climbing
 *
 * Precedence:
 *   >> (pipe)  — tightest, handled inside parse_vox_pipe
 *   &          — precedence 2
 *   + -        — precedence 1 (union/difference)
 * ---------------------------------------------------------------------- */
static void parse_vox_expr(EParser *p, VoxParseCtx *ctx, DC_Array *out)
{
    /* Parse left operand */
    DC_Array *left = dc_array_new(sizeof(DC_VoxOp));
    parse_vox_pipe(p, ctx, left);

    while (!p->has_error) {
        DC_VoxOpType csg_type;

        if (p->cur.type == ETOK_MINUS) {
            csg_type = DC_VOX_OP_SUBTRACT;
        } else if (p->cur.type == ETOK_PLUS) {
            csg_type = DC_VOX_OP_UNION;
        } else if (p->cur.type == ETOK_AMP) {
            csg_type = DC_VOX_OP_INTERSECT;
        } else {
            break;
        }
        next_token(p); /* consume operator */

        /* Parse right operand (at same or tighter precedence) */
        DC_Array *right = dc_array_new(sizeof(DC_VoxOp));

        if (csg_type == DC_VOX_OP_INTERSECT) {
            /* & binds tighter than +/- so parse only pipe level */
            parse_vox_pipe(p, ctx, right);
            /* But continue collecting & at this level */
            while (p->cur.type == ETOK_AMP && !p->has_error) {
                next_token(p);
                /* Wrap current (left & right) then intersect with next */
                DC_Array *combined = dc_array_new(sizeof(DC_VoxOp));
                DC_VoxOp gb = { .type = DC_VOX_OP_GROUP_BEGIN };
                DC_VoxOp ge = { .type = DC_VOX_OP_GROUP_END };
                dc_array_push(combined, &gb);
                vox_copy_ops(combined, left);
                dc_array_push(combined, &ge);
                DC_VoxOp csg = { .type = DC_VOX_OP_INTERSECT };
                dc_array_push(combined, &csg);
                dc_array_push(combined, &gb);
                vox_copy_ops(combined, right);
                dc_array_push(combined, &ge);
                dc_array_free(left);
                dc_array_free(right);
                left = combined;
                right = dc_array_new(sizeof(DC_VoxOp));
                parse_vox_pipe(p, ctx, right);
                csg_type = DC_VOX_OP_INTERSECT;
            }
        } else {
            /* For +/-, right side may contain & which binds tighter */
            parse_vox_pipe(p, ctx, right);
            /* Consume any & operators on the right side at higher precedence */
            while (p->cur.type == ETOK_AMP && !p->has_error) {
                next_token(p);
                DC_Array *rr = dc_array_new(sizeof(DC_VoxOp));
                parse_vox_pipe(p, ctx, rr);
                /* Wrap right & rr */
                DC_Array *combined = dc_array_new(sizeof(DC_VoxOp));
                DC_VoxOp gb = { .type = DC_VOX_OP_GROUP_BEGIN };
                DC_VoxOp ge = { .type = DC_VOX_OP_GROUP_END };
                dc_array_push(combined, &gb);
                vox_copy_ops(combined, right);
                dc_array_push(combined, &ge);
                DC_VoxOp icsg = { .type = DC_VOX_OP_INTERSECT };
                dc_array_push(combined, &icsg);
                dc_array_push(combined, &gb);
                vox_copy_ops(combined, rr);
                dc_array_push(combined, &ge);
                dc_array_free(right);
                dc_array_free(rr);
                right = combined;
            }
        }

        /* Wrap left and right in GROUP_BEGIN/END with CSG operator between */
        DC_Array *combined = dc_array_new(sizeof(DC_VoxOp));
        DC_VoxOp gb = { .type = DC_VOX_OP_GROUP_BEGIN };
        DC_VoxOp ge = { .type = DC_VOX_OP_GROUP_END };

        dc_array_push(combined, &gb);
        vox_copy_ops(combined, left);
        dc_array_push(combined, &ge);

        DC_VoxOp csg = { .type = csg_type };
        dc_array_push(combined, &csg);

        dc_array_push(combined, &gb);
        vox_copy_ops(combined, right);
        dc_array_push(combined, &ge);

        dc_array_free(left);
        dc_array_free(right);
        left = combined;
    }

    /* Copy final result to output */
    vox_copy_ops(out, left);
    dc_array_free(left);
}

/* -------------------------------------------------------------------------
 * Statement parser — settings, variables, for loops, expressions
 * ---------------------------------------------------------------------- */
static void parse_vox_statement(EParser *p, VoxParseCtx *ctx, DC_Array *vox_ops);

static void parse_vox_for(EParser *p, VoxParseCtx *ctx, DC_Array *vox_ops)
{
    /* for IDENT in [start:end] { body } */
    next_token(p); /* skip 'for' */

    char var_name[64] = {0};
    int vlen = p->cur.len < 63 ? p->cur.len : 63;
    memcpy(var_name, p->cur.start, (size_t)vlen);
    next_token(p); /* skip variable name */

    if (ident_eq(&p->cur, "in")) next_token(p);

    expect(p, ETOK_LBRACKET);
    double start = eval_arith_expr(p, ctx);
    expect(p, ETOK_COLON);
    double step = 1;
    double end = eval_arith_expr(p, ctx);
    if (p->cur.type == ETOK_COLON) {
        /* [start:step:end] */
        next_token(p);
        step = end;
        end = eval_arith_expr(p, ctx);
    }
    expect(p, ETOK_RBRACKET);

    /* Save parser position at body start */
    expect(p, ETOK_LBRACE);
    int body_start = p->pos;
    ETok body_tok = p->cur;

    /* Skip body to find end (for position restoration) */
    int depth = 1;
    while (depth > 0 && p->cur.type != ETOK_EOF) {
        if (p->cur.type == ETOK_LBRACE) depth++;
        if (p->cur.type == ETOK_RBRACE) depth--;
        if (depth > 0) next_token(p);
    }
    if (p->cur.type == ETOK_RBRACE) next_token(p);
    int after_body_pos = p->pos;
    ETok after_body_tok = p->cur;

    /* Save previous loop var state */
    char prev_var[64];
    double prev_val = 0;
    int prev_has = ctx->has_loop_var;
    if (prev_has) {
        memcpy(prev_var, ctx->loop_var, 64);
        prev_val = ctx->loop_val;
    }

    /* Set loop variable */
    memcpy(ctx->loop_var, var_name, 64);
    ctx->has_loop_var = 1;

    if (step == 0) step = 1;

    for (double i = start; (step > 0 ? i <= end : i >= end); i += step) {
        ctx->loop_val = i;

        /* Restore parser to body start */
        p->pos = body_start;
        p->cur = body_tok;

        /* Re-parse body for this iteration */
        while (p->cur.type != ETOK_RBRACE && p->cur.type != ETOK_EOF && !p->has_error) {
            parse_vox_statement(p, ctx, vox_ops);
        }
    }

    /* Restore parser to after the closing brace */
    p->pos = after_body_pos;
    p->cur = after_body_tok;

    /* Restore previous loop var */
    if (prev_has) {
        memcpy(ctx->loop_var, prev_var, 64);
        ctx->loop_val = prev_val;
    }
    ctx->has_loop_var = prev_has;
}

static void parse_vox_statement(EParser *p, VoxParseCtx *ctx, DC_Array *vox_ops)
{
    if (p->cur.type == ETOK_EOF || p->has_error) return;

    /* Settings — resolution / $vn / $vd */
    if (ident_eq(&p->cur, "resolution") || ident_eq(&p->cur, "$vn")) {
        next_token(p);
        eat(p, ETOK_EQ);
        DC_VoxOp op = { .type = DC_VOX_OP_SET_RESOLUTION };
        op.resolution = (int)eval_arith_expr(p, ctx);
        eat(p, ETOK_SEMI);
        dc_array_push(vox_ops, &op);
        return;
    }
    if (ident_eq(&p->cur, "$vd")) {
        /* Voxel density: voxels per mm → cell_size = 1.0 / vd */
        next_token(p);
        eat(p, ETOK_EQ);
        double vd = eval_arith_expr(p, ctx);
        if (vd < 0.1) vd = 0.1;
        eat(p, ETOK_SEMI);
        DC_VoxOp op = { .type = DC_VOX_OP_SET_CELL_SIZE };
        op.cell_size = (float)(1.0 / vd);
        dc_array_push(vox_ops, &op);
        return;
    }
    if (ident_eq(&p->cur, "cell_size")) {
        next_token(p);
        DC_VoxOp op = { .type = DC_VOX_OP_SET_CELL_SIZE };
        op.cell_size = (float)eval_arith_expr(p, ctx);
        eat(p, ETOK_SEMI);
        dc_array_push(vox_ops, &op);
        return;
    }

    /* For loop */
    if (ident_eq(&p->cur, "for")) {
        parse_vox_for(p, ctx, vox_ops);
        return;
    }

    /* Check for assignment: IDENT = expr ; */
    if (p->cur.type == ETOK_IDENT) {
        /* Peek ahead: is next token '='? */
        int save_pos = p->pos;
        ETok save_tok = p->cur;
        char name_buf[64] = {0};
        int nlen = p->cur.len < 63 ? p->cur.len : 63;
        memcpy(name_buf, p->cur.start, (size_t)nlen);
        int name_len = p->cur.len;
        next_token(p);

        if (p->cur.type == ETOK_EQ) {
            next_token(p); /* skip = */
            DC_Array *var_ops = dc_array_new(sizeof(DC_VoxOp));
            parse_vox_expr(p, ctx, var_ops);
            eat(p, ETOK_SEMI);

            VoxVarDef *v = vox_ctx_add(ctx, name_buf, name_len);
            if (v) {
                vox_copy_ops(v->ops, var_ops);
            }
            /* Also emit the ops directly — the last assignment of a variable
             * in expression position will be the final geometry */
            dc_array_free(var_ops);
            return;
        }

        /* Not an assignment — restore and fall through to expression */
        p->pos = save_pos;
        p->cur = save_tok;
    }

    /* Expression statement */
    parse_vox_expr(p, ctx, vox_ops);
    eat(p, ETOK_SEMI);
}

/* -------------------------------------------------------------------------
 * Top-level voxel parser entry point
 * ---------------------------------------------------------------------- */
/* Check if current position looks like a voxel statement (for top-level parsing) */
static int is_vox_statement_start(EParser *p, VoxParseCtx *ctx)
{
    if (p->cur.type == ETOK_LPAREN) return 1;
    if (p->cur.type != ETOK_IDENT) return 0;
    if (ident_eq(&p->cur, "sphere") || ident_eq(&p->cur, "cube") ||
        ident_eq(&p->cur, "cylinder") || ident_eq(&p->cur, "torus") ||
        ident_eq(&p->cur, "resolution") || ident_eq(&p->cur, "$vn") ||
        ident_eq(&p->cur, "$vd") || ident_eq(&p->cur, "cell_size") ||
        ident_eq(&p->cur, "for")) return 1;
    /* Variable reference or assignment */
    VoxVarDef *v = vox_ctx_find(ctx, p->cur.start, p->cur.len);
    if (v) return 1;
    /* Check for assignment (ident = ...) */
    int save_pos = p->pos;
    ETok save_tok = p->cur;
    next_token(p);
    int is_assign = (p->cur.type == ETOK_EQ);
    p->pos = save_pos;
    p->cur = save_tok;
    return is_assign;
}

static void parse_vox_statements(EParser *p, DC_Array *vox_ops)
{
    VoxParseCtx ctx;
    vox_ctx_init(&ctx);

    while (p->cur.type != ETOK_EOF && p->cur.type != ETOK_RBRACE && !p->has_error) {
        if (!is_vox_statement_start(p, &ctx)) break;
        parse_vox_statement(p, &ctx, vox_ops);
    }

    vox_ctx_free(&ctx);
}

/* Parse a voxel { ... } block */
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
    parse_vox_statements(p, vox_ops);
    if (p->cur.type == ETOK_RBRACE) next_token(p);
}

/* Check if current token starts a voxel statement */
static int is_vox_keyword(EParser *p)
{
    return ident_eq(&p->cur, "sphere") ||
           ident_eq(&p->cur, "cube") ||
           ident_eq(&p->cur, "cylinder") ||
           ident_eq(&p->cur, "torus") ||
           ident_eq(&p->cur, "resolution") ||
           ident_eq(&p->cur, "cell_size") ||
           ident_eq(&p->cur, "for");
}

/* Check if IDENT is followed by '=' (variable assignment) */
static int is_vox_assignment(EParser *p)
{
    if (p->cur.type != ETOK_IDENT) return 0;
    /* Look ahead past identifier for '=' */
    int save_pos = p->pos;
    ETok save_tok = p->cur;
    next_token(p);
    int result = (p->cur.type == ETOK_EQ);
    p->pos = save_pos;
    p->cur = save_tok;
    return result;
}

/* =========================================================================
 * Bezier mesh block parser: bezier_mesh { ... }
 * ========================================================================= */
static void parse_bezier_mesh_block(EParser *p, DC_Array *bmesh_ops)
{
    expect(p, ETOK_LBRACE);
    while (p->cur.type != ETOK_RBRACE && p->cur.type != ETOK_EOF && !p->has_error) {
        if (ident_eq(&p->cur, "sphere")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_BMeshOp op = { .type = DC_BMESH_OP_SPHERE };
            op.radius = 5.0;
            /* Parse args: sphere(5) or sphere(r=5) or sphere(radius=5) */
            while (p->cur.type != ETOK_RPAREN && p->cur.type != ETOK_EOF && !p->has_error) {
                if (p->cur.type == ETOK_NUMBER) {
                    op.radius = p->cur.num_val;
                    next_token(p);
                } else if (p->cur.type == ETOK_IDENT) {
                    /* named arg: skip name and = */
                    next_token(p);
                    eat(p, ETOK_EQ);
                    if (p->cur.type == ETOK_NUMBER) {
                        op.radius = p->cur.num_val;
                        next_token(p);
                    }
                }
                if (p->cur.type == ETOK_COMMA) next_token(p);
            }
            expect(p, ETOK_RPAREN);
            if (p->cur.type == ETOK_SEMI) next_token(p);
            dc_array_push(bmesh_ops, &op);

        } else if (ident_eq(&p->cur, "torus")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_BMeshOp op = { .type = DC_BMESH_OP_TORUS };
            op.radius = 5.0;
            op.radius2 = 2.0;
            if (p->cur.type == ETOK_NUMBER) {
                op.radius = p->cur.num_val;
                next_token(p);
                if (p->cur.type == ETOK_COMMA) {
                    next_token(p);
                    if (p->cur.type == ETOK_NUMBER) {
                        op.radius2 = p->cur.num_val;
                        next_token(p);
                    }
                }
            }
            expect(p, ETOK_RPAREN);
            if (p->cur.type == ETOK_SEMI) next_token(p);
            dc_array_push(bmesh_ops, &op);

        } else if (ident_eq(&p->cur, "box")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_BMeshOp op = { .type = DC_BMESH_OP_BOX };
            op.x = 10.0; op.y = 10.0; op.z = 10.0;
            if (p->cur.type == ETOK_NUMBER) {
                op.x = p->cur.num_val;
                next_token(p);
                if (p->cur.type == ETOK_COMMA) {
                    next_token(p);
                    if (p->cur.type == ETOK_NUMBER) {
                        op.y = p->cur.num_val;
                        next_token(p);
                    }
                    if (p->cur.type == ETOK_COMMA) {
                        next_token(p);
                        if (p->cur.type == ETOK_NUMBER) {
                            op.z = p->cur.num_val;
                            next_token(p);
                        }
                    }
                }
            }
            expect(p, ETOK_RPAREN);
            if (p->cur.type == ETOK_SEMI) next_token(p);
            dc_array_push(bmesh_ops, &op);

        } else if (ident_eq(&p->cur, "cylinder")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_BMeshOp op = { .type = DC_BMESH_OP_CYLINDER };
            op.radius = 5.0;   /* radius */
            op.y = 10.0;       /* height (stored in .y for apply_bmesh) */
            op.cols = 8;       /* segments */
            /* Parse args: cylinder(r,h) or cylinder(r=3, h=10) */
            while (p->cur.type != ETOK_RPAREN && p->cur.type != ETOK_EOF && !p->has_error) {
                if (p->cur.type == ETOK_NUMBER) {
                    /* positional: first=radius, second=height */
                    if (op.radius == 5.0) op.radius = p->cur.num_val;
                    else op.y = p->cur.num_val;
                    next_token(p);
                } else if (p->cur.type == ETOK_IDENT) {
                    /* named arg */
                    int is_r = ident_eq(&p->cur, "r") || ident_eq(&p->cur, "radius");
                    int is_h = ident_eq(&p->cur, "h") || ident_eq(&p->cur, "height");
                    int is_s = ident_eq(&p->cur, "segments");
                    next_token(p);
                    eat(p, ETOK_EQ);
                    if (p->cur.type == ETOK_NUMBER) {
                        if (is_r) op.radius = p->cur.num_val;
                        else if (is_h) op.y = p->cur.num_val;
                        else if (is_s) op.cols = (int)p->cur.num_val;
                        next_token(p);
                    }
                }
                if (p->cur.type == ETOK_COMMA) next_token(p);
            }
            expect(p, ETOK_RPAREN);
            if (p->cur.type == ETOK_SEMI) next_token(p);
            dc_array_push(bmesh_ops, &op);

        } else if (ident_eq(&p->cur, "grid")) {
            next_token(p);
            expect(p, ETOK_LPAREN);
            DC_BMeshOp op = { .type = DC_BMESH_OP_GRID };
            op.rows = 2;
            op.cols = 2;
            if (p->cur.type == ETOK_NUMBER) {
                op.rows = (int)p->cur.num_val;
                next_token(p);
                if (p->cur.type == ETOK_COMMA) {
                    next_token(p);
                    if (p->cur.type == ETOK_NUMBER) {
                        op.cols = (int)p->cur.num_val;
                        next_token(p);
                    }
                }
            }
            expect(p, ETOK_RPAREN);
            if (p->cur.type == ETOK_SEMI) next_token(p);
            dc_array_push(bmesh_ops, &op);

        } else if (ident_eq(&p->cur, "cp")) {
            /* cp[r][c] = [x, y, z]; */
            next_token(p);
            expect(p, ETOK_LBRACKET);
            DC_BMeshOp op = { .type = DC_BMESH_OP_SET_CP };
            op.rows = 0;
            op.cols = 0;
            if (p->cur.type == ETOK_NUMBER) {
                op.rows = (int)p->cur.num_val;
                next_token(p);
            }
            expect(p, ETOK_RBRACKET);
            expect(p, ETOK_LBRACKET);
            if (p->cur.type == ETOK_NUMBER) {
                op.cols = (int)p->cur.num_val;
                next_token(p);
            }
            expect(p, ETOK_RBRACKET);
            /* Support both = (absolute) and += (relative) */
            int is_relative = 0;
            if (p->cur.type == ETOK_PLUS) {
                is_relative = 1;
                next_token(p); /* skip + */
            }
            expect(p, ETOK_EQ);
            if (is_relative) op.type = DC_BMESH_OP_SET_CP; /* still SET_CP, but flagged */
            expect(p, ETOK_LBRACKET);
            op.x = op.y = op.z = 0.0;
            op.resolution = is_relative; /* reuse resolution field as relative flag */
            if (p->cur.type == ETOK_NUMBER || p->cur.type == ETOK_MINUS) {
                int sign = 1;
                if (p->cur.type == ETOK_MINUS) { sign = -1; next_token(p); }
                op.x = sign * p->cur.num_val; next_token(p);
                if (p->cur.type == ETOK_COMMA) next_token(p);
                sign = 1;
                if (p->cur.type == ETOK_MINUS) { sign = -1; next_token(p); }
                op.y = sign * p->cur.num_val; next_token(p);
                if (p->cur.type == ETOK_COMMA) next_token(p);
                sign = 1;
                if (p->cur.type == ETOK_MINUS) { sign = -1; next_token(p); }
                op.z = sign * p->cur.num_val; next_token(p);
            }
            expect(p, ETOK_RBRACKET);
            if (p->cur.type == ETOK_SEMI) next_token(p);
            dc_array_push(bmesh_ops, &op);

        } else if (ident_eq(&p->cur, "resolution")) {
            next_token(p);
            expect(p, ETOK_EQ);
            DC_BMeshOp op = { .type = DC_BMESH_OP_RESOLUTION };
            op.resolution = 64;
            if (p->cur.type == ETOK_NUMBER) {
                op.resolution = (int)p->cur.num_val;
                next_token(p);
            }
            if (p->cur.type == ETOK_SEMI) next_token(p);
            dc_array_push(bmesh_ops, &op);

        } else if (ident_eq(&p->cur, "view")) {
            next_token(p);
            expect(p, ETOK_EQ);
            DC_BMeshOp op = { .type = DC_BMESH_OP_VIEW };
            op.view_mode = 3;  /* default: both */
            if (ident_eq(&p->cur, "wireframe"))     { op.view_mode = 1; next_token(p); }
            else if (ident_eq(&p->cur, "voxel"))    { op.view_mode = 2; next_token(p); }
            else if (ident_eq(&p->cur, "both"))     { op.view_mode = 3; next_token(p); }
            else if (ident_eq(&p->cur, "none"))     { op.view_mode = 0; next_token(p); }
            if (p->cur.type == ETOK_SEMI) next_token(p);
            dc_array_push(bmesh_ops, &op);

        } else if (ident_eq(&p->cur, "projection")) {
            next_token(p);
            expect(p, ETOK_EQ);
            DC_BMeshOp op = { .type = DC_BMESH_OP_PROJECTION };
            op.view_mode = 0;  /* default: auto */
            if (ident_eq(&p->cur, "auto"))         { op.view_mode = 0; next_token(p); }
            else if (ident_eq(&p->cur, "xy"))      { op.view_mode = 1; next_token(p); }
            else if (ident_eq(&p->cur, "xz"))      { op.view_mode = 2; next_token(p); }
            else if (ident_eq(&p->cur, "yz"))       { op.view_mode = 3; next_token(p); }
            else if (ident_eq(&p->cur, "tangent"))  { op.view_mode = 4; next_token(p); }
            if (p->cur.type == ETOK_SEMI) next_token(p);
            dc_array_push(bmesh_ops, &op);

        } else {
            /* Skip unknown statement */
            next_token(p);
        }
    }
    if (p->cur.type == ETOK_RBRACE) next_token(p);
}

static void find_and_parse_blocks(EParser *p, DC_Array *sch_ops, DC_Array *pcb_ops, DC_Array *vox_ops, DC_Array *bmesh_ops)
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
        } else if (ident_eq(&p->cur, "to_solid")) {
            /* to_solid( bezier_mesh { ... } ) — parse inner mesh, flag for voxelization */
            next_token(p); /* skip to_solid */
            eat(p, ETOK_LPAREN);
            if (ident_eq(&p->cur, "bezier_mesh")) {
                next_token(p);
                parse_bezier_mesh_block(p, bmesh_ops);
            }
            eat(p, ETOK_RPAREN);
            if (p->cur.type == ETOK_SEMI) next_token(p);
            /* Signal: bmesh should be voxelized into vox output.
             * We store this as a special BMeshOp at the end. */
            {
                DC_BMeshOp op = { .type = DC_BMESH_OP_RESOLUTION };
                op.resolution = -1; /* sentinel: means to_solid */
                dc_array_push(bmesh_ops, &op);
            }
        } else if (ident_eq(&p->cur, "bezier_mesh")) {
            next_token(p);
            parse_bezier_mesh_block(p, bmesh_ops);
        } else if (is_vox_keyword(p) || is_vox_assignment(p)) {
            /* Top-level Cubeiform voxel statements — parse all as a batch
             * so variables persist across statements */
            parse_vox_statements(p, vox_ops);
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
    eda->bmesh_ops = dc_array_new(sizeof(DC_BMeshOp));
    if (!eda->sch_ops || !eda->pcb_ops || !eda->vox_ops || !eda->bmesh_ops) {
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
    find_and_parse_blocks(&p, eda->sch_ops, eda->pcb_ops, eda->vox_ops, eda->bmesh_ops);

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

    if (eda->bmesh_ops)
        dc_array_free(eda->bmesh_ops);

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

size_t dc_cubeiform_eda_bmesh_op_count(const DC_CubeiformEda *eda)
{
    return eda && eda->bmesh_ops ? dc_array_length(eda->bmesh_ops) : 0;
}

const DC_BMeshOp *dc_cubeiform_eda_get_bmesh_op(const DC_CubeiformEda *eda, size_t i)
{
    if (!eda || !eda->bmesh_ops || i >= dc_array_length(eda->bmesh_ops)) return NULL;
    return dc_array_get(eda->bmesh_ops, i);
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

/* =========================================================================
 * Apply — bezier mesh operations → ts_bezier_mesh
 * ========================================================================= */
static ts_bezier_mesh *
dc_cubeiform_eda_apply_bmesh(DC_CubeiformEda *eda, DC_Error *err)
{
    if (!eda || !eda->bmesh_ops || dc_array_length(eda->bmesh_ops) == 0)
        return NULL;

    ts_bezier_mesh *mesh = NULL;
    size_t n = dc_array_length(eda->bmesh_ops);

    for (size_t i = 0; i < n; i++) {
        DC_BMeshOp *op = dc_array_get(eda->bmesh_ops, i);
        switch (op->type) {
        case DC_BMESH_OP_SPHERE: {
            double r = op->radius > 0 ? op->radius : 5.0;
            ts_bezier_sphere s = { .center = ts_vec3_make(0, 0, 0), .radius = r };
            if (mesh) { ts_bezier_mesh_free(mesh); free(mesh); }
            mesh = malloc(sizeof(ts_bezier_mesh));
            *mesh = ts_bezier_mesh_from_sphere(&s);
            break;
        }
        case DC_BMESH_OP_TORUS: {
            double R = op->radius > 0 ? op->radius : 5.0;
            double r2 = op->radius2 > 0 ? op->radius2 : 1.5;
            int trows = op->rows > 0 ? op->rows : 4;
            int tcols = op->cols > 0 ? op->cols : 6;
            ts_bezier_torus t = ts_bezier_torus_new(
                ts_vec3_make(0, 0, 0), R, r2, trows, tcols);
            if (mesh) { ts_bezier_mesh_free(mesh); free(mesh); }
            mesh = malloc(sizeof(ts_bezier_mesh));
            *mesh = ts_bezier_mesh_from_torus(&t);
            ts_bezier_torus_free(&t);
            break;
        }
        case DC_BMESH_OP_BOX: {
            double sx = op->x > 0 ? op->x : 5.0;
            double sy = op->y > 0 ? op->y : 5.0;
            double sz = op->z > 0 ? op->z : 5.0;
            ts_vec3 mn = ts_vec3_make(-sx/2, -sy/2, -sz/2);
            ts_vec3 mx = ts_vec3_make(sx/2, sy/2, sz/2);
            if (mesh) { ts_bezier_mesh_free(mesh); free(mesh); }
            mesh = malloc(sizeof(ts_bezier_mesh));
            *mesh = ts_bezier_mesh_from_box(mn, mx);
            break;
        }
        case DC_BMESH_OP_CYLINDER: {
            double r = op->radius > 0 ? op->radius : 3.0;
            double h = op->y > 0 ? op->y : 10.0;
            int segs = op->cols > 0 ? op->cols : 4;
            if (mesh) { ts_bezier_mesh_free(mesh); free(mesh); }
            mesh = malloc(sizeof(ts_bezier_mesh));
            *mesh = ts_bezier_mesh_from_cylinder(r, 0.0, h, segs);
            break;
        }
        case DC_BMESH_OP_GRID: {
            int rows = op->rows > 0 ? op->rows : 2;
            int cols = op->cols > 0 ? op->cols : 2;
            if (mesh) { ts_bezier_mesh_free(mesh); free(mesh); }
            mesh = malloc(sizeof(ts_bezier_mesh));
            *mesh = ts_bezier_mesh_new(rows, cols);
            break;
        }
        case DC_BMESH_OP_SET_CP: {
            if (mesh) {
                int cr = op->rows, cc = op->cols;
                if (op->resolution) {
                    /* Relative: += adds to existing CP position */
                    ts_vec3 cur = ts_bezier_mesh_get_cp(mesh, cr, cc);
                    ts_bezier_mesh_set_cp(mesh, cr, cc,
                        ts_vec3_add(cur, ts_vec3_make(op->x, op->y, op->z)));
                } else {
                    /* Absolute: = sets CP position directly */
                    ts_bezier_mesh_set_cp(mesh, cr, cc,
                                           ts_vec3_make(op->x, op->y, op->z));
                }
            }
            break;
        }
        default:
            break;
        }
    }

    if (!mesh) {
        DC_SET_ERROR(err, DC_ERROR_EDA_PARSE, "bezier_mesh block has no geometry");
    }

    return mesh;
}

int
dc_cubeiform_execute_full(const char *dcad_src,
                            DC_ESchematic *sch,
                            DC_EPcb *pcb,
                            DC_VoxelGrid **vox_out,
                            void **bmesh_out,
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

    if (eda->bmesh_ops && dc_array_length(eda->bmesh_ops) > 0) {
        /* Check for to_solid sentinel: last bmesh_op with resolution == -1 */
        int want_to_solid = 0;
        {
            size_t n = dc_array_length(eda->bmesh_ops);
            if (n > 0) {
                DC_BMeshOp *last = dc_array_get(eda->bmesh_ops, n - 1);
                if (last->type == DC_BMESH_OP_RESOLUTION && last->resolution == -1) {
                    want_to_solid = 1;
                    last->resolution = 0; /* neutralize sentinel — apply_bmesh will skip it */
                }
            }
        }

        /* For to_solid: if the mesh is a pristine primitive (no CP edits),
         * use analytical SDF via apply_voxel instead of mesh voxelization.
         * This gives perfect results for known shapes. */
        int used_analytical = 0;
        if (want_to_solid && vox_out) {
            size_t n = dc_array_length(eda->bmesh_ops);
            int has_cp_edits = 0;
            DC_BMeshOp *prim_op = NULL;
            for (size_t i = 0; i < n; i++) {
                DC_BMeshOp *op = dc_array_get(eda->bmesh_ops, i);
                if (op->type == DC_BMESH_OP_SET_CP) has_cp_edits = 1;
                if (op->type == DC_BMESH_OP_SPHERE || op->type == DC_BMESH_OP_BOX ||
                    op->type == DC_BMESH_OP_CYLINDER || op->type == DC_BMESH_OP_TORUS)
                    prim_op = op;
            }
            if (prim_op && !has_cp_edits) {
                /* Convert to VoxOps and use apply_voxel for analytical SDF */
                DC_VoxOp vop = {0};
                vop.r = 180; vop.g = 180; vop.b = 180;
                switch (prim_op->type) {
                case DC_BMESH_OP_SPHERE:
                    vop.type = DC_VOX_OP_SPHERE;
                    vop.radius = prim_op->radius > 0 ? prim_op->radius : 5.0;
                    break;
                case DC_BMESH_OP_BOX: {
                    vop.type = DC_VOX_OP_BOX;
                    double hx = (prim_op->x > 0 ? prim_op->x : 10.0) * 0.5;
                    double hy = (prim_op->y > 0 ? prim_op->y : 10.0) * 0.5;
                    double hz = (prim_op->z > 0 ? prim_op->z : 10.0) * 0.5;
                    vop.x = -hx; vop.y = -hy; vop.z = -hz;
                    vop.x2 = hx; vop.y2 = hy; vop.z2 = hz;
                    break;
                }
                case DC_BMESH_OP_CYLINDER:
                    vop.type = DC_VOX_OP_CYLINDER;
                    vop.radius = prim_op->radius > 0 ? prim_op->radius : 3.0;
                    vop.z = 0;
                    vop.radius2 = prim_op->y > 0 ? prim_op->y : 10.0;
                    break;
                case DC_BMESH_OP_TORUS:
                    vop.type = DC_VOX_OP_TORUS;
                    vop.radius = prim_op->radius > 0 ? prim_op->radius : 5.0;
                    vop.radius2 = prim_op->radius2 > 0 ? prim_op->radius2 : 1.5;
                    break;
                default: break;
                }
                if (vop.type != 0) {
                    /* Build a temporary vox_ops array and use apply_voxel */
                    dc_array_push(eda->vox_ops, &vop);
                    *vox_out = dc_cubeiform_eda_apply_voxel(eda, err);
                    used_analytical = 1;
                }
            }
        }

        ts_bezier_mesh *mesh = used_analytical ? NULL : dc_cubeiform_eda_apply_bmesh(eda, err);
        if (mesh && want_to_solid && !used_analytical) {
            /* Edited mesh to_solid: pass mesh out via bmesh_out.
             * The caller (do_render) handles async voxelization. */
            if (bmesh_out) {
                *bmesh_out = mesh;
            } else {
                /* No bmesh_out — must voxelize synchronously */
                *vox_out = dc_voxelize_bezier(mesh, 64, 2, 15, err);
                ts_bezier_mesh_free(mesh);
                free(mesh);
            }
        } else if (mesh && bmesh_out) {
            *bmesh_out = mesh;
        } else if (mesh) {
            ts_bezier_mesh_free(mesh);
            free(mesh);
        }
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

    /* Check if there are any actual primitives (not just settings).
     * Without primitives, there's nothing to voxelize — bail early. */
    {
        size_t n = dc_array_length(eda->vox_ops);
        int has_prim = 0;
        for (size_t i = 0; i < n && !has_prim; i++) {
            DC_VoxOp *op = dc_array_get(eda->vox_ops, i);
            if (op->type == DC_VOX_OP_SPHERE || op->type == DC_VOX_OP_BOX ||
                op->type == DC_VOX_OP_CYLINDER || op->type == DC_VOX_OP_TORUS)
                has_prim = 1;
        }
        if (!has_prim) return NULL;
    }

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
    if (resolution > 4096) resolution = 4096;

    /* If no primitives contributed to the bounding box, bail out.
     * This happens when vox_ops only contain settings ($vd, $vn, etc.)
     * with no actual geometry (sphere, cube, etc.). */
    if (bmin[0] > bmax[0] || bmin[1] > bmax[1] || bmin[2] > bmax[2])
        return NULL;

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

    /* Snap bmin so world origin (0,0,0) falls on a cell center.
     * Cell center = bmin + (ix+0.5)*cell_size.  For origin to hit a center:
     *   0 = bmin + (n+0.5)*cell_size  =>  bmin = -(n+0.5)*cell_size
     * Pick n = floor(-bmin/cell_size - 0.5) to keep bmin close to original. */
    for (int a = 0; a < 3; a++) {
        float n = floorf(-bmin[a] / cell_size - 0.5f);
        bmin[a] = -(n + 0.5f) * cell_size;
    }
    /* Recompute extent after snapping */
    for (int a = 0; a < 3; a++) extent[a] = bmax[a] - bmin[a];

    int sx = (int)ceilf(extent[0] / cell_size) + 1;
    int sy = (int)ceilf(extent[1] / cell_size) + 1;
    int sz = (int)ceilf(extent[2] / cell_size) + 1;
    if (sx > 4096) sx = 4096;
    if (sy > 4096) sy = 4096;
    if (sz > 4096) sz = 4096;

    DC_VoxelGrid *grid = dc_voxel_grid_new(sx, sy, sz, cell_size);
    if (!grid) {
        DC_SET_ERROR(err, DC_ERROR_MEMORY, "voxel grid alloc");
        return NULL;
    }

    /* Store world-space origin so the renderer can position the grid correctly.
     * bmin is where grid cell (0,0,0) maps to in world space. */
    dc_voxel_grid_set_origin(grid, bmin[0], bmin[1], bmin[2]);

    dc_log(DC_LOG_INFO, DC_LOG_EVENT_EDA,
           "voxel grid: bmin=(%.2f,%.2f,%.2f) bmax=(%.2f,%.2f,%.2f) "
           "size=%dx%dx%d cell=%.4f",
           bmin[0], bmin[1], bmin[2], bmax[0], bmax[1], bmax[2],
           sx, sy, sz, cell_size);

    /* Offset: grid coords = world coords - bmin.
     * All SDF primitives must be shifted by -bmin. */
    #define OX(v) ((float)(v) - bmin[0])
    #define OY(v) ((float)(v) - bmin[1])
    #define OZ(v) ((float)(v) - bmin[2])

    /* Grid stack for CSG groups.
     * Op layout: GROUP_BEGIN left GROUP_END CSG_OP GROUP_BEGIN right GROUP_END
     * - GROUP_BEGIN: push new empty grid
     * - GROUP_END: if CSG pending AND sp >= 2, pop two, combine, push result
     * - CSG_OP: set pending flag (between two groups)
     * - Primitives: render into grid_stack[grid_sp] */
    #define MAX_GRID_STACK 16
    DC_VoxelGrid *grid_stack[MAX_GRID_STACK];
    int grid_sp = 0;
    int csg_pending_type = 0; /* 0=none, 1=subtract, 2=intersect, 3=union */

    grid_stack[0] = grid;

    /* Reset transform stack for second pass */
    xform_depth = 0;
    dc_sdf_transform_identity(&xform_stack[0]);

    for (size_t i = 0; i < nops; i++) {
        DC_VoxOp *op = dc_array_get(eda->vox_ops, i);

        switch (op->type) {
        case DC_VOX_OP_SET_RESOLUTION:
        case DC_VOX_OP_SET_CELL_SIZE:
            break;

        case DC_VOX_OP_GROUP_BEGIN:
            if (grid_sp + 1 < MAX_GRID_STACK) {
                grid_sp++;
                grid_stack[grid_sp] = dc_voxel_grid_new(sx, sy, sz, cell_size);
            }
            break;

        case DC_VOX_OP_GROUP_END:
            /* If CSG pending and we have at least 2 grids, combine */
            if (csg_pending_type && grid_sp >= 2) {
                DC_VoxelGrid *right_g = grid_stack[grid_sp];
                grid_sp--;
                DC_VoxelGrid *left_g = grid_stack[grid_sp];

                DC_VoxelGrid *out = dc_voxel_grid_new(sx, sy, sz, cell_size);
                if (out) {
                    if (csg_pending_type == 1) dc_sdf_subtract(left_g, right_g, out);
                    else if (csg_pending_type == 2) dc_sdf_intersect(left_g, right_g, out);
                    else dc_sdf_union(left_g, right_g, out);
                    dc_voxel_grid_free(left_g);
                    grid_stack[grid_sp] = out;
                }
                dc_voxel_grid_free(right_g);
                csg_pending_type = 0;
            }
            break;

        case DC_VOX_OP_SUBTRACT:  csg_pending_type = 1; break;
        case DC_VOX_OP_INTERSECT: csg_pending_type = 2; break;
        case DC_VOX_OP_UNION:     csg_pending_type = 3; break;

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

        case DC_VOX_OP_TO_MESH:
            /* Terminal op — handled by caller (inspect.c), not here */
            break;

        case DC_VOX_OP_SPHERE:
        case DC_VOX_OP_BOX:
        case DC_VOX_OP_CYLINDER:
        case DC_VOX_OP_TORUS: {
            DC_VoxelGrid *target = grid_stack[grid_sp];

            /* Build the full transform: grid offset (bmin shift) + user transform. */
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
            break;
        }
        }
    }

    /* Collapse remaining grid stack — merge into base grid */
    while (grid_sp > 0) {
        DC_VoxelGrid *top = grid_stack[grid_sp];
        grid_sp--;
        /* Union remaining grids into base */
        dc_sdf_union(grid_stack[grid_sp], top, grid_stack[grid_sp]);
        dc_voxel_grid_free(top);
    }

    grid = grid_stack[0];
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
