/*
 * test_eda_pcb.c — Tests for PCB data model.
 * No GTK dependency — links only dc_core.
 */

#include "eda/eda_pcb.h"

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
    DC_EPcb *pcb = dc_epcb_new();
    ASSERT(pcb != NULL);
    ASSERT(dc_epcb_footprint_count(pcb) == 0);
    ASSERT(dc_epcb_track_count(pcb) == 0);
    ASSERT(dc_epcb_via_count(pcb) == 0);
    ASSERT(dc_epcb_net_count(pcb) == 1); /* net 0 always exists */
    dc_epcb_free(pcb);
    return 0;
}

static int
test_add_footprint(void)
{
    DC_EPcb *pcb = dc_epcb_new();
    size_t idx = dc_epcb_add_footprint(pcb, "Resistor_SMD:R_0402",
                                         "R1", 100.0, 50.0, DC_PCB_LAYER_F_CU);
    ASSERT(idx == 0);
    ASSERT(dc_epcb_footprint_count(pcb) == 1);

    DC_PcbFootprint *fp = dc_epcb_get_footprint(pcb, 0);
    ASSERT(fp != NULL);
    ASSERT(strcmp(fp->lib_id, "Resistor_SMD:R_0402") == 0);
    ASSERT(strcmp(fp->reference, "R1") == 0);
    ASSERT(fp->x == 100.0);
    ASSERT(fp->layer == DC_PCB_LAYER_F_CU);

    dc_epcb_free(pcb);
    return 0;
}

static int
test_add_track(void)
{
    DC_EPcb *pcb = dc_epcb_new();
    size_t idx = dc_epcb_add_track(pcb, 10.0, 20.0, 30.0, 20.0, 0.25,
                                     DC_PCB_LAYER_F_CU, 1);
    ASSERT(idx == 0);
    ASSERT(dc_epcb_track_count(pcb) == 1);

    DC_PcbTrack *t = dc_epcb_get_track(pcb, 0);
    ASSERT(t != NULL);
    ASSERT(t->width == 0.25);
    ASSERT(t->net_id == 1);

    dc_epcb_free(pcb);
    return 0;
}

static int
test_add_via(void)
{
    DC_EPcb *pcb = dc_epcb_new();
    size_t idx = dc_epcb_add_via(pcb, 50.0, 50.0, 0.8, 0.4, 1);
    ASSERT(idx == 0);
    ASSERT(dc_epcb_via_count(pcb) == 1);

    DC_PcbVia *v = dc_epcb_get_via(pcb, 0);
    ASSERT(v != NULL);
    ASSERT(v->size == 0.8);
    ASSERT(v->drill == 0.4);

    dc_epcb_free(pcb);
    return 0;
}

static int
test_add_net(void)
{
    DC_EPcb *pcb = dc_epcb_new();
    int id = dc_epcb_add_net(pcb, "VCC");
    ASSERT(id == 1); /* 0 is reserved */
    ASSERT(dc_epcb_net_count(pcb) == 2);

    ASSERT(dc_epcb_find_net(pcb, "VCC") == 1);
    ASSERT(dc_epcb_find_net(pcb, "nonexistent") == -1);

    dc_epcb_free(pcb);
    return 0;
}

static int
test_find_footprint(void)
{
    DC_EPcb *pcb = dc_epcb_new();
    dc_epcb_add_footprint(pcb, "R", "R1", 10, 10, DC_PCB_LAYER_F_CU);
    dc_epcb_add_footprint(pcb, "D", "D1", 20, 20, DC_PCB_LAYER_F_CU);

    DC_PcbFootprint *fp = dc_epcb_find_footprint(pcb, "D1");
    ASSERT(fp != NULL);
    ASSERT(strcmp(fp->lib_id, "D") == 0);

    ASSERT(dc_epcb_find_footprint(pcb, "U99") == NULL);

    dc_epcb_free(pcb);
    return 0;
}

static int
test_layer_names(void)
{
    ASSERT(dc_pcb_layer_from_name("F.Cu") == DC_PCB_LAYER_F_CU);
    ASSERT(dc_pcb_layer_from_name("B.Cu") == DC_PCB_LAYER_B_CU);
    ASSERT(dc_pcb_layer_from_name("Edge.Cuts") == DC_PCB_LAYER_EDGE_CUTS);
    ASSERT(dc_pcb_layer_from_name("F.SilkS") == DC_PCB_LAYER_F_SILKS);
    ASSERT(dc_pcb_layer_from_name("Bogus") == -1);

    ASSERT(strcmp(dc_pcb_layer_to_name(DC_PCB_LAYER_F_CU), "F.Cu") == 0);
    ASSERT(strcmp(dc_pcb_layer_to_name(DC_PCB_LAYER_B_CU), "B.Cu") == 0);
    return 0;
}

static int
test_design_rules(void)
{
    DC_EPcb *pcb = dc_epcb_new();
    DC_PcbDesignRules *rules = dc_epcb_get_design_rules(pcb);
    ASSERT(rules != NULL);
    ASSERT(rules->clearance > 0);
    ASSERT(rules->track_width > 0);
    ASSERT(rules->via_size > 0);
    dc_epcb_free(pcb);
    return 0;
}

static int
test_load_kicad_pcb(void)
{
    DC_Error err = {0};
    DC_EPcb *pcb = dc_epcb_load(DC_TEST_DATA_DIR "/simple.kicad_pcb", &err);
    ASSERT(pcb != NULL);

    /* 2 footprints */
    ASSERT(dc_epcb_footprint_count(pcb) == 2);

    /* Verify R1 */
    DC_PcbFootprint *r1 = dc_epcb_find_footprint(pcb, "R1");
    ASSERT(r1 != NULL);
    ASSERT(strcmp(r1->reference, "R1") == 0);

    /* Verify D1 */
    DC_PcbFootprint *d1 = dc_epcb_find_footprint(pcb, "D1");
    ASSERT(d1 != NULL);

    /* 2 tracks (segments) */
    ASSERT(dc_epcb_track_count(pcb) == 2);

    /* 1 via */
    ASSERT(dc_epcb_via_count(pcb) == 1);

    /* Nets: 0 (empty) + VCC + GND + SIG = 4 */
    ASSERT(dc_epcb_net_count(pcb) == 4);
    ASSERT(dc_epcb_find_net(pcb, "VCC") >= 0);
    ASSERT(dc_epcb_find_net(pcb, "GND") >= 0);
    ASSERT(dc_epcb_find_net(pcb, "SIG") >= 0);

    dc_epcb_free(pcb);
    return 0;
}

static int
test_load_not_found(void)
{
    DC_Error err = {0};
    DC_EPcb *pcb = dc_epcb_load("/nonexistent/path.kicad_pcb", &err);
    ASSERT(pcb == NULL);
    ASSERT(err.code == DC_ERROR_IO);
    return 0;
}

/* ---- main ---- */
int
main(void)
{
    fprintf(stderr, "=== test_eda_pcb ===\n");

    RUN_TEST(test_new_empty);
    RUN_TEST(test_add_footprint);
    RUN_TEST(test_add_track);
    RUN_TEST(test_add_via);
    RUN_TEST(test_add_net);
    RUN_TEST(test_find_footprint);
    RUN_TEST(test_layer_names);
    RUN_TEST(test_design_rules);
    RUN_TEST(test_load_kicad_pcb);
    RUN_TEST(test_load_not_found);

    fprintf(stderr, "=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
