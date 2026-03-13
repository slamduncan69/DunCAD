/*
 * ts_eval.h — OpenSCAD AST evaluator
 *
 * Tree-walking interpreter. Evaluates AST from ts_parser.h,
 * calls ts_geo/ts_csg/ts_extrude/ts_mat functions, produces ts_mesh.
 *
 * Value system: number, bool, string, vector, range, undef, mesh.
 * Scope model: lexical scoping with dynamic $-variable inheritance.
 * Geometry model: transforms compose via matrix stack, implicit union
 * for multiple children in a block.
 */
#ifndef TS_EVAL_H
#define TS_EVAL_H

#include "ts_parser.h"
#include "ts_vec.h"
#include "ts_mat.h"
#include "ts_mesh.h"
#include "ts_geo.h"
#include "ts_csg.h"
#include "ts_extrude.h"
#include "ts_scalar.h"
#include "ts_trig.h"
#include "ts_random.h"
#include "ts_text.h"

#include <stdio.h>
#include <math.h>

/* ================================================================
 * VALUE SYSTEM
 * ================================================================ */
typedef enum {
    TS_VAL_UNDEF,
    TS_VAL_NUMBER,
    TS_VAL_BOOL,
    TS_VAL_STRING,
    TS_VAL_VECTOR,
    TS_VAL_RANGE,
} ts_val_type;

typedef struct ts_val ts_val;
struct ts_val {
    ts_val_type type;
    double      num;
    int         boolean;
    char       *str;
    ts_val     *items;
    int         count;
    double      range_start, range_step, range_end;
};

static inline ts_val ts_val_undef(void) {
    ts_val v; memset(&v, 0, sizeof(v)); v.type = TS_VAL_UNDEF; return v;
}
static inline ts_val ts_val_num(double n) {
    ts_val v; memset(&v, 0, sizeof(v)); v.type = TS_VAL_NUMBER; v.num = n; return v;
}
static inline ts_val ts_val_bool(int b) {
    ts_val v; memset(&v, 0, sizeof(v)); v.type = TS_VAL_BOOL; v.boolean = b; return v;
}

static inline int ts_val_is_true(ts_val v) {
    if (v.type == TS_VAL_BOOL) return v.boolean;
    if (v.type == TS_VAL_NUMBER) return v.num != 0;
    if (v.type == TS_VAL_UNDEF) return 0;
    return 1;
}

static inline double ts_val_to_num(ts_val v) {
    if (v.type == TS_VAL_NUMBER) return v.num;
    if (v.type == TS_VAL_BOOL) return v.boolean ? 1.0 : 0.0;
    return 0.0;
}

static inline ts_val ts_val_vec3(double x, double y, double z) {
    ts_val v; memset(&v, 0, sizeof(v));
    v.type = TS_VAL_VECTOR;
    v.items = (ts_val *)malloc(3 * sizeof(ts_val));
    v.count = 3;
    v.items[0] = ts_val_num(x);
    v.items[1] = ts_val_num(y);
    v.items[2] = ts_val_num(z);
    return v;
}

static inline void ts_val_free(ts_val *v) {
    if (v->type == TS_VAL_STRING) { free(v->str); v->str = NULL; }
    if (v->type == TS_VAL_VECTOR) {
        for (int i = 0; i < v->count; i++) ts_val_free(&v->items[i]);
        free(v->items); v->items = NULL;
    }
}

/* Deep-copy a ts_val so the caller owns an independent copy.
 * Scalars/ranges are value types — returned as-is.
 * Vectors and strings are heap-allocated — we allocate fresh storage. */
static inline ts_val ts_val_clone(ts_val v);
static inline ts_val ts_val_clone(ts_val v) {
    if (v.type == TS_VAL_STRING && v.str != NULL) {
        ts_val r = v;
        r.str = strdup(v.str);
        return r;
    }
    if (v.type == TS_VAL_VECTOR && v.count > 0 && v.items != NULL) {
        ts_val r = v;
        r.items = (ts_val *)malloc((size_t)v.count * sizeof(ts_val));
        for (int i = 0; i < v.count; i++)
            r.items[i] = ts_val_clone(v.items[i]);
        return r;
    }
    return v;  /* NUMBER, BOOL, RANGE, UNDEF — plain value copy */
}

static inline double ts_val_vec_get(ts_val v, int idx) {
    if (v.type == TS_VAL_VECTOR && idx >= 0 && idx < v.count)
        return ts_val_to_num(v.items[idx]);
    if (v.type == TS_VAL_NUMBER) return v.num; /* scalar broadcast */
    return 0.0;
}

/* ================================================================
 * ENVIRONMENT (scope)
 * ================================================================ */
#define TS_ENV_MAX 256

typedef struct ts_env ts_env;
/* Shared progress tracking — written by worker thread, read by UI thread */
typedef struct {
    volatile int done;      /* statements completed so far */
    volatile int total;     /* total geometry statements to process */
} ts_progress;

struct ts_env {
    ts_env *parent;
    volatile int *cancel;   /* cooperative cancellation flag */
    ts_progress  *progress; /* shared progress (root env only, inherited) */
    char   *names[TS_ENV_MAX];
    ts_val  values[TS_ENV_MAX];
    int     count;

    /* Module/function definitions (top-level only) */
    char   *mod_names[TS_ENV_MAX];
    ts_ast *mod_defs[TS_ENV_MAX];
    int     mod_count;

    char   *func_names[TS_ENV_MAX];
    ts_ast *func_defs[TS_ENV_MAX];
    int     func_count;

    /* Forced quality overrides — bypass source $fn/$fa/$fs assignments */
    double  force_fn;   /* >0 = override any $fn from source */
    double  force_fa;   /* >0 = override any $fa from source */
    double  force_fs;   /* >0 = override any $fs from source */

    /* children() support: caller's geometry children */
    ts_ast **caller_children;
    int      caller_child_count;

    /* Module call stack for parent_module() */
    const char *module_name;  /* name of module being evaluated, NULL = top-level */

    /* Base directory for resolving include/use paths */
    char   *base_dir;   /* heap-owned, NULL = cwd */

    /* Included AST nodes (owned, freed with env) */
    ts_ast **included_asts;
    int      included_count;
    int      included_cap;
};

static inline ts_env *ts_env_new(ts_env *parent) {
    ts_env *e = (ts_env *)calloc(1, sizeof(ts_env));
    e->parent = parent;
    e->cancel = parent ? parent->cancel : NULL;
    e->progress = parent ? parent->progress : NULL;
    /* Inherit forced quality overrides */
    if (parent) {
        e->force_fn = parent->force_fn;
        e->force_fa = parent->force_fa;
        e->force_fs = parent->force_fs;
    }
    return e;
}

static inline int ts_env_cancelled(ts_env *e) {
    return e->cancel && *e->cancel;
}

static inline void ts_env_free(ts_env *e) {
    for (int i = 0; i < e->count; i++) {
        free(e->names[i]);
        ts_val_free(&e->values[i]);
    }
    free(e->base_dir);
    for (int i = 0; i < e->included_count; i++)
        ts_ast_free(e->included_asts[i]);
    free(e->included_asts);
    free(e);
}

static inline void ts_env_set(ts_env *e, const char *name, ts_val val) {
    /* Update existing */
    for (int i = 0; i < e->count; i++) {
        if (strcmp(e->names[i], name) == 0) {
            ts_val_free(&e->values[i]);
            e->values[i] = val;
            return;
        }
    }
    /* Add new */
    if (e->count < TS_ENV_MAX) {
        e->names[e->count] = strdup(name);
        e->values[e->count] = val;
        e->count++;
    }
}

static inline ts_val ts_env_get(ts_env *e, const char *name) {
    for (ts_env *cur = e; cur; cur = cur->parent) {
        for (int i = 0; i < cur->count; i++) {
            if (strcmp(cur->names[i], name) == 0)
                return cur->values[i];
        }
    }
    return ts_val_undef();
}

static inline ts_ast *ts_env_get_module(ts_env *e, const char *name) {
    for (ts_env *cur = e; cur; cur = cur->parent) {
        for (int i = 0; i < cur->mod_count; i++) {
            if (strcmp(cur->mod_names[i], name) == 0)
                return cur->mod_defs[i];
        }
    }
    return NULL;
}

static inline ts_ast *ts_env_get_func(ts_env *e, const char *name) {
    for (ts_env *cur = e; cur; cur = cur->parent) {
        for (int i = 0; i < cur->func_count; i++) {
            if (strcmp(cur->func_names[i], name) == 0)
                return cur->func_defs[i];
        }
    }
    return NULL;
}

static inline double ts_env_get_special(ts_env *e, const char *name,
                                         double def) {
    ts_val v = ts_env_get(e, name);
    return (v.type == TS_VAL_NUMBER) ? v.num : def;
}

static inline int ts_resolve_fn(ts_env *e) {
    /* Forced override takes absolute precedence */
    if (e->force_fn > 0) return (int)e->force_fn;
    double fn = ts_env_get_special(e, "$fn", 0);
    if (fn > 0) return (int)fn;
    /* Default: use $fa and $fs to compute */
    return 16; /* reasonable default */
}

/* ================================================================
 * ARGUMENT RESOLUTION
 * ================================================================ */

/* Get argument by name, then by position, then default */
static inline ts_val ts_arg_get(ts_ast *node, ts_env *env,
                                 const char *name, int pos, ts_val def);
static ts_val ts_eval_expr(ts_ast *node, ts_env *env);

static inline ts_val ts_arg_get(ts_ast *node, ts_env *env,
                                 const char *name, int pos, ts_val def) {
    /* First: look for named argument */
    for (int i = 0; i < node->arg_count; i++) {
        if (node->args[i].name && strcmp(node->args[i].name, name) == 0)
            return ts_eval_expr(node->args[i].value, env);
    }
    /* Second: positional argument */
    int positional = 0;
    for (int i = 0; i < node->arg_count; i++) {
        if (!node->args[i].name) {
            if (positional == pos)
                return ts_eval_expr(node->args[i].value, env);
            positional++;
        }
    }
    return def;
}

static inline int ts_arg_has(ts_ast *node, const char *name) {
    for (int i = 0; i < node->arg_count; i++) {
        if (node->args[i].name && strcmp(node->args[i].name, name) == 0)
            return 1;
    }
    return 0;
}

/* ================================================================
 * EXPRESSION EVALUATION
 * ================================================================ */
static ts_val ts_eval_expr(ts_ast *node, ts_env *env) {
    if (!node) return ts_val_undef();

    switch (node->type) {
    case TS_AST_NUMBER:
        return ts_val_num(node->num_val);

    case TS_AST_STRING: {
        ts_val v; memset(&v, 0, sizeof(v));
        v.type = TS_VAL_STRING;
        v.str = strdup(node->str_val ? node->str_val : "");
        return v;
    }

    case TS_AST_BOOL:
        return ts_val_bool(node->bool_val);

    case TS_AST_UNDEF:
        return ts_val_undef();

    case TS_AST_IDENT:
        return ts_env_get(env, node->str_val);

    case TS_AST_VECTOR: {
        ts_val v; memset(&v, 0, sizeof(v));
        v.type = TS_VAL_VECTOR;
        v.count = node->child_count;
        v.items = (ts_val *)malloc((size_t)v.count * sizeof(ts_val));
        for (int i = 0; i < v.count; i++)
            v.items[i] = ts_eval_expr(node->children[i], env);
        return v;
    }

    case TS_AST_LIST_COMP: {
        /* [for (var = range/vector) [if (cond)] expr] */
        ts_val iter = ts_eval_expr(node->iter_expr, env);
        int cap = 16, count = 0;
        ts_val *items = (ts_val *)malloc((size_t)cap * sizeof(ts_val));
        ts_env *cenv = ts_env_new(env);

        if (iter.type == TS_VAL_RANGE) {
            double start = iter.range_start;
            double step = iter.range_step;
            double end = iter.range_end;
            if (step == 0) step = 1;
            for (double v = start;
                 step > 0 ? v <= end + 1e-10 : v >= end - 1e-10;
                 v += step) {
                ts_env_set(cenv, node->iter_var, ts_val_num(v));
                if (node->cond) {
                    ts_val c = ts_eval_expr(node->cond, cenv);
                    if (!ts_val_is_true(c)) continue;
                }
                ts_val elem = ts_eval_expr(node->body, cenv);
                if (count >= cap) {
                    cap *= 2;
                    items = (ts_val *)realloc(items, (size_t)cap * sizeof(ts_val));
                }
                items[count++] = elem;
            }
        } else if (iter.type == TS_VAL_VECTOR) {
            for (int i = 0; i < iter.count; i++) {
                /* Clone: ts_env_set takes ownership and will free on next set */
                ts_env_set(cenv, node->iter_var, ts_val_clone(iter.items[i]));
                if (node->cond) {
                    ts_val c = ts_eval_expr(node->cond, cenv);
                    if (!ts_val_is_true(c)) continue;
                }
                ts_val elem = ts_eval_expr(node->body, cenv);
                if (count >= cap) {
                    cap *= 2;
                    items = (ts_val *)realloc(items, (size_t)cap * sizeof(ts_val));
                }
                items[count++] = elem;
            }
        }
        ts_env_free(cenv);

        ts_val result; memset(&result, 0, sizeof(result));
        result.type = TS_VAL_VECTOR;
        result.count = count;
        result.items = items;
        return result;
    }

    case TS_AST_RANGE: {
        ts_val v; memset(&v, 0, sizeof(v));
        v.type = TS_VAL_RANGE;
        v.range_start = ts_val_to_num(ts_eval_expr(node->left, env));
        v.range_end = ts_val_to_num(ts_eval_expr(node->right, env));
        v.range_step = node->cond ?
            ts_val_to_num(ts_eval_expr(node->cond, env)) : 1.0;
        return v;
    }

    case TS_AST_INDEX: {
        ts_val left = ts_eval_expr(node->left, env);
        ts_val idx = ts_eval_expr(node->right, env);
        int i = (int)ts_val_to_num(idx);
        if (left.type == TS_VAL_VECTOR && i >= 0 && i < left.count)
            return left.items[i];
        return ts_val_undef();
    }

    case TS_AST_UNARY: {
        ts_val operand = ts_eval_expr(node->left, env);
        if (node->op == TS_TOK_MINUS)
            return ts_val_num(-ts_val_to_num(operand));
        if (node->op == TS_TOK_NOT)
            return ts_val_bool(!ts_val_is_true(operand));
        return operand;
    }

    case TS_AST_BINARY: {
        ts_val lv = ts_eval_expr(node->left, env);
        ts_val rv = ts_eval_expr(node->right, env);
        double l = ts_val_to_num(lv), r = ts_val_to_num(rv);

        /* Vector arithmetic */
        if (lv.type == TS_VAL_VECTOR && rv.type == TS_VAL_VECTOR &&
            lv.count == rv.count &&
            (node->op == TS_TOK_PLUS || node->op == TS_TOK_MINUS)) {
            ts_val v; memset(&v, 0, sizeof(v));
            v.type = TS_VAL_VECTOR;
            v.count = lv.count;
            v.items = (ts_val *)malloc((size_t)v.count * sizeof(ts_val));
            for (int i = 0; i < v.count; i++) {
                double a = ts_val_to_num(lv.items[i]);
                double b = ts_val_to_num(rv.items[i]);
                v.items[i] = ts_val_num(node->op == TS_TOK_PLUS ? a+b : a-b);
            }
            return v;
        }
        /* Scalar * vector */
        if (lv.type == TS_VAL_NUMBER && rv.type == TS_VAL_VECTOR &&
            node->op == TS_TOK_STAR) {
            ts_val v; memset(&v, 0, sizeof(v));
            v.type = TS_VAL_VECTOR; v.count = rv.count;
            v.items = (ts_val *)malloc((size_t)v.count * sizeof(ts_val));
            for (int i = 0; i < v.count; i++)
                v.items[i] = ts_val_num(l * ts_val_to_num(rv.items[i]));
            return v;
        }
        /* Vector * scalar */
        if (lv.type == TS_VAL_VECTOR && rv.type == TS_VAL_NUMBER &&
            node->op == TS_TOK_STAR) {
            ts_val v; memset(&v, 0, sizeof(v));
            v.type = TS_VAL_VECTOR; v.count = lv.count;
            v.items = (ts_val *)malloc((size_t)v.count * sizeof(ts_val));
            for (int i = 0; i < v.count; i++)
                v.items[i] = ts_val_num(ts_val_to_num(lv.items[i]) * r);
            return v;
        }
        /* Vector / scalar */
        if (lv.type == TS_VAL_VECTOR && rv.type == TS_VAL_NUMBER &&
            node->op == TS_TOK_SLASH && r != 0) {
            ts_val v; memset(&v, 0, sizeof(v));
            v.type = TS_VAL_VECTOR; v.count = lv.count;
            v.items = (ts_val *)malloc((size_t)v.count * sizeof(ts_val));
            for (int i = 0; i < v.count; i++)
                v.items[i] = ts_val_num(ts_val_to_num(lv.items[i]) / r);
            return v;
        }

        switch (node->op) {
        case TS_TOK_PLUS:    return ts_val_num(l + r);
        case TS_TOK_MINUS:   return ts_val_num(l - r);
        case TS_TOK_STAR:    return ts_val_num(l * r);
        case TS_TOK_SLASH:   return ts_val_num(r != 0 ? l / r : 0);
        case TS_TOK_PERCENT: return ts_val_num(r != 0 ? fmod(l, r) : 0);
        case TS_TOK_CARET:   return ts_val_num(pow(l, r));
        case TS_TOK_LT:      return ts_val_bool(l < r);
        case TS_TOK_GT:      return ts_val_bool(l > r);
        case TS_TOK_LE:      return ts_val_bool(l <= r);
        case TS_TOK_GE:      return ts_val_bool(l >= r);
        case TS_TOK_EQ:      return ts_val_bool(fabs(l - r) < 1e-10);
        case TS_TOK_NEQ:     return ts_val_bool(fabs(l - r) >= 1e-10);
        case TS_TOK_AND:     return ts_val_bool(ts_val_is_true(lv) && ts_val_is_true(rv));
        case TS_TOK_OR:      return ts_val_bool(ts_val_is_true(lv) || ts_val_is_true(rv));
        default: return ts_val_undef();
        }
    }

    case TS_AST_TERNARY: {
        ts_val c = ts_eval_expr(node->cond, env);
        return ts_eval_expr(ts_val_is_true(c) ? node->left : node->right, env);
    }

    case TS_AST_LET: {
        /* let(x=1, y=2) expr — create child scope with bindings */
        ts_env *lenv = ts_env_new(env);
        for (int i = 0; i < node->child_count; i++) {
            ts_ast *assign = node->children[i];
            ts_val v = ts_eval_expr(assign->left, lenv);
            ts_env_set(lenv, assign->str_val, v);
        }
        ts_val result = ts_eval_expr(node->body, lenv);
        ts_env_free(lenv);
        return result;
    }

    case TS_AST_FUNC_CALL: {
        /* Built-in functions */
        const char *name = node->str_val;
        if (!name) return ts_val_undef();

        /* Check user-defined functions first */
        ts_ast *fdef = ts_env_get_func(env, name);
        if (fdef) {
            ts_env *fenv = ts_env_new(env);
            for (int i = 0; i < fdef->def_param_count; i++) {
                ts_val arg = ts_arg_get(node, env, fdef->def_params[i], i,
                    fdef->def_defaults[i] ?
                        ts_eval_expr(fdef->def_defaults[i], fenv) :
                        ts_val_undef());
                ts_env_set(fenv, fdef->def_params[i], arg);
            }
            ts_val result = ts_eval_expr(fdef->def_expr, fenv);
            ts_env_free(fenv);
            return result;
        }

        /* 1-arg math functions */
        if (node->arg_count >= 1) {
            ts_val a = ts_eval_expr(node->args[0].value, env);
            double x = ts_val_to_num(a);

            if (strcmp(name, "abs") == 0)   return ts_val_num(ts_abs(x));
            if (strcmp(name, "sign") == 0)  return ts_val_num(ts_sign(x));
            if (strcmp(name, "sin") == 0)   return ts_val_num(ts_sin_deg(x));
            if (strcmp(name, "cos") == 0)   return ts_val_num(ts_cos_deg(x));
            if (strcmp(name, "tan") == 0)   return ts_val_num(ts_tan_deg(x));
            if (strcmp(name, "asin") == 0)  return ts_val_num(ts_asin_deg(x));
            if (strcmp(name, "acos") == 0)  return ts_val_num(ts_acos_deg(x));
            if (strcmp(name, "atan") == 0)  return ts_val_num(ts_atan_deg(x));
            if (strcmp(name, "ceil") == 0)  return ts_val_num(ts_ceil(x));
            if (strcmp(name, "floor") == 0) return ts_val_num(ts_floor(x));
            if (strcmp(name, "round") == 0) return ts_val_num(ts_round(x));
            if (strcmp(name, "sqrt") == 0)  return ts_val_num(ts_sqrt(x));
            if (strcmp(name, "exp") == 0)   return ts_val_num(ts_exp(x));
            if (strcmp(name, "ln") == 0)    return ts_val_num(ts_ln(x));
            if (strcmp(name, "log") == 0)   return ts_val_num(ts_log10(x));
            if (strcmp(name, "len") == 0) {
                if (a.type == TS_VAL_VECTOR) return ts_val_num(a.count);
                if (a.type == TS_VAL_STRING) return ts_val_num(strlen(a.str));
                return ts_val_undef();
            }
            if (strcmp(name, "norm") == 0 && a.type == TS_VAL_VECTOR && a.count >= 2) {
                double sum = 0;
                for (int i = 0; i < a.count; i++) {
                    double v = ts_val_to_num(a.items[i]);
                    sum += v * v;
                }
                return ts_val_num(sqrt(sum));
            }
            if (strcmp(name, "concat") == 0) {
                /* Concatenate all args into one vector */
                ts_val result; memset(&result, 0, sizeof(result));
                result.type = TS_VAL_VECTOR;
                int total = 0;
                for (int i = 0; i < node->arg_count; i++) {
                    ts_val ai = ts_eval_expr(node->args[i].value, env);
                    if (ai.type == TS_VAL_VECTOR) total += ai.count;
                    else total++;
                }
                result.items = (ts_val *)malloc((size_t)total * sizeof(ts_val));
                result.count = 0;
                for (int i = 0; i < node->arg_count; i++) {
                    ts_val ai = ts_eval_expr(node->args[i].value, env);
                    if (ai.type == TS_VAL_VECTOR) {
                        for (int j = 0; j < ai.count; j++)
                            result.items[result.count++] = ai.items[j];
                    } else {
                        result.items[result.count++] = ai;
                    }
                }
                return result;
            }
        }
        /* 2-arg functions */
        if (node->arg_count >= 2) {
            ts_val a = ts_eval_expr(node->args[0].value, env);
            ts_val b = ts_eval_expr(node->args[1].value, env);
            double x = ts_val_to_num(a), y = ts_val_to_num(b);

            if (strcmp(name, "pow") == 0)   return ts_val_num(ts_pow(x, y));
            if (strcmp(name, "atan2") == 0) return ts_val_num(ts_atan2_deg(x, y));
            if (strcmp(name, "min") == 0)   return ts_val_num(ts_min(x, y));
            if (strcmp(name, "max") == 0)   return ts_val_num(ts_max(x, y));
            if (strcmp(name, "cross") == 0 &&
                a.type == TS_VAL_VECTOR && b.type == TS_VAL_VECTOR) {
                ts_vec3 va = {{ ts_val_vec_get(a,0), ts_val_vec_get(a,1), ts_val_vec_get(a,2) }};
                ts_vec3 vb = {{ ts_val_vec_get(b,0), ts_val_vec_get(b,1), ts_val_vec_get(b,2) }};
                ts_vec3 r = ts_vec3_cross(va, vb);
                return ts_val_vec3(r.v[0], r.v[1], r.v[2]);
            }
        }
        /* 3-arg: rands */
        if (node->arg_count >= 3 && strcmp(name, "rands") == 0) {
            double lo = ts_val_to_num(ts_eval_expr(node->args[0].value, env));
            double hi = ts_val_to_num(ts_eval_expr(node->args[1].value, env));
            int cnt = (int)ts_val_to_num(ts_eval_expr(node->args[2].value, env));
            unsigned long seed = 42;
            if (node->arg_count >= 4)
                seed = (unsigned long)ts_val_to_num(ts_eval_expr(node->args[3].value, env));
            double *buf = (double *)malloc((size_t)cnt * sizeof(double));
            ts_rands(lo, hi, cnt, (uint64_t)seed, buf);
            ts_val v; memset(&v, 0, sizeof(v));
            v.type = TS_VAL_VECTOR;
            v.count = cnt;
            v.items = (ts_val *)malloc((size_t)cnt * sizeof(ts_val));
            for (int i = 0; i < cnt; i++)
                v.items[i] = ts_val_num(buf[i]);
            free(buf);
            return v;
        }
        /* parent_module(idx) — return name of module in the call stack */
        if (strcmp(name, "parent_module") == 0) {
            int idx = 0;
            if (node->arg_count >= 1)
                idx = (int)ts_val_to_num(ts_eval_expr(node->args[0].value, env));
            /* Walk up the env chain counting module contexts */
            int depth = 0;
            for (ts_env *cur = env; cur; cur = cur->parent) {
                if (cur->module_name) {
                    if (depth == idx) {
                        ts_val v; memset(&v, 0, sizeof(v));
                        v.type = TS_VAL_STRING;
                        v.str = strdup(cur->module_name);
                        return v;
                    }
                    depth++;
                }
            }
            return ts_val_undef();
        }

        /* str() */
        if (strcmp(name, "str") == 0) {
            /* Simple: convert first arg to string */
            ts_val v; memset(&v, 0, sizeof(v));
            v.type = TS_VAL_STRING;
            char buf[256] = "";
            if (node->arg_count >= 1) {
                ts_val a = ts_eval_expr(node->args[0].value, env);
                if (a.type == TS_VAL_NUMBER) snprintf(buf, sizeof(buf), "%g", a.num);
                else if (a.type == TS_VAL_STRING) snprintf(buf, sizeof(buf), "%s", a.str);
                else if (a.type == TS_VAL_BOOL) snprintf(buf, sizeof(buf), "%s", a.boolean ? "true" : "false");
            }
            v.str = strdup(buf);
            return v;
        }

        fprintf(stderr, "Warning: unknown function '%s'\n", name);
        return ts_val_undef();
    }

    default:
        return ts_val_undef();
    }
}

/* ================================================================
 * GEOMETRY EVALUATION
 *
 * Returns a ts_mesh. Transform matrix accumulated through recursion.
 * ================================================================ */
static ts_mesh ts_eval_geometry(ts_ast *node, ts_env *env, ts_mat4 xform);

/* Evaluate children and combine (implicit union = mesh concatenation) */
static inline ts_mesh ts_eval_children(ts_ast *node, ts_env *env,
                                        ts_mat4 xform) {
    ts_mesh result = ts_mesh_init();
    for (int i = 0; i < node->child_count; i++) {
        ts_mesh child = ts_eval_geometry(node->children[i], env, xform);
        ts_mesh_append(&result, &child);
        ts_mesh_free(&child);
    }
    return result;
}

/* Apply transform matrix to generated mesh */
static inline void ts_apply_xform(ts_mesh *m, ts_mat4 xform) {
    /* Check if identity */
    ts_mat4 id = ts_mat4_identity();
    int is_id = 1;
    for (int i = 0; i < 16 && is_id; i++)
        if (fabs(xform.m[i] - id.m[i]) > 1e-15) is_id = 0;
    if (!is_id)
        ts_mesh_transform(m, xform.m);
}

/* Resolve and load an include/use file.
 * include = import definitions + variables + geometry
 * use = import module/function definitions only
 * Returns mesh (geometry from include; empty for use). */
static ts_mesh ts_eval_geometry(ts_ast *node, ts_env *env, ts_mat4 xform);

static inline ts_mesh ts_load_include(const char *path, ts_env *env,
                                       ts_mat4 xform, int use_only) {
    ts_mesh result = ts_mesh_init();

    /* Resolve path relative to base_dir */
    char resolved[4096];
    if (path[0] == '/') {
        snprintf(resolved, sizeof(resolved), "%s", path);
    } else if (env->base_dir) {
        snprintf(resolved, sizeof(resolved), "%s/%s", env->base_dir, path);
    } else {
        snprintf(resolved, sizeof(resolved), "%s", path);
    }

    /* Read file */
    FILE *fp = fopen(resolved, "r");
    if (!fp) {
        fprintf(stderr, "Warning: cannot open '%s'\n", resolved);
        return result;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *source = (char *)malloc((size_t)size + 1);
    size_t nread = fread(source, 1, (size_t)size, fp);
    source[nread] = '\0';
    fclose(fp);

    /* Parse */
    ts_parse_error err; memset(&err, 0, sizeof(err));
    ts_ast *root = ts_parse(source, &err);
    free(source);
    if (err.msg[0]) {
        fprintf(stderr, "Parse error in '%s' line %d: %s\n",
                resolved, err.line, err.msg);
        ts_ast_free(root);
        return result;
    }

    /* Store AST ownership in env so it lives until env is freed
     * (module defs reference into the AST) */
    if (env->included_count >= env->included_cap) {
        int newcap = env->included_cap ? env->included_cap * 2 : 4;
        env->included_asts = (ts_ast **)realloc(env->included_asts,
                                                  (size_t)newcap * sizeof(ts_ast *));
        env->included_cap = newcap;
    }
    env->included_asts[env->included_count++] = root;

    /* Extract base_dir of the included file for nested includes */
    char *inc_base = strdup(resolved);
    char *last_slash = strrchr(inc_base, '/');
    if (last_slash) *last_slash = '\0';
    else { free(inc_base); inc_base = NULL; }

    char *old_base = env->base_dir;
    env->base_dir = inc_base;

    /* Process the included file's AST */
    if (root && root->type == TS_AST_BLOCK) {
        for (int i = 0; i < root->child_count; i++) {
            ts_ast *c = root->children[i];
            /* Always import module/function definitions */
            if (c->type == TS_AST_MODULE_DEF) {
                if (env->mod_count < TS_ENV_MAX) {
                    env->mod_names[env->mod_count] = c->str_val;
                    env->mod_defs[env->mod_count] = c;
                    env->mod_count++;
                }
            } else if (c->type == TS_AST_FUNCTION_DEF) {
                if (env->func_count < TS_ENV_MAX) {
                    env->func_names[env->func_count] = c->str_val;
                    env->func_defs[env->func_count] = c;
                    env->func_count++;
                }
            } else if (!use_only) {
                /* include: also import variables and geometry */
                if (c->type == TS_AST_ASSIGN) {
                    ts_val v = ts_eval_expr(c->left, env);
                    ts_env_set(env, c->str_val, v);
                } else if (c->type == TS_AST_INCLUDE) {
                    ts_mesh sub = ts_load_include(c->path, env, xform, 0);
                    ts_mesh_append(&result, &sub);
                    ts_mesh_free(&sub);
                } else if (c->type == TS_AST_USE) {
                    ts_mesh sub = ts_load_include(c->path, env, xform, 1);
                    ts_mesh_free(&sub);
                } else if (c->type != TS_AST_ECHO) {
                    ts_mesh child = ts_eval_geometry(c, env, xform);
                    ts_mesh_append(&result, &child);
                    ts_mesh_free(&child);
                }
            }
        }
    }

    env->base_dir = old_base;
    free(inc_base);
    return result;
}

static ts_mesh ts_eval_geometry(ts_ast *node, ts_env *env, ts_mat4 xform) {
    ts_mesh result = ts_mesh_init();
    if (!node) return result;
    if (ts_env_cancelled(env)) return result;

    switch (node->type) {

    /* ---- Block / implicit union ---- */
    case TS_AST_BLOCK: {
        /* First pass: collect definitions, assignments, and includes */
        for (int i = 0; i < node->child_count; i++) {
            ts_ast *c = node->children[i];
            if (c->type == TS_AST_ASSIGN) {
                ts_val v = ts_eval_expr(c->left, env);
                ts_env_set(env, c->str_val, v);
            } else if (c->type == TS_AST_MODULE_DEF) {
                if (env->mod_count < TS_ENV_MAX) {
                    env->mod_names[env->mod_count] = c->str_val;
                    env->mod_defs[env->mod_count] = c;
                    env->mod_count++;
                }
            } else if (c->type == TS_AST_FUNCTION_DEF) {
                if (env->func_count < TS_ENV_MAX) {
                    env->func_names[env->func_count] = c->str_val;
                    env->func_defs[env->func_count] = c;
                    env->func_count++;
                }
            } else if (c->type == TS_AST_INCLUDE || c->type == TS_AST_USE) {
                /* Load include/use: import definitions (+ geometry for include) */
                int use_only = (c->type == TS_AST_USE);
                ts_mesh inc_geo = ts_load_include(c->path, env, xform, use_only);
                if (!use_only) {
                    ts_mesh_append(&result, &inc_geo);
                }
                ts_mesh_free(&inc_geo);
            }
        }
        /* Second pass: evaluate geometry */
        /* Count geometry statements for progress (root block only) */
        int is_root = (env->progress && env->progress->total == 0);
        if (is_root) {
            int geo_count = 0;
            for (int i = 0; i < node->child_count; i++) {
                ts_ast *c = node->children[i];
                if (c->type != TS_AST_ASSIGN && c->type != TS_AST_MODULE_DEF &&
                    c->type != TS_AST_FUNCTION_DEF && c->type != TS_AST_INCLUDE &&
                    c->type != TS_AST_USE && c->type != TS_AST_ECHO &&
                    c->type != TS_AST_ASSERT)
                    geo_count++;
            }
            env->progress->total = geo_count;
            env->progress->done = 0;
        }
        for (int i = 0; i < node->child_count; i++) {
            ts_ast *c = node->children[i];
            if (c->type == TS_AST_ASSIGN || c->type == TS_AST_MODULE_DEF ||
                c->type == TS_AST_FUNCTION_DEF || c->type == TS_AST_INCLUDE ||
                c->type == TS_AST_USE)
                continue;
            /* Execute echo/assert inline (side effects only, no geometry) */
            if (c->type == TS_AST_ECHO || c->type == TS_AST_ASSERT) {
                ts_mesh dummy = ts_eval_geometry(c, env, xform);
                ts_mesh_free(&dummy);
                continue;
            }
            ts_mesh child = ts_eval_geometry(c, env, xform);
            ts_mesh_append(&result, &child);
            ts_mesh_free(&child);
            if (is_root && env->progress)
                env->progress->done++;
            if (ts_env_cancelled(env)) return result;
        }
        return result;
    }

    /* ---- Assignment (handled in block) ---- */
    case TS_AST_ASSIGN: {
        ts_val v = ts_eval_expr(node->left, env);
        ts_env_set(env, node->str_val, v);
        return result;
    }

    /* ---- Echo ---- */
    case TS_AST_ECHO: {
        fprintf(stderr, "ECHO: ");
        for (int i = 0; i < node->arg_count; i++) {
            ts_val v = ts_eval_expr(node->args[i].value, env);
            if (i > 0) fprintf(stderr, ", ");
            if (node->args[i].name)
                fprintf(stderr, "%s = ", node->args[i].name);
            if (v.type == TS_VAL_NUMBER) fprintf(stderr, "%g", v.num);
            else if (v.type == TS_VAL_STRING) fprintf(stderr, "\"%s\"", v.str);
            else if (v.type == TS_VAL_BOOL) fprintf(stderr, "%s", v.boolean ? "true" : "false");
            else if (v.type == TS_VAL_VECTOR) {
                fprintf(stderr, "[");
                for (int j = 0; j < v.count; j++) {
                    if (j) fprintf(stderr, ", ");
                    fprintf(stderr, "%g", ts_val_to_num(v.items[j]));
                }
                fprintf(stderr, "]");
            }
            else fprintf(stderr, "undef");
        }
        fprintf(stderr, "\n");
        return result;
    }

    /* ---- Assert ---- */
    case TS_AST_ASSERT: {
        if (node->arg_count >= 1) {
            ts_val cond = ts_eval_expr(node->args[0].value, env);
            if (!ts_val_is_true(cond)) {
                if (node->arg_count >= 2) {
                    ts_val msg = ts_eval_expr(node->args[1].value, env);
                    if (msg.type == TS_VAL_STRING)
                        fprintf(stderr, "ERROR: Assertion failed: %s (line %d)\n",
                                msg.str, node->line);
                    else
                        fprintf(stderr, "ERROR: Assertion failed (line %d)\n",
                                node->line);
                } else {
                    fprintf(stderr, "ERROR: Assertion failed (line %d)\n",
                            node->line);
                }
            }
        }
        return result;
    }

    /* ---- If ---- */
    case TS_AST_IF: {
        ts_val cond = ts_eval_expr(node->cond, env);
        if (ts_val_is_true(cond)) {
            if (node->then_body)
                return ts_eval_geometry(node->then_body, env, xform);
        } else {
            if (node->else_body)
                return ts_eval_geometry(node->else_body, env, xform);
        }
        return result;
    }

    /* ---- Let (geometry context) ---- */
    case TS_AST_LET: {
        ts_env *lenv = ts_env_new(env);
        for (int i = 0; i < node->child_count; i++) {
            ts_ast *assign = node->children[i];
            ts_val v = ts_eval_expr(assign->left, lenv);
            ts_env_set(lenv, assign->str_val, v);
        }
        if (node->body)
            result = ts_eval_geometry(node->body, lenv, xform);
        ts_env_free(lenv);
        return result;
    }

    /* ---- For ---- */
    case TS_AST_FOR: {
        ts_val range = ts_eval_expr(node->iter_expr, env);
        ts_env *loop_env = ts_env_new(env);

        if (range.type == TS_VAL_RANGE) {
            double start = range.range_start;
            double step = range.range_step;
            double end = range.range_end;
            if (step == 0) step = 1;
            for (double i = start;
                 step > 0 ? i <= end + 1e-10 : i >= end - 1e-10;
                 i += step) {
                if (ts_env_cancelled(env)) break;
                ts_env_set(loop_env, node->iter_var, ts_val_num(i));
                ts_mesh child = ts_eval_geometry(node->body, loop_env, xform);
                ts_mesh_append(&result, &child);
                ts_mesh_free(&child);
            }
        } else if (range.type == TS_VAL_VECTOR) {
            for (int i = 0; i < range.count; i++) {
                if (ts_env_cancelled(env)) break;
                /* Clone: ts_env_set takes ownership and will free on next set */
                ts_env_set(loop_env, node->iter_var, ts_val_clone(range.items[i]));
                ts_mesh child = ts_eval_geometry(node->body, loop_env, xform);
                ts_mesh_append(&result, &child);
                ts_mesh_free(&child);
            }
        }

        ts_env_free(loop_env);
        return result;
    }

    /* ---- Module instantiation ---- */
    case TS_AST_MODULE_INST: {
        const char *name = node->str_val;
        if (!name) return result;

        /* Skip disabled modules (modifier *) */
        if (node->modifier == '*') return result;

        /* --- 3D Primitives --- */
        if (strcmp(name, "cube") == 0) {
            ts_val size_v = ts_arg_get(node, env, "size", 0, ts_val_num(1));
            ts_val center = ts_arg_get(node, env, "center", 1, ts_val_bool(0));
            double sx, sy, sz;
            if (size_v.type == TS_VAL_VECTOR) {
                sx = ts_val_vec_get(size_v, 0);
                sy = ts_val_vec_get(size_v, 1);
                sz = ts_val_vec_get(size_v, 2);
            } else {
                sx = sy = sz = ts_val_to_num(size_v);
            }
            ts_gen_cube(sx, sy, sz, &result);
            /* ts_gen_cube generates centered. If not center, translate. */
            if (!ts_val_is_true(center)) {
                ts_mat4 off = ts_mat4_translate(sx*0.5, sy*0.5, sz*0.5);
                ts_mesh_transform(&result, off.m);
            }
            ts_apply_xform(&result, xform);
            return result;
        }

        if (strcmp(name, "sphere") == 0) {
            ts_val r_v = ts_arg_get(node, env, "r", 0, ts_val_num(1));
            double r = ts_val_to_num(r_v);
            if (ts_arg_has(node, "d"))
                r = ts_val_to_num(ts_arg_get(node, env, "d", -1, ts_val_num(2))) * 0.5;
            int fn = ts_resolve_fn(env);
            ts_gen_sphere(r, fn, &result);
            ts_apply_xform(&result, xform);
            return result;
        }

        if (strcmp(name, "cylinder") == 0) {
            ts_val h_v = ts_arg_get(node, env, "h", 0, ts_val_num(1));
            double h = ts_val_to_num(h_v);
            double r1, r2;
            if (ts_arg_has(node, "r1")) {
                r1 = ts_val_to_num(ts_arg_get(node, env, "r1", -1, ts_val_num(1)));
                r2 = ts_val_to_num(ts_arg_get(node, env, "r2", -1, ts_val_num(1)));
            } else if (ts_arg_has(node, "d1")) {
                r1 = ts_val_to_num(ts_arg_get(node, env, "d1", -1, ts_val_num(2))) * 0.5;
                r2 = ts_val_to_num(ts_arg_get(node, env, "d2", -1, ts_val_num(2))) * 0.5;
            } else if (ts_arg_has(node, "d")) {
                r1 = r2 = ts_val_to_num(ts_arg_get(node, env, "d", -1, ts_val_num(2))) * 0.5;
            } else {
                r1 = r2 = ts_val_to_num(ts_arg_get(node, env, "r", 1, ts_val_num(1)));
            }
            ts_val center = ts_arg_get(node, env, "center", -1, ts_val_bool(0));
            int fn = ts_resolve_fn(env);
            ts_gen_cylinder(h, r1, r2, fn, &result);
            /* ts_gen_cylinder generates centered. If not center, translate up. */
            if (!ts_val_is_true(center)) {
                ts_mat4 off = ts_mat4_translate(0, 0, h*0.5);
                ts_mesh_transform(&result, off.m);
            }
            ts_apply_xform(&result, xform);
            return result;
        }

        /* --- Transforms --- */
        if (strcmp(name, "translate") == 0) {
            ts_val v = ts_arg_get(node, env, "v", 0, ts_val_vec3(0,0,0));
            double x = ts_val_vec_get(v, 0);
            double y = ts_val_vec_get(v, 1);
            double z = ts_val_vec_get(v, 2);
            ts_mat4 t = ts_mat4_translate(x, y, z);
            ts_mat4 combined = ts_mat4_multiply(xform, t);
            return ts_eval_children(node, env, combined);
        }

        if (strcmp(name, "rotate") == 0) {
            ts_val a_v = ts_arg_get(node, env, "a", 0, ts_val_num(0));
            ts_mat4 rot;
            if (a_v.type == TS_VAL_VECTOR) {
                /* rotate([rx,ry,rz]) — Euler rotation */
                double rx = ts_val_vec_get(a_v, 0);
                double ry = ts_val_vec_get(a_v, 1);
                double rz = ts_val_vec_get(a_v, 2);
                rot = ts_mat4_rotate_euler(rx, ry, rz);
            } else {
                /* rotate(a, v=[x,y,z]) — axis-angle rotation */
                double angle = ts_val_to_num(a_v);
                ts_val v_v = ts_arg_get(node, env, "v", 1, ts_val_vec3(0,0,1));
                ts_vec3 axis = {{ ts_val_vec_get(v_v,0), ts_val_vec_get(v_v,1), ts_val_vec_get(v_v,2) }};
                rot = ts_mat4_rotate_axis(angle, axis);
            }
            ts_mat4 combined = ts_mat4_multiply(xform, rot);
            return ts_eval_children(node, env, combined);
        }

        if (strcmp(name, "scale") == 0) {
            ts_val v = ts_arg_get(node, env, "v", 0, ts_val_vec3(1,1,1));
            double sx, sy, sz;
            if (v.type == TS_VAL_VECTOR) {
                sx = ts_val_vec_get(v, 0);
                sy = ts_val_vec_get(v, 1);
                sz = ts_val_vec_get(v, 2);
            } else {
                sx = sy = sz = ts_val_to_num(v);
            }
            ts_mat4 s = ts_mat4_scale(sx, sy, sz);
            ts_mat4 combined = ts_mat4_multiply(xform, s);
            return ts_eval_children(node, env, combined);
        }

        if (strcmp(name, "mirror") == 0) {
            ts_val v = ts_arg_get(node, env, "v", 0, ts_val_vec3(1,0,0));
            ts_vec3 normal = {{ ts_val_vec_get(v,0), ts_val_vec_get(v,1), ts_val_vec_get(v,2) }};
            ts_mat4 m = ts_mat4_mirror(normal);
            ts_mat4 combined = ts_mat4_multiply(xform, m);
            return ts_eval_children(node, env, combined);
        }

        if (strcmp(name, "multmatrix") == 0) {
            ts_val m_v = ts_arg_get(node, env, "m", 0, ts_val_undef());
            ts_mat4 mat = ts_mat4_identity();
            if (m_v.type == TS_VAL_VECTOR && m_v.count >= 4) {
                for (int r = 0; r < 4 && r < m_v.count; r++) {
                    ts_val row = m_v.items[r];
                    if (row.type == TS_VAL_VECTOR) {
                        for (int c = 0; c < 4 && c < row.count; c++)
                            mat.m[r*4+c] = ts_val_to_num(row.items[c]);
                    }
                }
            }
            ts_mat4 combined = ts_mat4_multiply(xform, mat);
            return ts_eval_children(node, env, combined);
        }

        if (strcmp(name, "color") == 0) {
            /* Color: evaluate children with same transform, ignore color */
            return ts_eval_children(node, env, xform);
        }

        if (strcmp(name, "render") == 0) {
            return ts_eval_children(node, env, xform);
        }

        /* --- import() — load external STL files --- */
        if (strcmp(name, "import") == 0) {
            ts_val file_v = ts_arg_get(node, env, "file", 0, ts_val_undef());
            if (file_v.type == TS_VAL_STRING && file_v.str) {
                /* Resolve path relative to base_dir */
                char resolved[4096];
                if (file_v.str[0] == '/') {
                    snprintf(resolved, sizeof(resolved), "%s", file_v.str);
                } else if (env->base_dir) {
                    snprintf(resolved, sizeof(resolved), "%s/%s",
                             env->base_dir, file_v.str);
                } else {
                    snprintf(resolved, sizeof(resolved), "%s", file_v.str);
                }
                /* Check file extension — only STL supported */
                const char *ext = strrchr(resolved, '.');
                if (ext && (strcmp(ext, ".stl") == 0 || strcmp(ext, ".STL") == 0)) {
                    if (ts_mesh_read_stl(&result, resolved) != 0)
                        fprintf(stderr, "Warning: cannot read STL '%s'\n", resolved);
                    else
                        ts_apply_xform(&result, xform);
                } else {
                    fprintf(stderr, "Warning: import() only supports STL files, got '%s'\n", resolved);
                }
            }
            return result;
        }

        /* --- surface() — heightmap from .dat file --- */
        if (strcmp(name, "surface") == 0) {
            ts_val file_v = ts_arg_get(node, env, "file", 0, ts_val_undef());
            int center = ts_val_is_true(ts_arg_get(node, env, "center", -1, ts_val_bool(0)));
            if (file_v.type == TS_VAL_STRING && file_v.str) {
                char resolved[4096];
                if (file_v.str[0] == '/') {
                    snprintf(resolved, sizeof(resolved), "%s", file_v.str);
                } else if (env->base_dir) {
                    snprintf(resolved, sizeof(resolved), "%s/%s",
                             env->base_dir, file_v.str);
                } else {
                    snprintf(resolved, sizeof(resolved), "%s", file_v.str);
                }
                int cols = 0, rows = 0;
                double *heights = ts_parse_dat(resolved, &cols, &rows);
                if (heights) {
                    ts_gen_surface(heights, cols, rows, center, &result);
                    free(heights);
                    ts_apply_xform(&result, xform);
                } else {
                    fprintf(stderr, "Warning: cannot read surface file '%s'\n", resolved);
                }
            }
            return result;
        }

        /* --- text() — 2D text via Hershey font --- */
        if (strcmp(name, "text") == 0) {
            ts_val t_v = ts_arg_get(node, env, "t", 0, ts_val_undef());
            if (t_v.type != TS_VAL_STRING && node->arg_count > 0) {
                /* First positional arg might be the text */
                t_v = ts_eval_expr(node->args[0].value, env);
            }
            if (t_v.type == TS_VAL_STRING && t_v.str) {
                double size = ts_val_to_num(ts_arg_get(node, env, "size", 1, ts_val_num(10)));
                ts_val ha = ts_arg_get(node, env, "halign", -1, ts_val_undef());
                ts_val va = ts_arg_get(node, env, "valign", -1, ts_val_undef());
                double spacing = ts_val_to_num(ts_arg_get(node, env, "spacing", -1, ts_val_num(1)));
                const char *halign_s = (ha.type == TS_VAL_STRING) ? ha.str : "left";
                const char *valign_s = (va.type == TS_VAL_STRING) ? va.str : "baseline";
                ts_gen_text(t_v.str, size, halign_s, valign_s, spacing, &result);
                ts_apply_xform(&result, xform);
            }
            return result;
        }

        /* --- CSG operations --- */
        if (strcmp(name, "union") == 0) {
            /* Explicit union: concatenate children */
            return ts_eval_children(node, env, xform);
        }

        if (strcmp(name, "difference") == 0) {
            if (node->child_count == 0) return result;
            /* Handle block child containing multiple geometry */
            ts_ast *first_child = node->children[0];
            ts_mesh base;
            int nchildren;
            ts_ast **children;

            if (first_child->type == TS_AST_BLOCK && first_child->child_count >= 2) {
                /* Block with multiple children: first is base, rest subtracted */
                children = first_child->children;
                nchildren = first_child->child_count;
            } else {
                children = node->children;
                nchildren = node->child_count;
            }

            base = ts_eval_geometry(children[0], env, xform);
            for (int i = 1; i < nchildren; i++) {
                ts_mesh tool = ts_eval_geometry(children[i], env, xform);
                if (tool.tri_count > 0 && base.tri_count > 0) {
                    ts_mesh diff = ts_mesh_init();
                    ts_csg_boolean(&base, &tool, TS_CSG_OP_DIFFERENCE, &diff);
                    ts_mesh_free(&base);
                    base = diff;
                }
                ts_mesh_free(&tool);
            }
            return base;
        }

        if (strcmp(name, "intersection") == 0) {
            if (node->child_count == 0) return result;
            /* Handle block child containing multiple geometry */
            ts_ast *first_child = node->children[0];
            ts_mesh base;

            if (first_child->type == TS_AST_BLOCK && first_child->child_count >= 2) {
                /* Block with multiple children: intersect them all */
                base = ts_eval_geometry(first_child->children[0], env, xform);
                for (int i = 1; i < first_child->child_count; i++) {
                    ts_mesh tool = ts_eval_geometry(first_child->children[i], env, xform);
                    if (tool.tri_count > 0 && base.tri_count > 0) {
                        ts_mesh inter = ts_mesh_init();
                        ts_csg_boolean(&base, &tool, TS_CSG_OP_INTERSECTION, &inter);
                        ts_mesh_free(&base);
                        base = inter;
                    }
                    ts_mesh_free(&tool);
                }
            } else {
                base = ts_eval_geometry(first_child, env, xform);
                for (int i = 1; i < node->child_count; i++) {
                    ts_mesh tool = ts_eval_geometry(node->children[i], env, xform);
                    if (tool.tri_count > 0 && base.tri_count > 0) {
                        ts_mesh inter = ts_mesh_init();
                        ts_csg_boolean(&base, &tool, TS_CSG_OP_INTERSECTION, &inter);
                        ts_mesh_free(&base);
                        base = inter;
                    }
                    ts_mesh_free(&tool);
                }
            }
            return base;
        }

        if (strcmp(name, "hull") == 0) {
            ts_mesh combined = ts_eval_children(node, env, xform);
            if (combined.tri_count > 0) {
                ts_csg_hull(&combined, &result);
                ts_mesh_free(&combined);
                return result;
            }
            return combined;
        }

        if (strcmp(name, "minkowski") == 0) {
            if (node->child_count == 0) return result;
            /* Get first two geometry children */
            ts_ast *body = node->children[0];
            ts_mesh meshes[2];
            int mesh_count = 0;

            if (body && body->type == TS_AST_BLOCK) {
                for (int i = 0; i < body->child_count && mesh_count < 2; i++) {
                    ts_mesh m = ts_eval_geometry(body->children[i], env, xform);
                    if (m.tri_count > 0)
                        meshes[mesh_count++] = m;
                    else
                        ts_mesh_free(&m);
                }
            } else {
                for (int i = 0; i < node->child_count && mesh_count < 2; i++) {
                    ts_mesh m = ts_eval_geometry(node->children[i], env, xform);
                    if (m.tri_count > 0)
                        meshes[mesh_count++] = m;
                    else
                        ts_mesh_free(&m);
                }
            }

            if (mesh_count == 2) {
                ts_csg_minkowski(&meshes[0], &meshes[1], &result);
                ts_mesh_free(&meshes[0]);
                ts_mesh_free(&meshes[1]);
            } else if (mesh_count == 1) {
                result = meshes[0];
            }
            return result;
        }

        /* --- Extrusion --- */
        if (strcmp(name, "linear_extrude") == 0) {
            double height = ts_val_to_num(ts_arg_get(node, env, "height", 0, ts_val_num(1)));
            double twist = ts_val_to_num(ts_arg_get(node, env, "twist", -1, ts_val_num(0)));
            int slices = (int)ts_val_to_num(ts_arg_get(node, env, "slices", -1, ts_val_num(1)));
            double scale_top = ts_val_to_num(ts_arg_get(node, env, "scale", -1, ts_val_num(1)));
            int center = ts_val_is_true(ts_arg_get(node, env, "center", -1, ts_val_bool(0)));
            if (twist != 0 && slices < 2) slices = (int)(fabs(twist) / 5) + 1;

            /* Evaluate 2D children to get profile points */
            /* Look for circle/square/polygon in children */
            for (int i = 0; i < node->child_count; i++) {
                ts_ast *c = node->children[i];
                if (!c) continue;
                /* Handle block wrapper */
                if (c->type == TS_AST_BLOCK && c->child_count > 0)
                    c = c->children[0];
                if (c->type != TS_AST_MODULE_INST) continue;
                const char *cname = c->str_val;
                if (!cname) continue;

                double *pts = NULL;
                int npts = 0;

                if (strcmp(cname, "circle") == 0) {
                    double r = ts_val_to_num(ts_arg_get(c, env, "r", 0, ts_val_num(1)));
                    if (ts_arg_has(c, "d"))
                        r = ts_val_to_num(ts_arg_get(c, env, "d", -1, ts_val_num(2))) * 0.5;
                    int fn = ts_resolve_fn(env);
                    pts = (double *)malloc((size_t)fn * 2 * sizeof(double));
                    npts = ts_gen_circle_points(r, fn, pts, fn);
                } else if (strcmp(cname, "square") == 0) {
                    ts_val sv = ts_arg_get(c, env, "size", 0, ts_val_num(1));
                    double sx, sy;
                    if (sv.type == TS_VAL_VECTOR) {
                        sx = ts_val_vec_get(sv, 0); sy = ts_val_vec_get(sv, 1);
                    } else {
                        sx = sy = ts_val_to_num(sv);
                    }
                    ts_val ctr = ts_arg_get(c, env, "center", 1, ts_val_bool(0));
                    pts = (double *)malloc(8 * sizeof(double));
                    npts = ts_gen_square_points(sx, sy, pts, 4);
                    if (!ts_val_is_true(ctr)) {
                        for (int p = 0; p < npts; p++) {
                            pts[p*2] += sx * 0.5;
                            pts[p*2+1] += sy * 0.5;
                        }
                    }
                } else if (strcmp(cname, "polygon") == 0) {
                    ts_val pv = ts_arg_get(c, env, "points", 0, ts_val_undef());
                    if (pv.type == TS_VAL_VECTOR) {
                        npts = pv.count;
                        pts = (double *)malloc((size_t)npts * 2 * sizeof(double));
                        for (int p = 0; p < npts; p++) {
                            pts[p*2] = ts_val_vec_get(pv.items[p], 0);
                            pts[p*2+1] = ts_val_vec_get(pv.items[p], 1);
                        }
                    }
                }

                /* text() child: extrude the 2D text mesh directly */
                if (strcmp(cname, "text") == 0) {
                    ts_mat4 id = ts_mat4_identity();
                    ts_mesh flat = ts_eval_geometry(c, env, id);
                    if (flat.tri_count > 0) {
                        ts_mesh_extrude_z(&flat, height, center, &result);
                        ts_mesh_free(&flat);
                        ts_apply_xform(&result, xform);
                        return result;
                    }
                    ts_mesh_free(&flat);
                }

                if (pts && npts >= 3) {
                    ts_linear_extrude(pts, npts, height, twist, slices,
                                      scale_top, center, &result);
                    ts_apply_xform(&result, xform);
                    free(pts);
                    return result;
                }
                free(pts);
            }
            return result;
        }

        if (strcmp(name, "rotate_extrude") == 0) {
            double angle = ts_val_to_num(ts_arg_get(node, env, "angle", -1, ts_val_num(360)));
            int fn = ts_resolve_fn(env);

            for (int i = 0; i < node->child_count; i++) {
                ts_ast *c = node->children[i];
                if (!c) continue;
                if (c->type == TS_AST_BLOCK && c->child_count > 0)
                    c = c->children[0];
                if (c->type != TS_AST_MODULE_INST) continue;
                const char *cname = c->str_val;
                if (!cname) continue;

                double *pts = NULL;
                int npts = 0;

                if (strcmp(cname, "circle") == 0) {
                    double r = ts_val_to_num(ts_arg_get(c, env, "r", 0, ts_val_num(1)));
                    if (ts_arg_has(c, "d"))
                        r = ts_val_to_num(ts_arg_get(c, env, "d", -1, ts_val_num(2))) * 0.5;
                    int cfn = ts_resolve_fn(env);
                    pts = (double *)malloc((size_t)cfn * 2 * sizeof(double));
                    npts = ts_gen_circle_points(r, cfn, pts, cfn);
                } else if (strcmp(cname, "square") == 0) {
                    ts_val sv = ts_arg_get(c, env, "size", 0, ts_val_num(1));
                    double sx, sy;
                    if (sv.type == TS_VAL_VECTOR) {
                        sx = ts_val_vec_get(sv, 0); sy = ts_val_vec_get(sv, 1);
                    } else {
                        sx = sy = ts_val_to_num(sv);
                    }
                    pts = (double *)malloc(8 * sizeof(double));
                    npts = ts_gen_square_points(sx, sy, pts, 4);
                    /* Offset to positive X for rotation */
                    for (int p = 0; p < npts; p++)
                        pts[p*2] += sx * 0.5;
                } else if (strcmp(cname, "polygon") == 0) {
                    ts_val pv = ts_arg_get(c, env, "points", 0, ts_val_undef());
                    if (pv.type == TS_VAL_VECTOR) {
                        npts = pv.count;
                        pts = (double *)malloc((size_t)npts * 2 * sizeof(double));
                        for (int p = 0; p < npts; p++) {
                            pts[p*2] = ts_val_vec_get(pv.items[p], 0);
                            pts[p*2+1] = ts_val_vec_get(pv.items[p], 1);
                        }
                    }
                }
                if (strcmp(cname, "translate") == 0) {
                    /* translate([x,y]) circle(...) pattern for rotate_extrude */
                    ts_val tv = ts_arg_get(c, env, "v", 0, ts_val_vec3(0,0,0));
                    double tx = ts_val_vec_get(tv, 0);
                    double ty = ts_val_vec_get(tv, 1);
                    /* Look for circle child */
                    for (int j = 0; j < c->child_count; j++) {
                        ts_ast *gc = c->children[j];
                        if (gc && gc->type == TS_AST_MODULE_INST &&
                            gc->str_val && strcmp(gc->str_val, "circle") == 0) {
                            double r = ts_val_to_num(ts_arg_get(gc, env, "r", 0, ts_val_num(1)));
                            int cfn = ts_resolve_fn(env);
                            pts = (double *)malloc((size_t)cfn * 2 * sizeof(double));
                            npts = ts_gen_circle_points(r, cfn, pts, cfn);
                            for (int p = 0; p < npts; p++) {
                                pts[p*2] += tx;
                                pts[p*2+1] += ty;
                            }
                            break;
                        }
                    }
                }

                if (pts && npts >= 3) {
                    ts_rotate_extrude(pts, npts, angle, fn, &result);
                    ts_apply_xform(&result, xform);
                    free(pts);
                    return result;
                }
                free(pts);
            }
            return result;
        }

        /* --- children() — evaluate caller's geometry children --- */
        if (strcmp(name, "children") == 0) {
            if (env->caller_children && env->caller_child_count > 0) {
                /* children(i) = specific child, children() = all */
                if (node->arg_count > 0) {
                    ts_val idx = ts_eval_expr(node->args[0].value, env);
                    int i = (int)ts_val_to_num(idx);
                    if (i >= 0 && i < env->caller_child_count) {
                        result = ts_eval_geometry(env->caller_children[i],
                                                   env, xform);
                    }
                } else {
                    for (int i = 0; i < env->caller_child_count; i++) {
                        ts_mesh child = ts_eval_geometry(
                            env->caller_children[i], env, xform);
                        ts_mesh_append(&result, &child);
                        ts_mesh_free(&child);
                    }
                }
            }
            return result;
        }

        /* --- User-defined modules --- */
        ts_ast *mdef = ts_env_get_module(env, name);
        if (mdef) {
            ts_env *menv = ts_env_new(env);
            menv->module_name = name;
            /* Pass caller's children for children() access */
            menv->caller_children = node->children;
            menv->caller_child_count = node->child_count;
            ts_env_set(menv, "$children", ts_val_num((double)node->child_count));
            for (int i = 0; i < mdef->def_param_count; i++) {
                ts_val arg = ts_arg_get(node, env, mdef->def_params[i], i,
                    mdef->def_defaults[i] ?
                        ts_eval_expr(mdef->def_defaults[i], menv) :
                        ts_val_undef());
                ts_env_set(menv, mdef->def_params[i], arg);
            }
            if (mdef->def_body) {
                result = ts_eval_geometry(mdef->def_body, menv, xform);
            }
            ts_env_free(menv);
            return result;
        }

        fprintf(stderr, "Warning: unknown module '%s' at line %d\n",
                name, node->line);
        return result;
    }

    default:
        /* Ignore non-geometry nodes */
        return result;
    }
}

/* ================================================================
 * TOP-LEVEL INTERPRETER
 * ================================================================ */

/* Options for ts_interpret_ex — quality overrides + cancellation + progress */
typedef struct {
    double fn_override;    /* >0 = override default $fn */
    double fa_override;    /* >0 = override default $fa */
    double fs_override;    /* >0 = override default $fs */
    int    force_quality;  /* if true, overrides CANNOT be overridden by source */
    volatile int *cancel;  /* if non-NULL, checked for cooperative abort */
    ts_progress  *progress; /* if non-NULL, updated with statement progress */
    const char   *base_dir; /* directory for resolving include/use paths */
} ts_interpret_opts;

/* Interpret source code with options, produce mesh */
static inline ts_mesh ts_interpret_ex(const char *source,
                                       ts_parse_error *err,
                                       const ts_interpret_opts *opts) {
    ts_ast *root = ts_parse(source, err);
    if (err && err->msg[0]) {
        ts_ast_free(root);
        return ts_mesh_init();
    }

    ts_env *env = ts_env_new(NULL);
    double fn = (opts && opts->fn_override > 0) ? opts->fn_override : 100;
    double fa = (opts && opts->fa_override > 0) ? opts->fa_override : 1;
    double fs = (opts && opts->fs_override > 0) ? opts->fs_override : 0.4;
    ts_env_set(env, "$fn", ts_val_num(fn));
    ts_env_set(env, "$fa", ts_val_num(fa));
    ts_env_set(env, "$fs", ts_val_num(fs));
    /* Force mode: source $fn/$fa/$fs assignments are ignored */
    if (opts && opts->force_quality) {
        env->force_fn = fn;
        env->force_fa = fa;
        env->force_fs = fs;
    }
    if (opts && opts->cancel) env->cancel = opts->cancel;
    if (opts && opts->progress) env->progress = opts->progress;
    if (opts && opts->base_dir) env->base_dir = strdup(opts->base_dir);

    ts_mat4 identity = ts_mat4_identity();
    ts_mesh result = ts_eval_geometry(root, env, identity);

    ts_env_free(env);
    ts_ast_free(root);
    return result;
}

/* Interpret source code, produce mesh (convenience wrapper) */
static inline ts_mesh ts_interpret(const char *source, ts_parse_error *err) {
    return ts_interpret_ex(source, err, NULL);
}

/* Read file, interpret, write STL */
static inline int ts_interpret_file(const char *scad_path,
                                     const char *stl_path) {
    FILE *fp = fopen(scad_path, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s'\n", scad_path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *source = (char *)malloc((size_t)size + 1);
    size_t nread = fread(source, 1, (size_t)size, fp);
    source[nread] = '\0';
    fclose(fp);

    /* Extract directory from file path for include/use resolution */
    ts_interpret_opts opts; memset(&opts, 0, sizeof(opts));
    char *dir = strdup(scad_path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; opts.base_dir = dir; }
    else { free(dir); dir = NULL; }

    ts_parse_error err; memset(&err, 0, sizeof(err));
    ts_mesh mesh = ts_interpret_ex(source, &err, &opts);
    free(source);
    free(dir);

    if (err.msg[0]) {
        fprintf(stderr, "Parse error at line %d: %s\n", err.line, err.msg);
        ts_mesh_free(&mesh);
        return -1;
    }

    if (mesh.tri_count == 0) {
        fprintf(stderr, "Warning: no geometry produced\n");
        ts_mesh_free(&mesh);
        return -1;
    }

    printf("Generated %d vertices, %d triangles\n",
           mesh.vert_count, mesh.tri_count);

    int ret = ts_mesh_write_stl(&mesh, stl_path);
    ts_mesh_free(&mesh);

    if (ret == 0)
        printf("Written to %s\n", stl_path);
    else
        fprintf(stderr, "Error: failed to write '%s'\n", stl_path);

    return ret;
}

#endif /* TS_EVAL_H */
