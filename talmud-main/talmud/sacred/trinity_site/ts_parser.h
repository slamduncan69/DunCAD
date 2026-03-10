/*
 * ts_parser.h — OpenSCAD recursive-descent parser
 *
 * Parses token stream from ts_lexer into an AST.
 * Supports: primitives, transforms, CSG, extrusion, variables,
 * expressions, if/else, for, module/function definitions.
 *
 * Memory: all AST nodes are heap-allocated. Call ts_ast_free() on root.
 */
#ifndef TS_PARSER_H
#define TS_PARSER_H

#include "ts_lexer.h"

/* ================================================================
 * AST NODE TYPES
 * ================================================================ */
typedef enum {
    TS_AST_NUMBER,
    TS_AST_STRING,
    TS_AST_BOOL,
    TS_AST_UNDEF,
    TS_AST_IDENT,
    TS_AST_VECTOR,
    TS_AST_RANGE,
    TS_AST_BINARY,
    TS_AST_UNARY,
    TS_AST_TERNARY,
    TS_AST_INDEX,
    TS_AST_FUNC_CALL,
    TS_AST_ASSIGN,
    TS_AST_MODULE_INST,   /* cube(...) {...}, translate(...) {...}, etc. */
    TS_AST_MODULE_DEF,
    TS_AST_FUNCTION_DEF,
    TS_AST_IF,
    TS_AST_FOR,
    TS_AST_BLOCK,
    TS_AST_ECHO,
    TS_AST_INCLUDE,
    TS_AST_USE,
    TS_AST_LIST_COMP,  /* [for (var = range) expr] */
    TS_AST_LET,        /* let(x=1, y=2) expr */
} ts_ast_type;

/* ================================================================
 * AST NODE
 * ================================================================ */
typedef struct ts_ast ts_ast;

/* Named argument: name=expr (name NULL for positional) */
typedef struct {
    char   *name;
    ts_ast *value;
} ts_arg;

struct ts_ast {
    ts_ast_type type;
    int         line;

    /* Value fields */
    double      num_val;
    char       *str_val;     /* heap-owned: IDENT, STRING, MODULE_INST name, etc. */
    int         bool_val;

    /* Operator for BINARY/UNARY */
    ts_tok_type op;

    /* Arguments (for MODULE_INST, FUNC_CALL) */
    ts_arg     *args;
    int         arg_count;
    int         arg_cap;

    /* Children (block statements, CSG children) */
    ts_ast    **children;
    int         child_count;
    int         child_cap;

    /* For expressions: left, right, condition */
    ts_ast     *left;
    ts_ast     *right;
    ts_ast     *cond;

    /* For IF: then_body, else_body */
    ts_ast     *then_body;
    ts_ast     *else_body;

    /* For FOR: iter_var, iter_expr, body */
    char       *iter_var;
    ts_ast     *iter_expr;
    ts_ast     *body;

    /* For MODULE_DEF/FUNCTION_DEF: name, params with defaults, body/expr */
    char      **def_params;
    ts_ast    **def_defaults;
    int         def_param_count;
    ts_ast     *def_body;     /* module body */
    ts_ast     *def_expr;     /* function expression */

    /* For INCLUDE/USE */
    char       *path;

    /* Modifier characters: !, *, #, % */
    char        modifier;
};

/* ================================================================
 * PARSE ERROR
 * ================================================================ */
typedef struct {
    int  line;
    char msg[256];
} ts_parse_error;

/* ================================================================
 * FORWARD DECLARATIONS
 * ================================================================ */
static ts_ast *ts_parse_statement(ts_lexer *lex, ts_parse_error *err);
static ts_ast *ts_parse_expr(ts_lexer *lex, ts_parse_error *err);

/* ================================================================
 * AST ALLOCATION AND FREEING
 * ================================================================ */
static inline ts_ast *ts_ast_new(ts_ast_type type, int line) {
    ts_ast *n = (ts_ast *)calloc(1, sizeof(ts_ast));
    if (n) { n->type = type; n->line = line; }
    return n;
}

static inline void ts_ast_add_child(ts_ast *parent, ts_ast *child) {
    if (parent->child_count >= parent->child_cap) {
        int newcap = parent->child_cap ? parent->child_cap * 2 : 4;
        ts_ast **nc = (ts_ast **)realloc(parent->children,
                                          (size_t)newcap * sizeof(ts_ast *));
        if (!nc) return;
        parent->children = nc;
        parent->child_cap = newcap;
    }
    parent->children[parent->child_count++] = child;
}

static inline void ts_ast_add_arg(ts_ast *node, char *name, ts_ast *value) {
    if (node->arg_count >= node->arg_cap) {
        int newcap = node->arg_cap ? node->arg_cap * 2 : 4;
        ts_arg *na = (ts_arg *)realloc(node->args,
                                        (size_t)newcap * sizeof(ts_arg));
        if (!na) return;
        node->args = na;
        node->arg_cap = newcap;
    }
    node->args[node->arg_count].name = name;
    node->args[node->arg_count].value = value;
    node->arg_count++;
}

static void ts_ast_free(ts_ast *n) {
    if (!n) return;
    free(n->str_val);
    free(n->iter_var);
    free(n->path);
    for (int i = 0; i < n->arg_count; i++) {
        free(n->args[i].name);
        ts_ast_free(n->args[i].value);
    }
    free(n->args);
    for (int i = 0; i < n->child_count; i++)
        ts_ast_free(n->children[i]);
    free(n->children);
    ts_ast_free(n->left);
    ts_ast_free(n->right);
    ts_ast_free(n->cond);
    ts_ast_free(n->then_body);
    ts_ast_free(n->else_body);
    ts_ast_free(n->iter_expr);
    ts_ast_free(n->body);
    ts_ast_free(n->def_body);
    ts_ast_free(n->def_expr);
    for (int i = 0; i < n->def_param_count; i++) {
        free(n->def_params[i]);
        ts_ast_free(n->def_defaults[i]);
    }
    free(n->def_params);
    free(n->def_defaults);
    free(n);
}

/* ================================================================
 * ERROR HELPER
 * ================================================================ */
static inline void ts_parse_err(ts_parse_error *err, int line,
                                 const char *msg) {
    if (err && !err->msg[0]) {
        err->line = line;
        snprintf(err->msg, sizeof(err->msg), "%s", msg);
    }
}

/* ================================================================
 * EXPECT TOKEN
 * ================================================================ */
static inline ts_token ts_expect(ts_lexer *lex, ts_tok_type type,
                                  ts_parse_error *err) {
    ts_token t = ts_lexer_next(lex);
    if (t.type != type) {
        char buf[256];
        snprintf(buf, sizeof(buf), "line %d: unexpected token (expected %d, got %d)",
                 t.line, type, t.type);
        ts_parse_err(err, t.line, buf);
    }
    return t;
}

/* ================================================================
 * EXPRESSION PARSING (Pratt-style precedence climbing)
 * ================================================================ */

/* Primary expressions */
static ts_ast *ts_parse_primary(ts_lexer *lex, ts_parse_error *err) {
    ts_token t = ts_lexer_peek(lex);

    /* Number */
    if (t.type == TS_TOK_NUMBER) {
        ts_lexer_next(lex);
        ts_ast *n = ts_ast_new(TS_AST_NUMBER, t.line);
        n->num_val = t.num_val;
        return n;
    }

    /* String */
    if (t.type == TS_TOK_STRING) {
        ts_lexer_next(lex);
        ts_ast *n = ts_ast_new(TS_AST_STRING, t.line);
        n->str_val = ts_tok_strdup(t);
        return n;
    }

    /* Boolean */
    if (t.type == TS_TOK_TRUE || t.type == TS_TOK_FALSE) {
        ts_lexer_next(lex);
        ts_ast *n = ts_ast_new(TS_AST_BOOL, t.line);
        n->bool_val = (t.type == TS_TOK_TRUE);
        return n;
    }

    /* Undef */
    if (t.type == TS_TOK_UNDEF) {
        ts_lexer_next(lex);
        return ts_ast_new(TS_AST_UNDEF, t.line);
    }

    /* Identifier or function call */
    if (t.type == TS_TOK_IDENT) {
        ts_lexer_next(lex);
        ts_token next = ts_lexer_peek(lex);

        /* Function call: ident(...) */
        if (next.type == TS_TOK_LPAREN) {
            ts_lexer_next(lex); /* consume ( */
            ts_ast *call = ts_ast_new(TS_AST_FUNC_CALL, t.line);
            call->str_val = ts_tok_strdup(t);

            /* Parse arguments */
            if (ts_lexer_peek(lex).type != TS_TOK_RPAREN) {
                do {
                    /* Check for named argument: ident = expr */
                    ts_token peek1 = ts_lexer_peek(lex);
                    char *arg_name = NULL;
                    if (peek1.type == TS_TOK_IDENT) {
                        /* Save state to check for = */
                        ts_lexer saved = *lex;
                        ts_lexer_next(lex);
                        if (ts_lexer_peek(lex).type == TS_TOK_ASSIGN) {
                            ts_lexer_next(lex); /* consume = */
                            arg_name = ts_tok_strdup(peek1);
                        } else {
                            *lex = saved; /* restore — it's a positional expr */
                        }
                    }
                    ts_ast *val = ts_parse_expr(lex, err);
                    ts_ast_add_arg(call, arg_name, val);
                } while (ts_lexer_peek(lex).type == TS_TOK_COMMA &&
                         (ts_lexer_next(lex), 1));
            }
            ts_expect(lex, TS_TOK_RPAREN, err);
            return call;
        }

        /* Plain identifier */
        ts_ast *n = ts_ast_new(TS_AST_IDENT, t.line);
        n->str_val = ts_tok_strdup(t);
        return n;
    }

    /* Vector or range: [...] */
    if (t.type == TS_TOK_LBRACKET) {
        ts_lexer_next(lex);

        /* Empty vector */
        if (ts_lexer_peek(lex).type == TS_TOK_RBRACKET) {
            ts_lexer_next(lex);
            return ts_ast_new(TS_AST_VECTOR, t.line);
        }

        /* List comprehension: [for (var = range) expr]
         * Also supports: [for (var = range) if (cond) expr]
         * and nested: [for (var = range) for (var2 = range2) expr]
         * and: [for (var = range) each expr] */
        if (ts_lexer_peek(lex).type == TS_TOK_FOR) {
            ts_ast *comp = ts_ast_new(TS_AST_LIST_COMP, t.line);
            ts_lexer_next(lex); /* consume 'for' */
            ts_expect(lex, TS_TOK_LPAREN, err);
            ts_token var = ts_expect(lex, TS_TOK_IDENT, err);
            comp->iter_var = ts_tok_strdup(var);
            ts_expect(lex, TS_TOK_ASSIGN, err);
            comp->iter_expr = ts_parse_expr(lex, err);
            ts_expect(lex, TS_TOK_RPAREN, err);

            /* Optional 'if' filter */
            if (ts_lexer_peek(lex).type == TS_TOK_IF) {
                ts_lexer_next(lex);
                ts_expect(lex, TS_TOK_LPAREN, err);
                comp->cond = ts_parse_expr(lex, err);
                ts_expect(lex, TS_TOK_RPAREN, err);
            }

            /* Body expression (what to generate per iteration) */
            comp->body = ts_parse_expr(lex, err);

            ts_expect(lex, TS_TOK_RBRACKET, err);
            return comp;
        }

        /* Parse first expression */
        ts_ast *first = ts_parse_expr(lex, err);

        /* Check if it's a range: [start : end] or [start : step : end] */
        if (ts_lexer_peek(lex).type == TS_TOK_COLON) {
            ts_lexer_next(lex); /* consume : */
            ts_ast *second = ts_parse_expr(lex, err);

            ts_ast *range = ts_ast_new(TS_AST_RANGE, t.line);
            if (ts_lexer_peek(lex).type == TS_TOK_COLON) {
                /* [start : step : end] */
                ts_lexer_next(lex);
                ts_ast *third = ts_parse_expr(lex, err);
                range->left = first;   /* start */
                range->cond = second;  /* step */
                range->right = third;  /* end */
            } else {
                /* [start : end] — step=1 */
                range->left = first;
                range->cond = NULL; /* step defaults to 1 */
                range->right = second;
            }
            ts_expect(lex, TS_TOK_RBRACKET, err);
            return range;
        }

        /* Vector literal: [expr, expr, ...] */
        ts_ast *vec = ts_ast_new(TS_AST_VECTOR, t.line);
        ts_ast_add_child(vec, first);
        while (ts_lexer_peek(lex).type == TS_TOK_COMMA) {
            ts_lexer_next(lex);
            if (ts_lexer_peek(lex).type == TS_TOK_RBRACKET) break;
            ts_ast_add_child(vec, ts_parse_expr(lex, err));
        }
        ts_expect(lex, TS_TOK_RBRACKET, err);
        return vec;
    }

    /* Parenthesized expression */
    if (t.type == TS_TOK_LPAREN) {
        ts_lexer_next(lex);
        ts_ast *e = ts_parse_expr(lex, err);
        ts_expect(lex, TS_TOK_RPAREN, err);
        return e;
    }

    /* Let expression: let(name=expr, ...) body_expr */
    if (t.type == TS_TOK_LET) {
        ts_lexer_next(lex);
        ts_ast *n = ts_ast_new(TS_AST_LET, t.line);
        ts_expect(lex, TS_TOK_LPAREN, err);
        /* Parse assignments: name = expr, ... */
        while (ts_lexer_peek(lex).type != TS_TOK_RPAREN &&
               ts_lexer_peek(lex).type != TS_TOK_EOF) {
            ts_token var = ts_expect(lex, TS_TOK_IDENT, err);
            ts_expect(lex, TS_TOK_ASSIGN, err);
            ts_ast *val = ts_parse_expr(lex, err);
            /* Store as ASSIGN child */
            ts_ast *assign = ts_ast_new(TS_AST_ASSIGN, var.line);
            assign->str_val = ts_tok_strdup(var);
            assign->left = val;
            ts_ast_add_child(n, assign);
            if (ts_lexer_peek(lex).type == TS_TOK_COMMA)
                ts_lexer_next(lex);
        }
        ts_expect(lex, TS_TOK_RPAREN, err);
        /* Body expression */
        n->body = ts_parse_expr(lex, err);
        return n;
    }

    ts_lexer_next(lex);
    ts_parse_err(err, t.line, "unexpected token in expression");
    return ts_ast_new(TS_AST_UNDEF, t.line);
}

/* Postfix: indexing a[i] */
static ts_ast *ts_parse_postfix(ts_lexer *lex, ts_parse_error *err) {
    ts_ast *left = ts_parse_primary(lex, err);
    while (ts_lexer_peek(lex).type == TS_TOK_LBRACKET) {
        ts_lexer_next(lex);
        ts_ast *idx = ts_parse_expr(lex, err);
        ts_expect(lex, TS_TOK_RBRACKET, err);
        ts_ast *n = ts_ast_new(TS_AST_INDEX, left->line);
        n->left = left;
        n->right = idx;
        left = n;
    }
    /* member access: v.x */
    while (ts_lexer_peek(lex).type == TS_TOK_DOT) {
        ts_lexer_next(lex);
        ts_token memb = ts_expect(lex, TS_TOK_IDENT, err);
        ts_ast *n = ts_ast_new(TS_AST_INDEX, left->line);
        n->left = left;
        /* .x -> [0], .y -> [1], .z -> [2] */
        ts_ast *idx = ts_ast_new(TS_AST_NUMBER, memb.line);
        if (memb.len == 1) {
            if (memb.start[0] == 'x') idx->num_val = 0;
            else if (memb.start[0] == 'y') idx->num_val = 1;
            else if (memb.start[0] == 'z') idx->num_val = 2;
        }
        n->right = idx;
        left = n;
    }
    return left;
}

/* Unary: - ! */
static ts_ast *ts_parse_unary(ts_lexer *lex, ts_parse_error *err) {
    ts_token t = ts_lexer_peek(lex);
    if (t.type == TS_TOK_MINUS || t.type == TS_TOK_NOT) {
        ts_lexer_next(lex);
        ts_ast *operand = ts_parse_unary(lex, err);
        ts_ast *n = ts_ast_new(TS_AST_UNARY, t.line);
        n->op = t.type;
        n->left = operand;
        return n;
    }
    return ts_parse_postfix(lex, err);
}

/* Binary operators with precedence climbing */
static ts_ast *ts_parse_multiply(ts_lexer *lex, ts_parse_error *err) {
    ts_ast *left = ts_parse_unary(lex, err);
    while (ts_lexer_peek(lex).type == TS_TOK_STAR ||
           ts_lexer_peek(lex).type == TS_TOK_SLASH ||
           ts_lexer_peek(lex).type == TS_TOK_PERCENT) {
        ts_token op = ts_lexer_next(lex);
        ts_ast *right = ts_parse_unary(lex, err);
        ts_ast *n = ts_ast_new(TS_AST_BINARY, op.line);
        n->op = op.type;
        n->left = left;
        n->right = right;
        left = n;
    }
    return left;
}

static ts_ast *ts_parse_addition(ts_lexer *lex, ts_parse_error *err) {
    ts_ast *left = ts_parse_multiply(lex, err);
    while (ts_lexer_peek(lex).type == TS_TOK_PLUS ||
           ts_lexer_peek(lex).type == TS_TOK_MINUS) {
        ts_token op = ts_lexer_next(lex);
        ts_ast *right = ts_parse_multiply(lex, err);
        ts_ast *n = ts_ast_new(TS_AST_BINARY, op.line);
        n->op = op.type;
        n->left = left;
        n->right = right;
        left = n;
    }
    return left;
}

static ts_ast *ts_parse_compare(ts_lexer *lex, ts_parse_error *err) {
    ts_ast *left = ts_parse_addition(lex, err);
    ts_tok_type pt = ts_lexer_peek(lex).type;
    while (pt == TS_TOK_LT || pt == TS_TOK_GT ||
           pt == TS_TOK_LE || pt == TS_TOK_GE ||
           pt == TS_TOK_EQ || pt == TS_TOK_NEQ) {
        ts_token op = ts_lexer_next(lex);
        ts_ast *right = ts_parse_addition(lex, err);
        ts_ast *n = ts_ast_new(TS_AST_BINARY, op.line);
        n->op = op.type;
        n->left = left;
        n->right = right;
        left = n;
        pt = ts_lexer_peek(lex).type;
    }
    return left;
}

static ts_ast *ts_parse_and(ts_lexer *lex, ts_parse_error *err) {
    ts_ast *left = ts_parse_compare(lex, err);
    while (ts_lexer_peek(lex).type == TS_TOK_AND) {
        ts_token op = ts_lexer_next(lex);
        ts_ast *right = ts_parse_compare(lex, err);
        ts_ast *n = ts_ast_new(TS_AST_BINARY, op.line);
        n->op = op.type;
        n->left = left;
        n->right = right;
        left = n;
    }
    return left;
}

static ts_ast *ts_parse_or(ts_lexer *lex, ts_parse_error *err) {
    ts_ast *left = ts_parse_and(lex, err);
    while (ts_lexer_peek(lex).type == TS_TOK_OR) {
        ts_token op = ts_lexer_next(lex);
        ts_ast *right = ts_parse_and(lex, err);
        ts_ast *n = ts_ast_new(TS_AST_BINARY, op.line);
        n->op = op.type;
        n->left = left;
        n->right = right;
        left = n;
    }
    return left;
}

/* Ternary: expr ? expr : expr */
static ts_ast *ts_parse_ternary(ts_lexer *lex, ts_parse_error *err) {
    ts_ast *cond = ts_parse_or(lex, err);
    if (ts_lexer_peek(lex).type == TS_TOK_QUESTION) {
        ts_lexer_next(lex);
        ts_ast *then_e = ts_parse_expr(lex, err);
        ts_expect(lex, TS_TOK_COLON, err);
        ts_ast *else_e = ts_parse_expr(lex, err);
        ts_ast *n = ts_ast_new(TS_AST_TERNARY, cond->line);
        n->cond = cond;
        n->left = then_e;
        n->right = else_e;
        return n;
    }
    return cond;
}

static ts_ast *ts_parse_expr(ts_lexer *lex, ts_parse_error *err) {
    return ts_parse_ternary(lex, err);
}

/* ================================================================
 * STATEMENT PARSING
 * ================================================================ */

/* Parse argument list for module instantiation (same as func call) */
static void ts_parse_arglist(ts_lexer *lex, ts_ast *node,
                              ts_parse_error *err) {
    ts_expect(lex, TS_TOK_LPAREN, err);
    if (ts_lexer_peek(lex).type != TS_TOK_RPAREN) {
        do {
            ts_token peek1 = ts_lexer_peek(lex);
            char *arg_name = NULL;
            if (peek1.type == TS_TOK_IDENT) {
                ts_lexer saved = *lex;
                ts_lexer_next(lex);
                if (ts_lexer_peek(lex).type == TS_TOK_ASSIGN) {
                    ts_lexer_next(lex);
                    arg_name = ts_tok_strdup(peek1);
                } else {
                    *lex = saved;
                }
            }
            ts_ast *val = ts_parse_expr(lex, err);
            ts_ast_add_arg(node, arg_name, val);
        } while (ts_lexer_peek(lex).type == TS_TOK_COMMA &&
                 (ts_lexer_next(lex), 1));
    }
    ts_expect(lex, TS_TOK_RPAREN, err);
}

/* Parse module body: ; or single statement or { statements } */
static ts_ast *ts_parse_module_body(ts_lexer *lex, ts_parse_error *err) {
    ts_token t = ts_lexer_peek(lex);

    /* Empty body: ; */
    if (t.type == TS_TOK_SEMICOLON) {
        ts_lexer_next(lex);
        return NULL;
    }

    /* Block body: { ... } */
    if (t.type == TS_TOK_LBRACE) {
        ts_lexer_next(lex);
        ts_ast *block = ts_ast_new(TS_AST_BLOCK, t.line);
        while (ts_lexer_peek(lex).type != TS_TOK_RBRACE &&
               ts_lexer_peek(lex).type != TS_TOK_EOF) {
            ts_ast *s = ts_parse_statement(lex, err);
            if (s) ts_ast_add_child(block, s);
        }
        ts_expect(lex, TS_TOK_RBRACE, err);
        return block;
    }

    /* Single statement body */
    return ts_parse_statement(lex, err);
}

/* Parse parameter definition list: (param, param=default, ...) */
static void ts_parse_def_params(ts_lexer *lex, ts_ast *node,
                                 ts_parse_error *err) {
    ts_expect(lex, TS_TOK_LPAREN, err);
    int cap = 4;
    node->def_params = (char **)malloc((size_t)cap * sizeof(char *));
    node->def_defaults = (ts_ast **)calloc((size_t)cap, sizeof(ts_ast *));
    node->def_param_count = 0;

    if (ts_lexer_peek(lex).type != TS_TOK_RPAREN) {
        do {
            ts_token name = ts_expect(lex, TS_TOK_IDENT, err);
            if (node->def_param_count >= cap) {
                cap *= 2;
                node->def_params = (char **)realloc(node->def_params,
                                                     (size_t)cap * sizeof(char *));
                node->def_defaults = (ts_ast **)realloc(node->def_defaults,
                                                         (size_t)cap * sizeof(ts_ast *));
            }
            int idx = node->def_param_count++;
            node->def_params[idx] = ts_tok_strdup(name);
            node->def_defaults[idx] = NULL;
            if (ts_lexer_peek(lex).type == TS_TOK_ASSIGN) {
                ts_lexer_next(lex);
                node->def_defaults[idx] = ts_parse_expr(lex, err);
            }
        } while (ts_lexer_peek(lex).type == TS_TOK_COMMA &&
                 (ts_lexer_next(lex), 1));
    }
    ts_expect(lex, TS_TOK_RPAREN, err);
}

static ts_ast *ts_parse_statement(ts_lexer *lex, ts_parse_error *err) {
    ts_token t = ts_lexer_peek(lex);

    /* Modifier characters: !, *, #, % */
    char modifier = 0;
    if (t.type == TS_TOK_NOT || t.type == TS_TOK_STAR ||
        t.type == TS_TOK_HASH || t.type == TS_TOK_PERCENT) {
        modifier = t.start[0];
        ts_lexer_next(lex);
        t = ts_lexer_peek(lex);
    }

    /* include <path> / use <path> */
    if (t.type == TS_TOK_INCLUDE || t.type == TS_TOK_USE) {
        ts_ast_type atype = (t.type == TS_TOK_INCLUDE) ?
                            TS_AST_INCLUDE : TS_AST_USE;
        ts_lexer_next(lex);
        ts_ast *n = ts_ast_new(atype, t.line);
        /* Expect < path > */
        ts_expect(lex, TS_TOK_LT, err);
        const char *start = lex->cur;
        while (*lex->cur && *lex->cur != '>') lex->cur++;
        int len = (int)(lex->cur - start);
        n->path = (char *)malloc((size_t)len + 1);
        memcpy(n->path, start, (size_t)len);
        n->path[len] = '\0';
        if (*lex->cur == '>') lex->cur++;
        lex->has_peek = 0; /* invalidate peek after manual advance */
        /* Optional semicolon */
        if (ts_lexer_peek(lex).type == TS_TOK_SEMICOLON)
            ts_lexer_next(lex);
        return n;
    }

    /* module definition */
    if (t.type == TS_TOK_MODULE) {
        ts_lexer_next(lex);
        ts_token name = ts_expect(lex, TS_TOK_IDENT, err);
        ts_ast *n = ts_ast_new(TS_AST_MODULE_DEF, t.line);
        n->str_val = ts_tok_strdup(name);
        ts_parse_def_params(lex, n, err);
        n->def_body = ts_parse_module_body(lex, err);
        return n;
    }

    /* function definition */
    if (t.type == TS_TOK_FUNCTION) {
        ts_lexer_next(lex);
        ts_token name = ts_expect(lex, TS_TOK_IDENT, err);
        ts_ast *n = ts_ast_new(TS_AST_FUNCTION_DEF, t.line);
        n->str_val = ts_tok_strdup(name);
        ts_parse_def_params(lex, n, err);
        ts_expect(lex, TS_TOK_ASSIGN, err);
        n->def_expr = ts_parse_expr(lex, err);
        ts_expect(lex, TS_TOK_SEMICOLON, err);
        return n;
    }

    /* if statement */
    if (t.type == TS_TOK_IF) {
        ts_lexer_next(lex);
        ts_expect(lex, TS_TOK_LPAREN, err);
        ts_ast *n = ts_ast_new(TS_AST_IF, t.line);
        n->cond = ts_parse_expr(lex, err);
        ts_expect(lex, TS_TOK_RPAREN, err);
        n->then_body = ts_parse_statement(lex, err);
        if (ts_lexer_peek(lex).type == TS_TOK_ELSE) {
            ts_lexer_next(lex);
            n->else_body = ts_parse_statement(lex, err);
        }
        return n;
    }

    /* for loop */
    if (t.type == TS_TOK_FOR) {
        ts_lexer_next(lex);
        ts_expect(lex, TS_TOK_LPAREN, err);
        ts_ast *n = ts_ast_new(TS_AST_FOR, t.line);
        ts_token var = ts_expect(lex, TS_TOK_IDENT, err);
        n->iter_var = ts_tok_strdup(var);
        ts_expect(lex, TS_TOK_ASSIGN, err);
        n->iter_expr = ts_parse_expr(lex, err);
        ts_expect(lex, TS_TOK_RPAREN, err);
        n->body = ts_parse_statement(lex, err);
        return n;
    }

    /* let statement: let(x=1, y=2) statement */
    if (t.type == TS_TOK_LET) {
        ts_lexer_next(lex);
        ts_ast *n = ts_ast_new(TS_AST_LET, t.line);
        ts_expect(lex, TS_TOK_LPAREN, err);
        while (ts_lexer_peek(lex).type != TS_TOK_RPAREN &&
               ts_lexer_peek(lex).type != TS_TOK_EOF) {
            ts_token var = ts_expect(lex, TS_TOK_IDENT, err);
            ts_expect(lex, TS_TOK_ASSIGN, err);
            ts_ast *val = ts_parse_expr(lex, err);
            ts_ast *assign = ts_ast_new(TS_AST_ASSIGN, var.line);
            assign->str_val = ts_tok_strdup(var);
            assign->left = val;
            ts_ast_add_child(n, assign);
            if (ts_lexer_peek(lex).type == TS_TOK_COMMA)
                ts_lexer_next(lex);
        }
        ts_expect(lex, TS_TOK_RPAREN, err);
        n->body = ts_parse_statement(lex, err);
        return n;
    }

    /* echo */
    if (t.type == TS_TOK_ECHO) {
        ts_lexer_next(lex);
        ts_ast *n = ts_ast_new(TS_AST_ECHO, t.line);
        ts_parse_arglist(lex, n, err);
        ts_expect(lex, TS_TOK_SEMICOLON, err);
        return n;
    }

    /* Block: { ... } */
    if (t.type == TS_TOK_LBRACE) {
        ts_lexer_next(lex);
        ts_ast *block = ts_ast_new(TS_AST_BLOCK, t.line);
        while (ts_lexer_peek(lex).type != TS_TOK_RBRACE &&
               ts_lexer_peek(lex).type != TS_TOK_EOF) {
            ts_ast *s = ts_parse_statement(lex, err);
            if (s) ts_ast_add_child(block, s);
        }
        ts_expect(lex, TS_TOK_RBRACE, err);
        return block;
    }

    /* Identifier: could be assignment or module instantiation */
    if (t.type == TS_TOK_IDENT) {
        /* Save lexer state to disambiguate */
        ts_lexer saved = *lex;
        ts_lexer_next(lex);
        ts_token next = ts_lexer_peek(lex);

        /* Assignment: ident = expr ; */
        if (next.type == TS_TOK_ASSIGN) {
            ts_lexer_next(lex); /* consume = */
            ts_ast *n = ts_ast_new(TS_AST_ASSIGN, t.line);
            n->str_val = ts_tok_strdup(t);
            n->left = ts_parse_expr(lex, err);
            ts_expect(lex, TS_TOK_SEMICOLON, err);
            return n;
        }

        /* Module instantiation: ident ( args ) body */
        if (next.type == TS_TOK_LPAREN) {
            ts_ast *n = ts_ast_new(TS_AST_MODULE_INST, t.line);
            n->str_val = ts_tok_strdup(t);
            n->modifier = modifier;
            ts_parse_arglist(lex, n, err);

            /* Body */
            ts_ast *body = ts_parse_module_body(lex, err);
            if (body) ts_ast_add_child(n, body);
            return n;
        }

        /* Otherwise restore and try as expression statement */
        *lex = saved;
    }

    /* Expression statement (fallback) */
    ts_ast *expr = ts_parse_expr(lex, err);
    if (ts_lexer_peek(lex).type == TS_TOK_SEMICOLON)
        ts_lexer_next(lex);
    return expr;
}

/* ================================================================
 * TOP-LEVEL PARSE
 * ================================================================ */
static inline ts_ast *ts_parse(const char *source, ts_parse_error *err) {
    ts_lexer lex;
    ts_lexer_init(&lex, source);

    if (err) { err->line = 0; err->msg[0] = '\0'; }

    ts_ast *root = ts_ast_new(TS_AST_BLOCK, 1);
    while (ts_lexer_peek(&lex).type != TS_TOK_EOF) {
        ts_ast *s = ts_parse_statement(&lex, err);
        if (s) ts_ast_add_child(root, s);
        if (err && err->msg[0]) break;
    }
    return root;
}

#endif /* TS_PARSER_H */
