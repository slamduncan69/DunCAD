#define _POSIX_C_SOURCE 200809L
/*
 * sexpr.c — Generic s-expression parser for KiCad file formats.
 *
 * Recursive descent parser that handles:
 *   - Unquoted atoms (identifiers, numbers, layer names like F.Cu)
 *   - Quoted strings with backslash escapes
 *   - Nested parenthesized lists
 *   - Line/block comments (not standard s-expr but KiCad doesn't use them)
 *
 * The parser is single-pass and produces a tree of DC_Sexpr nodes.
 */

#include "eda/sexpr.h"
#include "core/string_builder.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Parser state
 * ========================================================================= */
typedef struct {
    const char *src;
    const char *pos;
    int         line;
    DC_Error   *err;
} SexprParser;

/* ---- Helpers ---- */

static void
skip_whitespace(SexprParser *p)
{
    while (*p->pos) {
        if (*p->pos == '\n') {
            p->line++;
            p->pos++;
        } else if (*p->pos == ' ' || *p->pos == '\t' || *p->pos == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static DC_Sexpr *
sexpr_new(DC_SexprType type, int line)
{
    DC_Sexpr *n = calloc(1, sizeof(DC_Sexpr));
    if (n) {
        n->type = type;
        n->line = line;
    }
    return n;
}

static int
sexpr_add_child(DC_Sexpr *parent, DC_Sexpr *child)
{
    if (parent->child_count >= parent->child_cap) {
        size_t new_cap = parent->child_cap ? parent->child_cap * 2 : 8;
        DC_Sexpr **new_children = realloc(parent->children,
                                           new_cap * sizeof(DC_Sexpr *));
        if (!new_children) return -1;
        parent->children = new_children;
        parent->child_cap = new_cap;
    }
    parent->children[parent->child_count++] = child;
    return 0;
}

/* Is this character valid inside an unquoted atom? */
static int
is_atom_char(char c)
{
    if (c == '\0' || c == '(' || c == ')' || c == '"') return 0;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return 0;
    return 1;
}

/* =========================================================================
 * Recursive descent parser
 * ========================================================================= */
static DC_Sexpr *
parse_node(SexprParser *p)
{
    skip_whitespace(p);
    if (!*p->pos) return NULL;

    /* --- List: (tag child1 child2 ...) --- */
    if (*p->pos == '(') {
        p->pos++;
        DC_Sexpr *list = sexpr_new(DC_SEXPR_LIST, p->line);
        if (!list) {
            if (p->err) DC_SET_ERROR(p->err, DC_ERROR_MEMORY, "sexpr list alloc");
            return NULL;
        }

        skip_whitespace(p);
        while (*p->pos && *p->pos != ')') {
            DC_Sexpr *child = parse_node(p);
            if (!child) {
                dc_sexpr_free(list);
                return NULL;
            }
            if (sexpr_add_child(list, child) != 0) {
                dc_sexpr_free(child);
                dc_sexpr_free(list);
                if (p->err) DC_SET_ERROR(p->err, DC_ERROR_MEMORY, "sexpr add child");
                return NULL;
            }
            skip_whitespace(p);
        }

        if (*p->pos == ')') {
            p->pos++;
        } else {
            if (p->err) DC_SET_ERROR(p->err, DC_ERROR_PARSE,
                                      "unclosed '(' at line %d", list->line);
            dc_sexpr_free(list);
            return NULL;
        }
        return list;
    }

    /* --- Quoted string --- */
    if (*p->pos == '"') {
        p->pos++;
        int start_line = p->line;
        DC_StringBuilder *sb = dc_sb_new();
        if (!sb) {
            if (p->err) DC_SET_ERROR(p->err, DC_ERROR_MEMORY, "sexpr string alloc");
            return NULL;
        }

        while (*p->pos && *p->pos != '"') {
            if (*p->pos == '\\' && p->pos[1]) {
                p->pos++;
                switch (*p->pos) {
                case 'n':  dc_sb_append(sb, "\n"); break;
                case 't':  dc_sb_append(sb, "\t"); break;
                case '\\': dc_sb_append(sb, "\\"); break;
                case '"':  dc_sb_append(sb, "\""); break;
                default: {
                    char esc[3] = { '\\', *p->pos, '\0' };
                    dc_sb_append(sb, esc);
                    break;
                }
                }
            } else {
                if (*p->pos == '\n') p->line++;
                char ch[2] = { *p->pos, '\0' };
                dc_sb_append(sb, ch);
            }
            p->pos++;
        }

        if (*p->pos == '"') {
            p->pos++;
        } else {
            if (p->err) DC_SET_ERROR(p->err, DC_ERROR_PARSE,
                                      "unclosed string at line %d", start_line);
            dc_sb_free(sb);
            return NULL;
        }

        DC_Sexpr *node = sexpr_new(DC_SEXPR_STRING, start_line);
        if (!node) {
            dc_sb_free(sb);
            if (p->err) DC_SET_ERROR(p->err, DC_ERROR_MEMORY, "sexpr string node alloc");
            return NULL;
        }
        node->value = dc_sb_take(sb);
        dc_sb_free(sb);
        return node;
    }

    /* --- Unquoted atom --- */
    if (is_atom_char(*p->pos)) {
        int start_line = p->line;
        const char *start = p->pos;
        while (is_atom_char(*p->pos)) p->pos++;
        size_t len = (size_t)(p->pos - start);

        DC_Sexpr *node = sexpr_new(DC_SEXPR_ATOM, start_line);
        if (!node) {
            if (p->err) DC_SET_ERROR(p->err, DC_ERROR_MEMORY, "sexpr atom alloc");
            return NULL;
        }
        node->value = malloc(len + 1);
        if (!node->value) {
            dc_sexpr_free(node);
            if (p->err) DC_SET_ERROR(p->err, DC_ERROR_MEMORY, "sexpr atom value alloc");
            return NULL;
        }
        memcpy(node->value, start, len);
        node->value[len] = '\0';
        return node;
    }

    /* --- Unexpected character --- */
    if (p->err) DC_SET_ERROR(p->err, DC_ERROR_PARSE,
                              "unexpected char '%c' at line %d", *p->pos, p->line);
    return NULL;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

DC_Sexpr *
dc_sexpr_parse(const char *text, DC_Error *err)
{
    if (!text) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL input");
        return NULL;
    }

    SexprParser p = {
        .src  = text,
        .pos  = text,
        .line = 1,
        .err  = err,
    };

    DC_Sexpr *root = parse_node(&p);
    if (!root) return NULL;

    /* Check for trailing junk (allow whitespace) */
    skip_whitespace(&p);
    if (*p.pos != '\0') {
        /* There's more content — wrap in a synthetic root list */
        DC_Sexpr *wrapper = sexpr_new(DC_SEXPR_LIST, 1);
        if (!wrapper) {
            dc_sexpr_free(root);
            if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "wrapper alloc");
            return NULL;
        }
        sexpr_add_child(wrapper, root);

        while (*p.pos && *p.pos != '\0') {
            skip_whitespace(&p);
            if (!*p.pos) break;
            DC_Sexpr *extra = parse_node(&p);
            if (!extra) {
                dc_sexpr_free(wrapper);
                return NULL;
            }
            sexpr_add_child(wrapper, extra);
            skip_whitespace(&p);
        }
        return wrapper;
    }

    return root;
}

void
dc_sexpr_free(DC_Sexpr *node)
{
    if (!node) return;
    free(node->value);
    if (node->children) {
        for (size_t i = 0; i < node->child_count; i++) {
            dc_sexpr_free(node->children[i]);
        }
        free(node->children);
    }
    free(node);
}

/* ---- Writer ---- */

static void
sexpr_write_node(const DC_Sexpr *node, DC_StringBuilder *sb, int indent, int pretty)
{
    switch (node->type) {
    case DC_SEXPR_ATOM:
        dc_sb_append(sb, node->value);
        break;

    case DC_SEXPR_STRING:
        dc_sb_append(sb, "\"");
        /* Escape special chars */
        for (const char *c = node->value; *c; c++) {
            switch (*c) {
            case '"':  dc_sb_append(sb, "\\\""); break;
            case '\\': dc_sb_append(sb, "\\\\"); break;
            case '\n': dc_sb_append(sb, "\\n"); break;
            case '\t': dc_sb_append(sb, "\\t"); break;
            default: {
                char ch[2] = { *c, '\0' };
                dc_sb_append(sb, ch);
                break;
            }
            }
        }
        dc_sb_append(sb, "\"");
        break;

    case DC_SEXPR_LIST:
        dc_sb_append(sb, "(");
        for (size_t i = 0; i < node->child_count; i++) {
            if (i > 0) dc_sb_append(sb, " ");

            /* Pretty-print: newline+indent for list children after the tag */
            if (pretty && i > 0 && node->children[i]->type == DC_SEXPR_LIST) {
                dc_sb_append(sb, "\n");
                for (int j = 0; j < indent + 1; j++) dc_sb_append(sb, "  ");
            }

            sexpr_write_node(node->children[i], sb, indent + 1, pretty);
        }
        dc_sb_append(sb, ")");
        break;
    }
}

char *
dc_sexpr_write(const DC_Sexpr *node, DC_Error *err)
{
    if (!node) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL node");
        return NULL;
    }

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "sb alloc");
        return NULL;
    }

    sexpr_write_node(node, sb, 0, 0);
    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}

/* ---- Query helpers ---- */

DC_Sexpr *
dc_sexpr_find(const DC_Sexpr *parent, const char *tag)
{
    if (!parent || parent->type != DC_SEXPR_LIST || !tag) return NULL;
    for (size_t i = 0; i < parent->child_count; i++) {
        DC_Sexpr *child = parent->children[i];
        if (child->type == DC_SEXPR_LIST &&
            child->child_count > 0 &&
            child->children[0]->type == DC_SEXPR_ATOM &&
            strcmp(child->children[0]->value, tag) == 0) {
            return child;
        }
    }
    return NULL;
}

DC_Sexpr **
dc_sexpr_find_all(const DC_Sexpr *parent, const char *tag, size_t *out_count)
{
    if (out_count) *out_count = 0;
    if (!parent || parent->type != DC_SEXPR_LIST || !tag) return NULL;

    /* First pass: count matches */
    size_t count = 0;
    for (size_t i = 0; i < parent->child_count; i++) {
        DC_Sexpr *child = parent->children[i];
        if (child->type == DC_SEXPR_LIST &&
            child->child_count > 0 &&
            child->children[0]->type == DC_SEXPR_ATOM &&
            strcmp(child->children[0]->value, tag) == 0) {
            count++;
        }
    }
    if (count == 0) return NULL;

    DC_Sexpr **result = malloc(count * sizeof(DC_Sexpr *));
    if (!result) return NULL;

    size_t idx = 0;
    for (size_t i = 0; i < parent->child_count; i++) {
        DC_Sexpr *child = parent->children[i];
        if (child->type == DC_SEXPR_LIST &&
            child->child_count > 0 &&
            child->children[0]->type == DC_SEXPR_ATOM &&
            strcmp(child->children[0]->value, tag) == 0) {
            result[idx++] = child;
        }
    }

    if (out_count) *out_count = count;
    return result;
}

const char *
dc_sexpr_tag(const DC_Sexpr *node)
{
    if (!node || node->type != DC_SEXPR_LIST) return NULL;
    if (node->child_count == 0) return NULL;
    if (node->children[0]->type != DC_SEXPR_ATOM) return NULL;
    return node->children[0]->value;
}

const char *
dc_sexpr_value_at(const DC_Sexpr *node, size_t index)
{
    if (!node || node->type != DC_SEXPR_LIST) return NULL;
    /* index 0 = first child after tag = children[1] */
    size_t actual = index + 1;
    if (actual >= node->child_count) return NULL;
    DC_Sexpr *child = node->children[actual];
    if (child->type == DC_SEXPR_LIST) return NULL;
    return child->value;
}

const char *
dc_sexpr_value(const DC_Sexpr *node)
{
    return dc_sexpr_value_at(node, 0);
}

size_t
dc_sexpr_child_count(const DC_Sexpr *node)
{
    if (!node || node->type != DC_SEXPR_LIST) return 0;
    return node->child_count;
}

/* =========================================================================
 * Creation API (Phase 4A)
 * ========================================================================= */

DC_Sexpr *
dc_sexpr_new_atom(const char *value)
{
    DC_Sexpr *n = sexpr_new(DC_SEXPR_ATOM, 0);
    if (!n) return NULL;
    n->value = value ? strdup(value) : strdup("");
    if (!n->value) { free(n); return NULL; }
    return n;
}

DC_Sexpr *
dc_sexpr_new_string(const char *value)
{
    DC_Sexpr *n = sexpr_new(DC_SEXPR_STRING, 0);
    if (!n) return NULL;
    n->value = value ? strdup(value) : strdup("");
    if (!n->value) { free(n); return NULL; }
    return n;
}

DC_Sexpr *
dc_sexpr_new_list(void)
{
    return sexpr_new(DC_SEXPR_LIST, 0);
}

DC_Sexpr *
dc_sexpr_clone(const DC_Sexpr *node)
{
    if (!node) return NULL;

    DC_Sexpr *copy = sexpr_new(node->type, node->line);
    if (!copy) return NULL;

    if (node->value) {
        copy->value = strdup(node->value);
        if (!copy->value) { free(copy); return NULL; }
    }

    if (node->type == DC_SEXPR_LIST && node->child_count > 0) {
        for (size_t i = 0; i < node->child_count; i++) {
            DC_Sexpr *child_copy = dc_sexpr_clone(node->children[i]);
            if (!child_copy) { dc_sexpr_free(copy); return NULL; }
            if (sexpr_add_child(copy, child_copy) != 0) {
                dc_sexpr_free(child_copy);
                dc_sexpr_free(copy);
                return NULL;
            }
        }
    }

    return copy;
}

/* =========================================================================
 * Mutation API
 * ========================================================================= */

int
dc_sexpr_add_child(DC_Sexpr *parent, DC_Sexpr *child)
{
    if (!parent || parent->type != DC_SEXPR_LIST || !child) return -1;
    return sexpr_add_child(parent, child);
}

int
dc_sexpr_remove_child(DC_Sexpr *parent, size_t index)
{
    if (!parent || parent->type != DC_SEXPR_LIST) return -1;
    if (index >= parent->child_count) return -1;

    dc_sexpr_free(parent->children[index]);

    /* Shift remaining children left */
    for (size_t i = index; i + 1 < parent->child_count; i++)
        parent->children[i] = parent->children[i + 1];
    parent->child_count--;
    return 0;
}

int
dc_sexpr_replace_child(DC_Sexpr *parent, size_t index, DC_Sexpr *new_child)
{
    if (!parent || parent->type != DC_SEXPR_LIST || !new_child) return -1;
    if (index >= parent->child_count) return -1;

    dc_sexpr_free(parent->children[index]);
    parent->children[index] = new_child;
    return 0;
}

int
dc_sexpr_set_value(DC_Sexpr *node, const char *new_value)
{
    if (!node || node->type == DC_SEXPR_LIST) return -1;
    char *copy = new_value ? strdup(new_value) : strdup("");
    if (!copy) return -1;
    free(node->value);
    node->value = copy;
    return 0;
}

/* =========================================================================
 * Pretty writer
 * ========================================================================= */

char *
dc_sexpr_write_pretty(const DC_Sexpr *node, DC_Error *err)
{
    if (!node) {
        if (err) DC_SET_ERROR(err, DC_ERROR_INVALID_ARG, "NULL node");
        return NULL;
    }

    DC_StringBuilder *sb = dc_sb_new();
    if (!sb) {
        if (err) DC_SET_ERROR(err, DC_ERROR_MEMORY, "sb alloc");
        return NULL;
    }

    sexpr_write_node(node, sb, 0, 1);
    dc_sb_append(sb, "\n");
    char *result = dc_sb_take(sb);
    dc_sb_free(sb);
    return result;
}
