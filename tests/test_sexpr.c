/*
 * test_sexpr.c — Tests for the s-expression parser.
 * No GTK dependency — links only dc_core.
 */

#include "eda/sexpr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Minimal test framework ---- */
static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        fprintf(stderr, "  %-40s ", #fn); \
        int r = fn(); \
        if (r == 0) { fprintf(stderr, "PASS\n"); g_pass++; } \
        else        { fprintf(stderr, "(see above)\n"); g_fail++; } \
    } while (0)

/* ---- Tests ---- */

static int
test_parse_atom(void)
{
    DC_Error err = {0};
    DC_Sexpr *n = dc_sexpr_parse("hello", &err);
    ASSERT(n != NULL);
    ASSERT(err.code == DC_OK);
    ASSERT(n->type == DC_SEXPR_ATOM);
    ASSERT(strcmp(n->value, "hello") == 0);
    dc_sexpr_free(n);
    return 0;
}

static int
test_parse_string(void)
{
    DC_Error err = {0};
    DC_Sexpr *n = dc_sexpr_parse("\"hello world\"", &err);
    ASSERT(n != NULL);
    ASSERT(n->type == DC_SEXPR_STRING);
    ASSERT(strcmp(n->value, "hello world") == 0);
    dc_sexpr_free(n);
    return 0;
}

static int
test_parse_simple_list(void)
{
    DC_Error err = {0};
    DC_Sexpr *n = dc_sexpr_parse("(foo bar 42)", &err);
    ASSERT(n != NULL);
    ASSERT(err.code == DC_OK);
    ASSERT(n->type == DC_SEXPR_LIST);
    ASSERT(n->child_count == 3);
    ASSERT(n->children[0]->type == DC_SEXPR_ATOM);
    ASSERT(strcmp(n->children[0]->value, "foo") == 0);
    ASSERT(strcmp(n->children[1]->value, "bar") == 0);
    ASSERT(strcmp(n->children[2]->value, "42") == 0);
    dc_sexpr_free(n);
    return 0;
}

static int
test_parse_nested_list(void)
{
    DC_Error err = {0};
    DC_Sexpr *n = dc_sexpr_parse("(a (b c) (d (e f)))", &err);
    ASSERT(n != NULL);
    ASSERT(n->type == DC_SEXPR_LIST);
    ASSERT(n->child_count == 3);

    /* Second child: (b c) */
    ASSERT(n->children[1]->type == DC_SEXPR_LIST);
    ASSERT(n->children[1]->child_count == 2);
    ASSERT(strcmp(n->children[1]->children[0]->value, "b") == 0);

    /* Third child: (d (e f)) */
    ASSERT(n->children[2]->type == DC_SEXPR_LIST);
    ASSERT(n->children[2]->child_count == 2);
    DC_Sexpr *ef = n->children[2]->children[1];
    ASSERT(ef->type == DC_SEXPR_LIST);
    ASSERT(ef->child_count == 2);
    ASSERT(strcmp(ef->children[0]->value, "e") == 0);

    dc_sexpr_free(n);
    return 0;
}

static int
test_parse_string_with_escapes(void)
{
    DC_Error err = {0};
    DC_Sexpr *n = dc_sexpr_parse("(msg \"hello\\nworld\\t!\")", &err);
    ASSERT(n != NULL);
    ASSERT(n->type == DC_SEXPR_LIST);
    ASSERT(n->child_count == 2);
    ASSERT(n->children[1]->type == DC_SEXPR_STRING);
    ASSERT(strstr(n->children[1]->value, "\n") != NULL);
    ASSERT(strstr(n->children[1]->value, "\t") != NULL);
    dc_sexpr_free(n);
    return 0;
}

static int
test_parse_kicad_like(void)
{
    const char *src =
        "(kicad_sch\n"
        "  (version 20230121)\n"
        "  (generator \"eeschema\")\n"
        "  (uuid \"abc-123\")\n"
        "  (symbol\n"
        "    (lib_id \"Device:R_Small\")\n"
        "    (at 100 50 0)\n"
        "  )\n"
        ")";

    DC_Error err = {0};
    DC_Sexpr *root = dc_sexpr_parse(src, &err);
    ASSERT(root != NULL);
    ASSERT(err.code == DC_OK);

    /* Tag */
    ASSERT(strcmp(dc_sexpr_tag(root), "kicad_sch") == 0);

    /* Find version */
    DC_Sexpr *ver = dc_sexpr_find(root, "version");
    ASSERT(ver != NULL);
    ASSERT(strcmp(dc_sexpr_value(ver), "20230121") == 0);

    /* Find generator */
    DC_Sexpr *gen = dc_sexpr_find(root, "generator");
    ASSERT(gen != NULL);
    ASSERT(strcmp(dc_sexpr_value(gen), "eeschema") == 0);

    /* Find symbol */
    DC_Sexpr *sym = dc_sexpr_find(root, "symbol");
    ASSERT(sym != NULL);
    DC_Sexpr *lid = dc_sexpr_find(sym, "lib_id");
    ASSERT(lid != NULL);
    ASSERT(strcmp(dc_sexpr_value(lid), "Device:R_Small") == 0);

    /* at */
    DC_Sexpr *at = dc_sexpr_find(sym, "at");
    ASSERT(at != NULL);
    ASSERT(strcmp(dc_sexpr_value_at(at, 0), "100") == 0);
    ASSERT(strcmp(dc_sexpr_value_at(at, 1), "50") == 0);

    dc_sexpr_free(root);
    return 0;
}

static int
test_roundtrip_write(void)
{
    const char *src = "(a (b \"c d\") (e 42))";
    DC_Error err = {0};
    DC_Sexpr *root = dc_sexpr_parse(src, &err);
    ASSERT(root != NULL);

    char *out = dc_sexpr_write(root, &err);
    ASSERT(out != NULL);

    /* Parse the output again */
    DC_Sexpr *root2 = dc_sexpr_parse(out, &err);
    ASSERT(root2 != NULL);
    ASSERT(root2->type == DC_SEXPR_LIST);
    ASSERT(root2->child_count == 3);
    ASSERT(strcmp(dc_sexpr_tag(root2), "a") == 0);

    dc_sexpr_free(root);
    dc_sexpr_free(root2);
    free(out);
    return 0;
}

static int
test_find_all(void)
{
    const char *src = "(root (item 1) (item 2) (other 3) (item 4))";
    DC_Sexpr *root = dc_sexpr_parse(src, NULL);
    ASSERT(root != NULL);

    size_t count = 0;
    DC_Sexpr **items = dc_sexpr_find_all(root, "item", &count);
    ASSERT(items != NULL);
    ASSERT(count == 3);
    ASSERT(strcmp(dc_sexpr_value(items[0]), "1") == 0);
    ASSERT(strcmp(dc_sexpr_value(items[1]), "2") == 0);
    ASSERT(strcmp(dc_sexpr_value(items[2]), "4") == 0);

    free(items);
    dc_sexpr_free(root);
    return 0;
}

static int
test_parse_error_unclosed(void)
{
    DC_Error err = {0};
    DC_Sexpr *n = dc_sexpr_parse("(a (b c)", &err);
    ASSERT(n == NULL);
    ASSERT(err.code == DC_ERROR_PARSE);
    return 0;
}

static int
test_parse_null(void)
{
    DC_Error err = {0};
    DC_Sexpr *n = dc_sexpr_parse(NULL, &err);
    ASSERT(n == NULL);
    ASSERT(err.code == DC_ERROR_INVALID_ARG);
    return 0;
}

static int
test_child_count(void)
{
    DC_Sexpr *n = dc_sexpr_parse("(a b c d e)", NULL);
    ASSERT(n != NULL);
    ASSERT(dc_sexpr_child_count(n) == 5);
    dc_sexpr_free(n);

    /* Atom has 0 children */
    n = dc_sexpr_parse("hello", NULL);
    ASSERT(n != NULL);
    ASSERT(dc_sexpr_child_count(n) == 0);
    dc_sexpr_free(n);
    return 0;
}

static int
test_layer_name_atom(void)
{
    /* KiCad layer names like F.Cu are atoms (unquoted) */
    DC_Sexpr *n = dc_sexpr_parse("(layer F.Cu)", NULL);
    ASSERT(n != NULL);
    ASSERT(n->type == DC_SEXPR_LIST);
    ASSERT(n->child_count == 2);
    ASSERT(strcmp(n->children[1]->value, "F.Cu") == 0);
    dc_sexpr_free(n);
    return 0;
}

/* ---- main ---- */
int
main(void)
{
    fprintf(stderr, "=== test_sexpr ===\n");

    RUN_TEST(test_parse_atom);
    RUN_TEST(test_parse_string);
    RUN_TEST(test_parse_simple_list);
    RUN_TEST(test_parse_nested_list);
    RUN_TEST(test_parse_string_with_escapes);
    RUN_TEST(test_parse_kicad_like);
    RUN_TEST(test_roundtrip_write);
    RUN_TEST(test_find_all);
    RUN_TEST(test_parse_error_unclosed);
    RUN_TEST(test_parse_null);
    RUN_TEST(test_child_count);
    RUN_TEST(test_layer_name_atom);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
