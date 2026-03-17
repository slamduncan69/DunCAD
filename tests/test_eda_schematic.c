/*
 * test_eda_schematic.c — Tests for schematic data model.
 * No GTK dependency — links only dc_core.
 */

#include "eda/eda_schematic.h"

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
test_new_empty(void)
{
    DC_ESchematic *sch = dc_eschematic_new();
    ASSERT(sch != NULL);
    ASSERT(dc_eschematic_symbol_count(sch) == 0);
    ASSERT(dc_eschematic_wire_count(sch) == 0);
    ASSERT(dc_eschematic_label_count(sch) == 0);
    ASSERT(dc_eschematic_junction_count(sch) == 0);
    dc_eschematic_free(sch);
    return 0;
}

static int
test_add_symbol(void)
{
    DC_ESchematic *sch = dc_eschematic_new();
    size_t idx = dc_eschematic_add_symbol(sch, "Device:R_Small", "R1", 100.0, 50.0);
    ASSERT(idx == 0);
    ASSERT(dc_eschematic_symbol_count(sch) == 1);

    DC_SchSymbol *sym = dc_eschematic_get_symbol(sch, 0);
    ASSERT(sym != NULL);
    ASSERT(strcmp(sym->lib_id, "Device:R_Small") == 0);
    ASSERT(strcmp(sym->reference, "R1") == 0);
    ASSERT(sym->x == 100.0);
    ASSERT(sym->y == 50.0);

    dc_eschematic_free(sch);
    return 0;
}

static int
test_add_wire(void)
{
    DC_ESchematic *sch = dc_eschematic_new();
    size_t idx = dc_eschematic_add_wire(sch, 10.0, 20.0, 30.0, 40.0);
    ASSERT(idx == 0);
    ASSERT(dc_eschematic_wire_count(sch) == 1);

    DC_SchWire *w = dc_eschematic_get_wire(sch, 0);
    ASSERT(w != NULL);
    ASSERT(w->x1 == 10.0);
    ASSERT(w->y2 == 40.0);

    dc_eschematic_free(sch);
    return 0;
}

static int
test_add_label(void)
{
    DC_ESchematic *sch = dc_eschematic_new();
    size_t idx = dc_eschematic_add_label(sch, "VCC", 50.0, 30.0);
    ASSERT(idx == 0);
    ASSERT(dc_eschematic_label_count(sch) == 1);

    DC_SchLabel *l = dc_eschematic_get_label(sch, 0);
    ASSERT(strcmp(l->name, "VCC") == 0);

    dc_eschematic_free(sch);
    return 0;
}

static int
test_find_symbol(void)
{
    DC_ESchematic *sch = dc_eschematic_new();
    dc_eschematic_add_symbol(sch, "Device:R_Small", "R1", 100.0, 50.0);
    dc_eschematic_add_symbol(sch, "Device:LED_Small", "D1", 130.0, 50.0);

    DC_SchSymbol *found = dc_eschematic_find_symbol(sch, "D1");
    ASSERT(found != NULL);
    ASSERT(strcmp(found->lib_id, "Device:LED_Small") == 0);

    ASSERT(dc_eschematic_find_symbol(sch, "R99") == NULL);

    dc_eschematic_free(sch);
    return 0;
}

static int
test_set_property(void)
{
    DC_ESchematic *sch = dc_eschematic_new();
    dc_eschematic_add_symbol(sch, "Device:R_Small", "R1", 100.0, 50.0);

    int rc = dc_eschematic_set_property(sch, 0, "Value", "10k");
    ASSERT(rc == 0);

    DC_SchSymbol *sym = dc_eschematic_get_symbol(sch, 0);
    const char *val = dc_eschematic_symbol_property(sym, "Value");
    ASSERT(val != NULL);
    ASSERT(strcmp(val, "10k") == 0);

    dc_eschematic_free(sch);
    return 0;
}

static int
test_load_kicad_sch(void)
{
    DC_Error err = {0};
    DC_ESchematic *sch = dc_eschematic_load(DC_TEST_DATA_DIR "/simple.kicad_sch", &err);
    ASSERT(sch != NULL);

    /* Should have 2 symbols (R1 and D1) */
    ASSERT(dc_eschematic_symbol_count(sch) == 2);

    /* Verify R1 */
    DC_SchSymbol *r1 = dc_eschematic_find_symbol(sch, "R1");
    ASSERT(r1 != NULL);
    ASSERT(strcmp(r1->lib_id, "Device:R_Small") == 0);
    ASSERT(r1->x == 100.0);
    ASSERT(r1->y == 50.0);

    const char *r1_val = dc_eschematic_symbol_property(r1, "Value");
    ASSERT(r1_val != NULL);
    ASSERT(strcmp(r1_val, "470") == 0);

    /* Verify D1 */
    DC_SchSymbol *d1 = dc_eschematic_find_symbol(sch, "D1");
    ASSERT(d1 != NULL);
    ASSERT(strcmp(d1->lib_id, "Device:LED_Small") == 0);

    /* Should have 3 wires */
    ASSERT(dc_eschematic_wire_count(sch) == 3);

    /* Should have 3 labels */
    ASSERT(dc_eschematic_label_count(sch) == 3);

    /* Should have 1 junction */
    ASSERT(dc_eschematic_junction_count(sch) == 1);

    dc_eschematic_free(sch);
    return 0;
}

static int
test_remove_symbol(void)
{
    DC_ESchematic *sch = dc_eschematic_new();
    dc_eschematic_add_symbol(sch, "Device:R_Small", "R1", 100.0, 50.0);
    dc_eschematic_add_symbol(sch, "Device:LED_Small", "D1", 130.0, 50.0);
    ASSERT(dc_eschematic_symbol_count(sch) == 2);

    int rc = dc_eschematic_remove_symbol(sch, 0);
    ASSERT(rc == 0);
    ASSERT(dc_eschematic_symbol_count(sch) == 1);

    dc_eschematic_free(sch);
    return 0;
}

static int
test_serialize_new(void)
{
    DC_ESchematic *sch = dc_eschematic_new();
    dc_eschematic_add_symbol(sch, "Device:R_Small", "R1", 100.0, 50.0);
    dc_eschematic_add_wire(sch, 100.0, 48.0, 100.0, 40.0);
    dc_eschematic_add_label(sch, "VCC", 100.0, 40.0);

    DC_Error err = {0};
    char *text = dc_eschematic_to_sexpr_string(sch, &err);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "kicad_sch") != NULL);
    ASSERT(strstr(text, "Device:R_Small") != NULL);
    ASSERT(strstr(text, "VCC") != NULL);

    free(text);
    dc_eschematic_free(sch);
    return 0;
}

static int
test_load_not_found(void)
{
    DC_Error err = {0};
    DC_ESchematic *sch = dc_eschematic_load("/nonexistent/path.kicad_sch", &err);
    ASSERT(sch == NULL);
    ASSERT(err.code == DC_ERROR_IO);
    return 0;
}

/* ---- main ---- */
int
main(void)
{
    fprintf(stderr, "=== test_eda_schematic ===\n");

    RUN_TEST(test_new_empty);
    RUN_TEST(test_add_symbol);
    RUN_TEST(test_add_wire);
    RUN_TEST(test_add_label);
    RUN_TEST(test_find_symbol);
    RUN_TEST(test_set_property);
    RUN_TEST(test_load_kicad_sch);
    RUN_TEST(test_remove_symbol);
    RUN_TEST(test_serialize_new);
    RUN_TEST(test_load_not_found);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
