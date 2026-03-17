/*
 * test_eda_library.c — Tests for KiCad symbol/footprint library loader.
 * No GTK dependency — links only dc_core.
 */

#include "eda/eda_library.h"

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
    DC_ELibrary *lib = dc_elibrary_new();
    ASSERT(lib != NULL);
    ASSERT(dc_elibrary_symbol_count(lib) == 0);
    dc_elibrary_free(lib);
    return 0;
}

static int
test_load_symbols(void)
{
    DC_ELibrary *lib = dc_elibrary_new();
    DC_Error err = {0};

    int rc = dc_elibrary_load_symbols(lib, DC_TEST_DATA_DIR "/Device.kicad_sym", &err);
    ASSERT(rc == 0);

    /* Should have loaded R_Small, LED_Small, C_Small */
    ASSERT(dc_elibrary_symbol_count(lib) >= 3);

    dc_elibrary_free(lib);
    return 0;
}

static int
test_find_symbol_by_name(void)
{
    DC_ELibrary *lib = dc_elibrary_new();
    dc_elibrary_load_symbols(lib, DC_TEST_DATA_DIR "/Device.kicad_sym", NULL);

    const DC_Sexpr *sym = dc_elibrary_find_symbol_by_name(lib, "R_Small");
    ASSERT(sym != NULL);
    ASSERT(sym->type == DC_SEXPR_LIST);

    /* Should be a (symbol "R_Small" ...) node */
    const char *tag = dc_sexpr_tag(sym);
    ASSERT(tag != NULL);
    ASSERT(strcmp(tag, "symbol") == 0);

    const char *name = dc_sexpr_value(sym);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "R_Small") == 0);

    dc_elibrary_free(lib);
    return 0;
}

static int
test_find_symbol_by_lib_id(void)
{
    DC_ELibrary *lib = dc_elibrary_new();
    dc_elibrary_load_symbols(lib, DC_TEST_DATA_DIR "/Device.kicad_sym", NULL);

    const DC_Sexpr *sym = dc_elibrary_find_symbol(lib, "Device:R_Small");
    ASSERT(sym != NULL);

    const DC_Sexpr *led = dc_elibrary_find_symbol(lib, "Device:LED_Small");
    ASSERT(led != NULL);

    const DC_Sexpr *cap = dc_elibrary_find_symbol(lib, "Device:C_Small");
    ASSERT(cap != NULL);

    /* Not found */
    ASSERT(dc_elibrary_find_symbol(lib, "Device:Nonexistent") == NULL);
    ASSERT(dc_elibrary_find_symbol(lib, "WrongLib:R_Small") == NULL);

    dc_elibrary_free(lib);
    return 0;
}

static int
test_symbol_has_pins(void)
{
    DC_ELibrary *lib = dc_elibrary_new();
    dc_elibrary_load_symbols(lib, DC_TEST_DATA_DIR "/Device.kicad_sym", NULL);

    const DC_Sexpr *sym = dc_elibrary_find_symbol(lib, "Device:R_Small");
    ASSERT(sym != NULL);

    /* R_Small should have sub-symbols containing pins */
    size_t sub_count = 0;
    DC_Sexpr **subs = dc_sexpr_find_all(sym, "symbol", &sub_count);
    ASSERT(subs != NULL);
    ASSERT(sub_count >= 1);

    /* Find the pin-carrying sub-symbol */
    int found_pins = 0;
    for (size_t i = 0; i < sub_count; i++) {
        size_t pin_count = 0;
        DC_Sexpr **pins = dc_sexpr_find_all(subs[i], "pin", &pin_count);
        if (pins) {
            found_pins += (int)pin_count;
            free(pins);
        }
    }
    ASSERT(found_pins >= 2); /* R_Small has 2 pins */

    free(subs);
    dc_elibrary_free(lib);
    return 0;
}

static int
test_symbol_names(void)
{
    DC_ELibrary *lib = dc_elibrary_new();
    dc_elibrary_load_symbols(lib, DC_TEST_DATA_DIR "/Device.kicad_sym", NULL);

    size_t count = dc_elibrary_symbol_count(lib);
    ASSERT(count >= 3);

    /* Check that we can retrieve names */
    int found_r = 0, found_led = 0, found_c = 0;
    for (size_t i = 0; i < count; i++) {
        const char *name = dc_elibrary_symbol_name(lib, i);
        ASSERT(name != NULL);
        if (strcmp(name, "R_Small") == 0) found_r = 1;
        if (strcmp(name, "LED_Small") == 0) found_led = 1;
        if (strcmp(name, "C_Small") == 0) found_c = 1;
    }
    ASSERT(found_r);
    ASSERT(found_led);
    ASSERT(found_c);

    dc_elibrary_free(lib);
    return 0;
}

static int
test_load_not_found(void)
{
    DC_ELibrary *lib = dc_elibrary_new();
    DC_Error err = {0};
    int rc = dc_elibrary_load_symbols(lib, "/nonexistent/path.kicad_sym", &err);
    ASSERT(rc == -1);
    ASSERT(err.code == DC_ERROR_IO);
    dc_elibrary_free(lib);
    return 0;
}

/* ---- main ---- */
int
main(void)
{
    fprintf(stderr, "=== test_eda_library ===\n");

    RUN_TEST(test_new_empty);
    RUN_TEST(test_load_symbols);
    RUN_TEST(test_find_symbol_by_name);
    RUN_TEST(test_find_symbol_by_lib_id);
    RUN_TEST(test_symbol_has_pins);
    RUN_TEST(test_symbol_names);
    RUN_TEST(test_load_not_found);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
