#define _POSIX_C_SOURCE 200809L
/*
 * cubeiform.c — Cubeiform-to-OpenSCAD transpiler.
 *
 * Single-pass recursive descent parser that tokenizes Cubeiform source
 * and emits equivalent OpenSCAD. Key transformations:
 *   - Pipe ">>" reversal with transform keyword mapping
 *   - CSG operators (+, -, &) at statement level
 *   - shape→module, fn→function, for..in→for(..=)
 *   - Special variables: fn→$fn, fa→$fa, fs→$fs
 *   - Named axis transforms: move(x=10) → translate([10,0,0])
 */

#include "cubeiform/cubeiform.h"
#include "core/string_builder.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Token types
 * ========================================================================= */
typedef enum {
    TOK_EOF = 0,
    TOK_NUM, TOK_STR, TOK_IDENT,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT, TOK_CARET,
    TOK_AMP,        /* & */
    TOK_PIPE,       /* >> */
    TOK_EQ,         /* = */
    TOK_EQEQ,      /* == */
    TOK_NEQ,        /* != */
    TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_AND,        /* && */
    TOK_OR,         /* || */
    TOK_NOT,        /* ! */
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_SEMI, TOK_COMMA, TOK_COLON, TOK_DOT, TOK_QMARK,
    TOK_HASH,       /* # (modifier) */
    /* Keywords */
    TOK_SHAPE, TOK_FN, TOK_FOR, TOK_IN, TOK_IF, TOK_ELSE,
    TOK_LET, TOK_INCLUDE, TOK_USE, TOK_ASSERT, TOK_ECHO,
    TOK_TRUE, TOK_FALSE, TOK_UNDEF,
} TokType;

typedef struct {
    TokType     type;
    const char *start;
    int         len;
    int         line;
} Token;

/* =========================================================================
 * Tokenizer
 * ========================================================================= */
#define MAX_TOKENS 65536

static TokType
keyword_type(const char *s, int len)
{
    #define KW(str, tok) if (len == (int)sizeof(str)-1 && memcmp(s,str,len)==0) return tok
    KW("shape",   TOK_SHAPE);
    KW("fn",      TOK_FN);
    KW("for",     TOK_FOR);
    KW("in",      TOK_IN);
    KW("if",      TOK_IF);
    KW("else",    TOK_ELSE);
    KW("let",     TOK_LET);
    KW("include", TOK_INCLUDE);
    KW("use",     TOK_USE);
    KW("assert",  TOK_ASSERT);
    KW("echo",    TOK_ECHO);
    KW("true",    TOK_TRUE);
    KW("false",   TOK_FALSE);
    KW("undef",   TOK_UNDEF);
    #undef KW
    return TOK_IDENT;
}

static Token *
tokenize(const char *src, int *out_count)
{
    Token *toks = malloc(MAX_TOKENS * sizeof(Token));
    if (!toks) return NULL;
    int n = 0;
    int line = 1;
    const char *p = src;

    while (*p && n < MAX_TOKENS - 1) {
        /* Skip whitespace */
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
            if (*p == '\n') line++;
            p++;
        }
        if (!*p) break;

        /* Skip line comments */
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }
        /* Skip block comments */
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n') line++;
                p++;
            }
            if (*p) p += 2;
            continue;
        }

        Token t = { .start = p, .line = line };

        /* Numbers */
        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
            while (isdigit((unsigned char)*p)) p++;
            if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
            if (*p == 'e' || *p == 'E') {
                p++;
                if (*p == '+' || *p == '-') p++;
                while (isdigit((unsigned char)*p)) p++;
            }
            t.type = TOK_NUM;
            t.len = (int)(p - t.start);
            toks[n++] = t;
            continue;
        }

        /* Strings */
        if (*p == '"') {
            p++;
            t.start = p;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                p++;
            }
            t.type = TOK_STR;
            t.len = (int)(p - t.start);
            if (*p == '"') p++;
            toks[n++] = t;
            continue;
        }

        /* Identifiers and keywords */
        if (isalpha((unsigned char)*p) || *p == '_' || *p == '$') {
            while (isalnum((unsigned char)*p) || *p == '_' || *p == '$') p++;
            t.len = (int)(p - t.start);
            t.type = keyword_type(t.start, t.len);
            toks[n++] = t;
            continue;
        }

        /* Multi-character operators */
        t.len = 1;
        switch (*p) {
        case '>':
            if (p[1] == '>') { t.type = TOK_PIPE; t.len = 2; p += 2; }
            else if (p[1] == '=') { t.type = TOK_GE; t.len = 2; p += 2; }
            else { t.type = TOK_GT; p++; }
            break;
        case '<':
            if (p[1] == '=') { t.type = TOK_LE; t.len = 2; p += 2; }
            else { t.type = TOK_LT; p++; }
            break;
        case '=':
            if (p[1] == '=') { t.type = TOK_EQEQ; t.len = 2; p += 2; }
            else { t.type = TOK_EQ; p++; }
            break;
        case '!':
            if (p[1] == '=') { t.type = TOK_NEQ; t.len = 2; p += 2; }
            else { t.type = TOK_NOT; p++; }
            break;
        case '&':
            if (p[1] == '&') { t.type = TOK_AND; t.len = 2; p += 2; }
            else { t.type = TOK_AMP; p++; }
            break;
        case '|':
            if (p[1] == '|') { t.type = TOK_OR; t.len = 2; p += 2; }
            else { p++; continue; } /* bare | not used */
            break;
        case '+': t.type = TOK_PLUS;     p++; break;
        case '-': t.type = TOK_MINUS;    p++; break;
        case '*': t.type = TOK_STAR;     p++; break;
        case '/': t.type = TOK_SLASH;    p++; break;
        case '%': t.type = TOK_PERCENT;  p++; break;
        case '^': t.type = TOK_CARET;    p++; break;
        case '(': t.type = TOK_LPAREN;   p++; break;
        case ')': t.type = TOK_RPAREN;   p++; break;
        case '{': t.type = TOK_LBRACE;   p++; break;
        case '}': t.type = TOK_RBRACE;   p++; break;
        case '[': t.type = TOK_LBRACKET; p++; break;
        case ']': t.type = TOK_RBRACKET; p++; break;
        case ';': t.type = TOK_SEMI;     p++; break;
        case ',': t.type = TOK_COMMA;    p++; break;
        case ':': t.type = TOK_COLON;    p++; break;
        case '.': t.type = TOK_DOT;      p++; break;
        case '?': t.type = TOK_QMARK;    p++; break;
        case '#': t.type = TOK_HASH;     p++; break;
        default:
            /* Skip multi-byte UTF-8 sequences properly */
            if ((unsigned char)*p >= 0xC0) {
                /* UTF-8 lead byte: skip continuation bytes */
                p++;
                while (*p && ((unsigned char)*p & 0xC0) == 0x80) p++;
            } else {
                p++;
            }
            continue;
        }
        toks[n++] = t;
    }

    toks[n].type = TOK_EOF;
    toks[n].start = p;
    toks[n].len = 0;
    toks[n].line = line;
    *out_count = n;
    return toks;
}

/* =========================================================================
 * Parser state
 * ========================================================================= */
#define MAX_GEO_VARS 512

typedef struct {
    char *name;
    char *scad;     /* transpiled OpenSCAD for this geometry variable */
} GeoVar;

typedef struct {
    Token           *toks;
    int              count;
    int              pos;
    DC_StringBuilder *out;
    int              indent;
    /* Geometry variable tracking for CSG inlining */
    GeoVar           geo_vars[MAX_GEO_VARS];
    int              geo_count;
    int              error;
    char             errmsg[256];
} Parser;

/* ---- Token helpers ---- */
static Token peek(Parser *p) { return p->toks[p->pos]; }
static Token peek2(Parser *p) {
    return (p->pos + 1 < p->count) ? p->toks[p->pos + 1] : p->toks[p->count];
}
static Token advance(Parser *p) { return p->toks[p->pos++]; }
static int at(Parser *p, TokType t) { return peek(p).type == t; }
static int at_end(Parser *p) { return peek(p).type == TOK_EOF; }

static int tok_eq(Token t, const char *s) {
    return (int)strlen(s) == t.len && memcmp(t.start, s, (size_t)t.len) == 0;
}

static char *tok_str(Token t) {
    char *s = malloc((size_t)t.len + 1);
    if (s) { memcpy(s, t.start, (size_t)t.len); s[t.len] = '\0'; }
    return s;
}

/* Check if a string ends with '}' (block — no trailing semicolon in OpenSCAD) */
static int ends_with_brace(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\n' || s[len-1] == '\t'))
        len--;
    return len > 0 && s[len-1] == '}';
}

/* ---- Output helpers ---- */
static void emit(Parser *p, const char *s) { dc_sb_append(p->out, s); }
static void emitf(Parser *p, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dc_sb_append(p->out, buf);
}
static void emit_tok(Parser *p, Token t) {
    char *s = tok_str(t);
    if (s) { emit(p, s); free(s); }
}
static void emit_indent(Parser *p) {
    for (int i = 0; i < p->indent; i++) emit(p, "    ");
}
static void emit_nl(Parser *p) { emit(p, "\n"); }

/* ---- Geometry variable tracking ---- */
static void geo_var_add(Parser *p, const char *name, const char *scad) {
    if (p->geo_count >= MAX_GEO_VARS) return;
    p->geo_vars[p->geo_count].name = strdup(name);
    p->geo_vars[p->geo_count].scad = strdup(scad);
    p->geo_count++;
}

static const char *geo_var_find(Parser *p, const char *name) {
    for (int i = p->geo_count - 1; i >= 0; i--)
        if (strcmp(p->geo_vars[i].name, name) == 0)
            return p->geo_vars[i].scad;
    return NULL;
}

/* ---- Known geometry primitives ---- */
static int is_geo_primitive(const char *name) {
    static const char *prims[] = {
        "cube", "sphere", "cylinder", "circle", "square", "polygon",
        "polyhedron", "text", "tetrahedron", "octahedron", "dodecahedron",
        "icosahedron", "hull", "minkowski", "union", "difference",
        "intersection", "import", "surface", "linear_extrude",
        "rotate_extrude", "projection", NULL
    };
    for (int i = 0; prims[i]; i++)
        if (strcmp(name, prims[i]) == 0) return 1;
    return 0;
}

/* Is this identifier a special variable that needs $ prefix? */
static int is_special_var(const char *name, int len) {
    return (len == 2 && (memcmp(name, "fn", 2) == 0 ||
                         memcmp(name, "fa", 2) == 0 ||
                         memcmp(name, "fs", 2) == 0));
}

/* =========================================================================
 * Forward declarations
 * ========================================================================= */
static void parse_statement(Parser *p);
static void parse_block(Parser *p);
static void emit_value_expr(Parser *p);
static void emit_pipe_expr(Parser *p);
static char *capture_geo_expr(Parser *p);

/* =========================================================================
 * Value expression parser — arithmetic context (inside parens, args, etc.)
 * ========================================================================= */

/* Emit a vector/list literal: [a, b, c] or [for ...] */
static void emit_vector(Parser *p) {
    emit(p, "[");
    advance(p); /* [ */
    /* Check for list comprehension: [for ...] */
    if (at(p, TOK_FOR)) {
        advance(p); /* for */
        /* Emit: for (ident = range) ... */
        emit(p, "for (");
        if (at(p, TOK_IDENT)) { emit_tok(p, advance(p)); }
        emit(p, " = ");
        if (at(p, TOK_IN)) advance(p);
        emit_value_expr(p);
        emit(p, ") ");
        /* Optional condition: if (cond) */
        if (at(p, TOK_IF)) {
            advance(p);
            emit(p, "if ");
            emit_value_expr(p);
            emit(p, " ");
        }
        emit_value_expr(p);
    } else {
        int first = 1;
        while (!at(p, TOK_RBRACKET) && !at_end(p)) {
            if (!first) emit(p, ", ");
            first = 0;
            emit_value_expr(p);
            if (at(p, TOK_COMMA)) advance(p);
            /* Handle range: [start:step:end] or [start:end] */
            if (at(p, TOK_COLON)) {
                emit(p, ":");
                advance(p);
                emit_value_expr(p);
                if (at(p, TOK_COLON)) {
                    emit(p, ":");
                    advance(p);
                    emit_value_expr(p);
                }
            }
        }
    }
    if (at(p, TOK_RBRACKET)) advance(p);
    emit(p, "]");
}

/* Emit a function call's argument list (inside parens) */
static void emit_args(Parser *p) {
    emit(p, "(");
    advance(p); /* ( */
    int first = 1;
    int depth = 1;
    while (depth > 0 && !at_end(p)) {
        if (at(p, TOK_RPAREN)) {
            depth--;
            if (depth == 0) { advance(p); break; }
        }
        if (at(p, TOK_LPAREN)) depth++;
        if (!first && at(p, TOK_COMMA) && depth == 1) {
            emit(p, ", ");
            advance(p);
            first = 0;
            continue;
        }
        first = 0;
        /* fn/fa/fs as named args → $fn/$fa/$fs in OpenSCAD */
        if (depth == 1 && peek2(p).type == TOK_EQ) {
            Token peeked = peek(p);
            if ((peeked.type == TOK_FN) ||
                (peeked.type == TOK_IDENT && is_special_var(peeked.start, peeked.len))) {
                emit(p, "$");
            }
        }
        emit_value_expr(p);
        if (at(p, TOK_COMMA) && depth == 1) {
            emit(p, ", ");
            advance(p);
        }
    }
    emit(p, ")");
}

/* Primary value expression */
static void emit_value_primary(Parser *p) {
    Token t = peek(p);

    if (t.type == TOK_NUM) {
        emit_tok(p, advance(p));
    } else if (t.type == TOK_STR) {
        emit(p, "\"");
        emit_tok(p, advance(p));
        emit(p, "\"");
    } else if (t.type == TOK_TRUE) {
        advance(p); emit(p, "true");
    } else if (t.type == TOK_FALSE) {
        advance(p); emit(p, "false");
    } else if (t.type == TOK_UNDEF) {
        advance(p); emit(p, "undef");
    } else if (t.type == TOK_LBRACKET) {
        emit_vector(p);
    } else if (t.type == TOK_LPAREN) {
        emit(p, "(");
        advance(p);
        emit_value_expr(p);
        if (at(p, TOK_RPAREN)) { advance(p); emit(p, ")"); }
    } else if (t.type == TOK_MINUS) {
        advance(p); emit(p, "-");
        emit_value_primary(p);
    } else if (t.type == TOK_NOT) {
        advance(p); emit(p, "!");
        emit_value_primary(p);
    } else if (t.type == TOK_FN) {
        /* fn used as identifier in value context (e.g. fn=64 in args) */
        advance(p);
        emit(p, "fn");
    } else if (t.type == TOK_IDENT) {
        Token name = advance(p);
        /* Special variable mapping */
        if (is_special_var(name.start, name.len)) {
            emit(p, "$");
        }
        /* PI → PI (already supported), but check for function call */
        emit_tok(p, name);
        /* Function call */
        if (at(p, TOK_LPAREN)) {
            emit_args(p);
        }
        /* Index: x[i] */
        while (at(p, TOK_LBRACKET)) {
            emit_vector(p);
        }
    } else {
        /* Skip unknown token */
        if (!at_end(p)) { emit_tok(p, advance(p)); }
    }
}

/* Value expression with precedence climbing */
static void emit_value_expr(Parser *p) {
    emit_value_primary(p);
    for (;;) {
        Token t = peek(p);
        if (t.type == TOK_PLUS)    { advance(p); emit(p, " + ");  emit_value_primary(p); }
        else if (t.type == TOK_MINUS)   { advance(p); emit(p, " - ");  emit_value_primary(p); }
        else if (t.type == TOK_STAR)    { advance(p); emit(p, " * ");  emit_value_primary(p); }
        else if (t.type == TOK_SLASH)   { advance(p); emit(p, " / ");  emit_value_primary(p); }
        else if (t.type == TOK_PERCENT) { advance(p); emit(p, " % ");  emit_value_primary(p); }
        else if (t.type == TOK_CARET) {
            /* ^ → pow() */
            advance(p);
            emit(p, " /* ^ */ "); /* TODO: proper pow() transform */
            emit_value_primary(p);
        }
        else if (t.type == TOK_EQEQ) { advance(p); emit(p, " == "); emit_value_primary(p); }
        else if (t.type == TOK_NEQ)   { advance(p); emit(p, " != "); emit_value_primary(p); }
        else if (t.type == TOK_LT)    { advance(p); emit(p, " < ");  emit_value_primary(p); }
        else if (t.type == TOK_GT)    { advance(p); emit(p, " > ");  emit_value_primary(p); }
        else if (t.type == TOK_LE)    { advance(p); emit(p, " <= "); emit_value_primary(p); }
        else if (t.type == TOK_GE)    { advance(p); emit(p, " >= "); emit_value_primary(p); }
        else if (t.type == TOK_AND)   { advance(p); emit(p, " && "); emit_value_primary(p); }
        else if (t.type == TOK_OR)    { advance(p); emit(p, " || "); emit_value_primary(p); }
        else if (t.type == TOK_QMARK) {
            advance(p); emit(p, " ? ");
            emit_value_expr(p);
            if (at(p, TOK_COLON)) { advance(p); emit(p, " : "); }
            emit_value_expr(p);
        }
        else break;
    }
}

/* =========================================================================
 * Transform mapping — converts Cubeiform pipe transforms to OpenSCAD
 * ========================================================================= */

/* Parse named args like (x=10, y=5) and build [x, y, z] vector.
 * Returns malloc'd string. Handles both positional and named forms. */
static char *parse_axis_args(Parser *p, const char *defaults[3]) {
    /* Collect arguments */
    if (!at(p, TOK_LPAREN)) return strdup("");
    advance(p); /* ( */

    char *vals[3];
    vals[0] = strdup(defaults[0]);
    vals[1] = strdup(defaults[1]);
    vals[2] = strdup(defaults[2]);

    int positional = 0;
    while (!at(p, TOK_RPAREN) && !at_end(p)) {
        /* Check for named: x=expr, y=expr, z=expr */
        if (at(p, TOK_IDENT) && peek2(p).type == TOK_EQ) {
            Token name = advance(p);
            advance(p); /* = */
            /* Capture value expression into temp buffer */
            DC_StringBuilder *tmp = dc_sb_new();
            DC_StringBuilder *save = p->out;
            p->out = tmp;
            emit_value_expr(p);
            p->out = save;
            char *val = dc_sb_take(tmp);
            dc_sb_free(tmp);

            if (tok_eq(name, "x"))      { free(vals[0]); vals[0] = val; }
            else if (tok_eq(name, "y")) { free(vals[1]); vals[1] = val; }
            else if (tok_eq(name, "z")) { free(vals[2]); vals[2] = val; }
            else free(val);
        } else {
            /* Positional */
            DC_StringBuilder *tmp = dc_sb_new();
            DC_StringBuilder *save = p->out;
            p->out = tmp;
            emit_value_expr(p);
            p->out = save;
            char *val = dc_sb_take(tmp);
            dc_sb_free(tmp);

            if (positional < 3) { free(vals[positional]); vals[positional] = val; }
            else free(val);
            positional++;
        }
        if (at(p, TOK_COMMA)) advance(p);
    }
    if (at(p, TOK_RPAREN)) advance(p);

    /* Build result */
    char buf[512];
    snprintf(buf, sizeof(buf), "[%s, %s, %s]", vals[0], vals[1], vals[2]);
    free(vals[0]); free(vals[1]); free(vals[2]);
    return strdup(buf);
}

/* Parse sweep/revolve args and emit OpenSCAD equivalent */
static void emit_sweep_args(Parser *p) {
    if (!at(p, TOK_LPAREN)) { emit(p, "()"); return; }
    advance(p); /* ( */
    emit(p, "(");
    int first = 1;
    int positional = 0;
    while (!at(p, TOK_RPAREN) && !at_end(p)) {
        if (!first) emit(p, ", ");
        first = 0;

        /* Check for named args and map them */
        if (at(p, TOK_IDENT) && peek2(p).type == TOK_EQ) {
            Token name = advance(p);
            advance(p); /* = */
            if (tok_eq(name, "h")) emit(p, "height = ");
            else if (tok_eq(name, "fn")) emit(p, "$fn = ");
            else { emit_tok(p, name); emit(p, " = "); }
            emit_value_expr(p);
        } else {
            /* First positional arg for sweep is height */
            if (positional == 0) emit(p, "height = ");
            emit_value_expr(p);
            positional++;
        }
        if (at(p, TOK_COMMA)) advance(p);
    }
    if (at(p, TOK_RPAREN)) advance(p);
    emit(p, ")");
}

/* Parse revolve args and emit OpenSCAD */
static void emit_revolve_args(Parser *p) {
    if (!at(p, TOK_LPAREN)) { emit(p, "()"); return; }
    advance(p); /* ( */
    emit(p, "(");
    int first = 1;
    while (!at(p, TOK_RPAREN) && !at_end(p)) {
        if (!first) emit(p, ", ");
        first = 0;
        if (at(p, TOK_IDENT) && peek2(p).type == TOK_EQ) {
            Token name = advance(p);
            advance(p);
            if (tok_eq(name, "fn")) emit(p, "$fn = ");
            else { emit_tok(p, name); emit(p, " = "); }
            emit_value_expr(p);
        } else {
            emit_value_expr(p);
        }
        if (at(p, TOK_COMMA)) advance(p);
    }
    if (at(p, TOK_RPAREN)) advance(p);
    emit(p, ")");
}

/* Emit a single pipe transform to the output */
static void emit_transform(Parser *p, Token name, int saved_pos) {
    int restore_pos = p->pos; /* save current position */
    p->pos = saved_pos;       /* rewind to transform args */

    const char *zero3[] = {"0", "0", "0"};
    const char *one3[]  = {"1", "1", "1"};

    if (tok_eq(name, "move")) {
        char *vec = parse_axis_args(p, zero3);
        emitf(p, "translate(%s) ", vec);
        free(vec);
    } else if (tok_eq(name, "rotate")) {
        /* Check for axis-angle: rotate(a=30, v=[1,1,0]) */
        /* For now, treat as Euler angles */
        char *vec = parse_axis_args(p, zero3);
        emitf(p, "rotate(%s) ", vec);
        free(vec);
    } else if (tok_eq(name, "scale")) {
        /* Check if single uniform arg */
        char *vec = parse_axis_args(p, one3);
        emitf(p, "scale(%s) ", vec);
        free(vec);
    } else if (tok_eq(name, "mirror")) {
        char *vec = parse_axis_args(p, zero3);
        emitf(p, "mirror(%s) ", vec);
        free(vec);
    } else if (tok_eq(name, "color")) {
        emit(p, "color");
        if (at(p, TOK_LPAREN)) emit_args(p);
        emit(p, " ");
    } else if (tok_eq(name, "sweep")) {
        emit(p, "linear_extrude");
        emit_sweep_args(p);
        emit(p, " ");
    } else if (tok_eq(name, "revolve")) {
        emit(p, "rotate_extrude");
        emit_revolve_args(p);
        emit(p, " ");
    } else if (tok_eq(name, "project")) {
        emit(p, "projection");
        if (at(p, TOK_LPAREN)) emit_args(p);
        emit(p, " ");
    } else if (tok_eq(name, "matrix")) {
        emit(p, "multmatrix");
        if (at(p, TOK_LPAREN)) emit_args(p);
        emit(p, " ");
    } else {
        /* Unknown transform — pass through */
        emit_tok(p, name);
        if (at(p, TOK_LPAREN)) emit_args(p);
        emit(p, " ");
    }

    /* Restore position to after the transform was fully parsed */
    /* Actually, parsing the args above already advanced p->pos.
     * The caller needs to know where we ended up. */
    (void)restore_pos;
}

/* =========================================================================
 * Geometry expression parser — handles pipes and CSG at statement level
 * ========================================================================= */

/* Capture a geometry expression into a fresh string buffer.
 * This is used for CSG operands that need to be inlined.
 * Returns malloc'd string. */
static char *capture_geo_term(Parser *p);

/* Emit a single geometry primary (a primitive call, identifier, or parens group) */
static void emit_geo_primary(Parser *p) {
    Token t = peek(p);

    if (t.type == TOK_HASH) {
        advance(p); emit(p, "# ");
        emit_geo_primary(p);
        return;
    }

    if (t.type == TOK_LPAREN) {
        /* Parenthesized geometry expression */
        advance(p);
        /* This is a geo context — CSG operators work here */
        emit(p, "(");
        /* For now, just emit as value expr — CSG in parens is complex */
        emit_value_expr(p);
        if (at(p, TOK_RPAREN)) { advance(p); emit(p, ")"); }
        return;
    }

    if (t.type == TOK_IDENT) {
        Token name = advance(p);

        /* Check if it's a known geometry variable → will be inlined by CSG handler */
        if (!at(p, TOK_LPAREN) && !at(p, TOK_LBRACE)) {
            /* Bare identifier — might be a geometry variable or regular var */
            char *name_s = tok_str(name);
            const char *geo = geo_var_find(p, name_s);
            (void)geo; /* Inlining handled at CSG level */
            free(name_s);
            if (is_special_var(name.start, name.len)) emit(p, "$");
            emit_tok(p, name);
            return;
        }

        /* hull/minkowski with arguments → convert to children block */
        if ((tok_eq(name, "hull") || tok_eq(name, "minkowski")) &&
            at(p, TOK_LPAREN) && peek2(p).type != TOK_RPAREN) {
            /* hull(a, b, c) → hull() { a; b; c; } */
            emit_tok(p, name);
            advance(p); /* ( */
            emit(p, "() {\n");
            p->indent++;
            while (!at(p, TOK_RPAREN) && !at_end(p)) {
                emit_indent(p);
                emit_pipe_expr(p);
                emit(p, ";\n");
                if (at(p, TOK_COMMA)) advance(p);
            }
            p->indent--;
            if (at(p, TOK_RPAREN)) advance(p);
            emit_indent(p);
            emit(p, "}");
            return;
        }

        /* Function/shape call */
        emit_tok(p, name);
        if (at(p, TOK_LPAREN)) emit_args(p);

        /* Optional children block: shape_call() { ... } */
        if (at(p, TOK_LBRACE)) {
            emit(p, " ");
            parse_block(p);
        }
        return;
    }

    /* Fall through to value expression for numbers, etc. */
    emit_value_expr(p);
}

/* Parse a pipe expression: primary >> transform >> transform ...
 * Transforms are emitted in reverse (outermost first in OpenSCAD). */
static void emit_pipe_expr(Parser *p) {
    /* Collect transforms in a buffer, then emit in reverse */
    typedef struct { Token name; int args_pos; char *emitted; } XForm;
    XForm xforms[64];
    int nxf = 0;

    /* First, we need to capture the base expression */
    /* Parse base and collect pipe chain */
    /* Strategy: parse base into a temp buffer, then parse transforms into
     * their own temp buffers, then emit transforms reversed + base */

    DC_StringBuilder *base_sb = dc_sb_new();
    DC_StringBuilder *save = p->out;
    p->out = base_sb;
    emit_geo_primary(p);
    p->out = save;

    while (at(p, TOK_PIPE) && nxf < 64) {
        advance(p); /* >> */
        if (!at(p, TOK_IDENT)) break;
        Token name = advance(p);

        /* Capture the transform's OpenSCAD output */
        DC_StringBuilder *xf_sb = dc_sb_new();
        p->out = xf_sb;
        emit_transform(p, name, p->pos);
        p->out = save;

        xforms[nxf].name = name;
        xforms[nxf].emitted = dc_sb_take(xf_sb);
        dc_sb_free(xf_sb);
        nxf++;
    }

    /* Emit transforms in reverse order, then base */
    for (int i = nxf - 1; i >= 0; i--) {
        emit(p, xforms[i].emitted);
        free(xforms[i].emitted);
    }
    char *base = dc_sb_take(base_sb);
    dc_sb_free(base_sb);
    emit(p, base);
    free(base);
}

/* Capture a geo term (pipe expression) into a malloc'd string */
static char *capture_geo_term(Parser *p) {
    DC_StringBuilder *sb = dc_sb_new();
    DC_StringBuilder *save = p->out;
    p->out = sb;
    emit_pipe_expr(p);
    p->out = save;
    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}

/* Parse a full geometry expression with CSG operators and emit it.
 * Returns 1 if this was a CSG expression, 0 otherwise. */
static int emit_geo_expr_full(Parser *p) {
    char *first = capture_geo_term(p);

    /* Check for CSG operators */
    if (at(p, TOK_PLUS) || at(p, TOK_MINUS) || at(p, TOK_AMP)) {
        /* Collect all CSG terms */
        typedef struct { int op; char *term; } CsgTerm;
        CsgTerm terms[128];
        int nterms = 0;
        terms[nterms].op = 0; /* first term has no operator */
        terms[nterms].term = first;
        nterms++;

        while ((at(p, TOK_PLUS) || at(p, TOK_MINUS) || at(p, TOK_AMP)) && nterms < 128) {
            int op = peek(p).type;
            advance(p);
            terms[nterms].op = op;
            terms[nterms].term = capture_geo_term(p);
            nterms++;
        }

        /* Emit CSG operations. Chain left-to-right:
         * a - b - c → difference() { a; difference() { ... } }
         * Actually, for chains of the same op, we can group:
         * a - b - c → difference() { a; b; c; }
         * But mixed ops need nesting. For simplicity, handle chains. */

        if (nterms == 2) {
            /* Simple binary CSG */
            const char *csg = "union()";
            if (terms[1].op == TOK_MINUS) csg = "difference()";
            else if (terms[1].op == TOK_AMP) csg = "intersection()";
            emitf(p, "%s {\n", csg);
            p->indent++;
            /* Inline geometry variables */
            for (int i = 0; i < nterms; i++) {
                emit_indent(p);
                const char *geo = geo_var_find(p, terms[i].term);
                const char *val = geo ? geo : terms[i].term;
                emit(p, val);
                if (ends_with_brace(val)) emit(p, "\n");
                else emit(p, ";\n");
            }
            p->indent--;
            emit_indent(p);
            emit(p, "}");
        } else {
            /* Multi-term: group consecutive same-op terms.
             * For mixed, nest. Simplified: emit as left-associative. */
            /* Start with first term, then wrap each operator */
            /* a - b - c + d → union() { difference() { a; b; c; }; d; }
             * This requires a more complex approach. For now, group same-op runs. */

            /* Simple approach: process left-to-right, grouping runs of same op */
            int i = 1;
            char *accumulated = strdup(terms[0].term);

            while (i < nterms) {
                int op = terms[i].op;
                const char *csg = "union()";
                if (op == TOK_MINUS) csg = "difference()";
                else if (op == TOK_AMP) csg = "intersection()";

                /* Collect all consecutive terms with same operator */
                DC_StringBuilder *inner = dc_sb_new();
                dc_sb_appendf(inner, "%s {\n", csg);
                /* First operand: the accumulated result */
                const char *geo = geo_var_find(p, accumulated);
                const char *val = geo ? geo : accumulated;
                if (ends_with_brace(val))
                    dc_sb_appendf(inner, "    %s\n", val);
                else
                    dc_sb_appendf(inner, "    %s;\n", val);

                while (i < nterms && terms[i].op == op) {
                    geo = geo_var_find(p, terms[i].term);
                    val = geo ? geo : terms[i].term;
                    if (ends_with_brace(val))
                        dc_sb_appendf(inner, "    %s\n", val);
                    else
                        dc_sb_appendf(inner, "    %s;\n", val);
                    i++;
                }
                dc_sb_append(inner, "}");
                free(accumulated);
                accumulated = dc_sb_take(inner);
                dc_sb_free(inner);
            }
            emit(p, accumulated);
            free(accumulated);
        }

        for (int i = 0; i < nterms; i++) free(terms[i].term);
        return 1;
    }

    /* Not CSG — just emit the single term */
    /* Check if it's a geometry variable reference that should be inlined */
    const char *geo = geo_var_find(p, first);
    const char *val = geo ? geo : first;
    emit(p, val);
    int is_block = ends_with_brace(val);
    free(first);
    return is_block;  /* treat block-ending terms like CSG (no trailing ;) */
}

/* Capture a full geo expression into a malloc'd string */
static char *capture_geo_expr(Parser *p) {
    DC_StringBuilder *sb = dc_sb_new();
    DC_StringBuilder *save = p->out;
    p->out = sb;
    emit_geo_expr_full(p);
    p->out = save;
    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}

/* =========================================================================
 * Statement parser
 * ========================================================================= */
static void parse_block(Parser *p) {
    emit(p, "{\n");
    if (at(p, TOK_LBRACE)) advance(p);
    p->indent++;
    while (!at(p, TOK_RBRACE) && !at_end(p)) {
        emit_indent(p);
        parse_statement(p);
    }
    p->indent--;
    if (at(p, TOK_RBRACE)) advance(p);
    emit_indent(p);
    emit(p, "}");
}

static void parse_statement(Parser *p) {
    if (p->error || at_end(p)) return;
    Token t = peek(p);

    /* --- shape name(...) { } → module name(...) { } --- */
    if (t.type == TOK_SHAPE) {
        advance(p);
        emit(p, "module ");
        if (at(p, TOK_IDENT)) emit_tok(p, advance(p));
        if (at(p, TOK_LPAREN)) emit_args(p);
        emit(p, " ");
        if (at(p, TOK_LBRACE)) parse_block(p);
        emit_nl(p);
        return;
    }

    /* --- fn = 64; → $fn = 64; (special variable assignment) --- */
    if (t.type == TOK_FN && peek2(p).type == TOK_EQ) {
        advance(p); /* fn */
        advance(p); /* = */
        emit(p, "$fn = ");
        emit_value_expr(p);
        if (at(p, TOK_SEMI)) advance(p);
        emit(p, ";\n");
        return;
    }

    /* --- fn name(...) = expr; → function name(...) = expr; --- */
    if (t.type == TOK_FN && peek2(p).type == TOK_IDENT) {
        advance(p); /* fn */
        Token name = peek(p);
        if (name.type == TOK_IDENT && peek2(p).type == TOK_LPAREN) {
            emit(p, "function ");
            emit_tok(p, advance(p)); /* name */
            if (at(p, TOK_LPAREN)) emit_args(p);
            if (at(p, TOK_EQ)) { advance(p); emit(p, " = "); }
            emit_value_expr(p);
            if (at(p, TOK_SEMI)) advance(p);
            emit(p, ";\n");
            return;
        }
        p->pos--; /* unconsume fn */
    }

    /* --- include "file"; → include <file> --- */
    if (t.type == TOK_INCLUDE) {
        advance(p);
        emit(p, "include ");
        if (at(p, TOK_STR)) {
            Token path = advance(p);
            /* Check if .dcad → would need recursive transpile.
             * For now, emit as-is. */
            emit(p, "<");
            emit_tok(p, path);
            emit(p, ">");
        }
        if (at(p, TOK_SEMI)) advance(p);
        emit(p, "\n");
        return;
    }

    /* --- use "file"; → use <file> --- */
    if (t.type == TOK_USE) {
        advance(p);
        emit(p, "use ");
        if (at(p, TOK_STR)) {
            Token path = advance(p);
            emit(p, "<");
            emit_tok(p, path);
            emit(p, ">");
        }
        if (at(p, TOK_SEMI)) advance(p);
        emit(p, "\n");
        return;
    }

    /* --- for ident in expr { } → for (ident = expr) { } --- */
    if (t.type == TOK_FOR) {
        advance(p);
        emit(p, "for (");
        if (at(p, TOK_IDENT)) emit_tok(p, advance(p));
        if (at(p, TOK_IN)) advance(p);
        emit(p, " = ");
        emit_value_expr(p);
        emit(p, ") ");
        if (at(p, TOK_LBRACE)) parse_block(p);
        emit_nl(p);
        return;
    }

    /* --- if (cond) { } else { } → pass through --- */
    if (t.type == TOK_IF) {
        advance(p);
        emit(p, "if ");
        if (at(p, TOK_LPAREN)) {
            emit(p, "(");
            advance(p);
            emit_value_expr(p);
            if (at(p, TOK_RPAREN)) { advance(p); emit(p, ")"); }
        }
        emit(p, " ");
        if (at(p, TOK_LBRACE)) parse_block(p);
        if (at(p, TOK_ELSE)) {
            advance(p);
            emit(p, " else ");
            if (at(p, TOK_LBRACE)) parse_block(p);
            else if (at(p, TOK_IF)) { parse_statement(p); return; }
        }
        emit_nl(p);
        return;
    }

    /* --- let (...) { } → let (...) { } --- */
    if (t.type == TOK_LET) {
        advance(p);
        emit(p, "let ");
        if (at(p, TOK_LPAREN)) emit_args(p);
        emit(p, " ");
        if (at(p, TOK_LBRACE)) parse_block(p);
        emit_nl(p);
        return;
    }

    /* --- assert(...); / echo(...); → pass through --- */
    if (t.type == TOK_ASSERT || t.type == TOK_ECHO) {
        emit_tok(p, advance(p));
        if (at(p, TOK_LPAREN)) emit_args(p);
        if (at(p, TOK_SEMI)) advance(p);
        emit(p, ";\n");
        return;
    }

    /* --- Assignment: ident = expr; --- */
    if (t.type == TOK_IDENT && peek2(p).type == TOK_EQ) {
        Token name = advance(p);
        advance(p); /* = */

        /* Check for special variables */
        if (is_special_var(name.start, name.len)) {
            emit(p, "$");
            emit_tok(p, name);
            emit(p, " = ");
            emit_value_expr(p);
            if (at(p, TOK_SEMI)) advance(p);
            emit(p, ";\n");
            return;
        }

        /* Determine if RHS is geometry or value.
         * Geometry if: starts with known primitive, or contains >> or CSG */
        Token rhs_start = peek(p);
        char *rhs_name = NULL;
        int is_geo = 0;

        if (rhs_start.type == TOK_IDENT) {
            rhs_name = tok_str(rhs_start);
            if (is_geo_primitive(rhs_name) || geo_var_find(p, rhs_name))
                is_geo = 1;
        }

        /* Scan ahead for >> or CSG operators at top level */
        if (!is_geo) {
            int depth = 0;
            for (int i = p->pos; i < p->count; i++) {
                if (p->toks[i].type == TOK_LPAREN || p->toks[i].type == TOK_LBRACKET ||
                    p->toks[i].type == TOK_LBRACE) depth++;
                else if (p->toks[i].type == TOK_RPAREN || p->toks[i].type == TOK_RBRACKET ||
                         p->toks[i].type == TOK_RBRACE) depth--;
                else if (p->toks[i].type == TOK_SEMI) break;
                else if (depth == 0 && p->toks[i].type == TOK_PIPE) { is_geo = 1; break; }
            }
        }

        if (is_geo) {
            /* Geometry assignment → capture and store, don't emit */
            char *scad = capture_geo_expr(p);
            char *var_name = tok_str(name);
            geo_var_add(p, var_name, scad);
            free(scad);
            free(var_name);
            if (at(p, TOK_SEMI)) advance(p);
            /* Emit a comment so the user knows where it went */
            emitf(p, "/* %.*s = ... (geometry variable) */\n", name.len, name.start);
        } else {
            /* Regular value assignment */
            emit_tok(p, name);
            emit(p, " = ");
            emit_value_expr(p);
            if (at(p, TOK_SEMI)) advance(p);
            emit(p, ";\n");
        }
        free(rhs_name);
        return;
    }

    /* --- Expression statement (geometry + possible pipes + CSG) --- */
    int was_csg = emit_geo_expr_full(p);
    if (at(p, TOK_SEMI)) advance(p);
    if (was_csg)
        emit(p, "\n");  /* CSG blocks already have closing brace */
    else
        emit(p, ";\n");
}

/* =========================================================================
 * Public API
 * ========================================================================= */
char *
dc_cubeiform_to_scad(const char *dcad_src, DC_Error *err)
{
    if (!dcad_src) {
        if (err) { err->code = DC_ERROR_INVALID_ARG; snprintf(err->message, sizeof(err->message), "NULL source"); }
        return NULL;
    }

    int tok_count = 0;
    Token *toks = tokenize(dcad_src, &tok_count);
    if (!toks) {
        if (err) { err->code = DC_ERROR_MEMORY; snprintf(err->message, sizeof(err->message), "tokenize OOM"); }
        return NULL;
    }

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) {
        free(toks);
        if (err) { err->code = DC_ERROR_MEMORY; snprintf(err->message, sizeof(err->message), "sb OOM"); }
        return NULL;
    }

    Parser parser = {
        .toks = toks,
        .count = tok_count,
        .pos = 0,
        .out = sb,
        .indent = 0,
        .geo_count = 0,
        .error = 0,
    };

    /* Parse all statements */
    while (!at_end(&parser) && !parser.error) {
        parse_statement(&parser);
    }

    char *result = NULL;
    if (parser.error) {
        if (err) {
            err->code = DC_ERROR_PARSE;
            snprintf(err->message, sizeof(err->message), "%s", parser.errmsg);
        }
    } else {
        result = dc_sb_take(sb);
    }

    /* Cleanup */
    for (int i = 0; i < parser.geo_count; i++) {
        free(parser.geo_vars[i].name);
        free(parser.geo_vars[i].scad);
    }
    dc_sb_free(sb);
    free(toks);
    return result;
}
