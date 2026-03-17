#define _POSIX_C_SOURCE 200809L

#include "cubeiform/cubeiform_eda.h"
#include "eda/eda_cubeiform_export.h"
#include "eda/eda_schematic.h"
#include "eda/eda_pcb.h"
#include "core/error.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal test harness */
static int s_pass = 0, s_fail = 0;
#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    name(); \
    printf(" PASS\n"); \
    s_pass++; \
} while (0)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAIL\n    assertion failed: %s (%s:%d)\n", \
               #cond, __FILE__, __LINE__); \
        s_fail++; return; \
    } \
} while (0)

/* =========================================================================
 * Helper: read file to string
 * ========================================================================= */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* =========================================================================
 * Tests — Parsing
 * ========================================================================= */

TEST(test_parse_empty)
{
    DC_Error err = {0};
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda("", &err);
    ASSERT(eda != NULL);
    ASSERT(dc_cubeiform_eda_sch_op_count(eda) == 0);
    ASSERT(dc_cubeiform_eda_pcb_op_count(eda) == 0);
    dc_cubeiform_eda_free(eda);
}

TEST(test_parse_null)
{
    DC_Error err = {0};
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(NULL, &err);
    ASSERT(eda == NULL);
    ASSERT(err.code == DC_ERROR_INVALID_ARG);
}

TEST(test_parse_schematic_component)
{
    DC_Error err = {0};
    const char *src = "schematic { component R1 = \"Device:R_Small\" at 100, 50; }";
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);
    ASSERT(dc_cubeiform_eda_sch_op_count(eda) == 1);

    const DC_SchOp *op = dc_cubeiform_eda_get_sch_op(eda, 0);
    ASSERT(op != NULL);
    ASSERT(op->type == DC_SCH_OP_ADD_COMPONENT);
    ASSERT(strcmp(op->ref, "R1") == 0);
    ASSERT(strcmp(op->lib_id, "Device:R_Small") == 0);
    ASSERT(op->x == 100.0);
    ASSERT(op->y == 50.0);

    dc_cubeiform_eda_free(eda);
}

TEST(test_parse_schematic_wire)
{
    DC_Error err = {0};
    const char *src = "schematic { wire SIG: R1.2, LED1.1; }";
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);
    /* 2 pins → 1 wire segment between consecutive pin pairs */
    ASSERT(dc_cubeiform_eda_sch_op_count(eda) == 1);

    const DC_SchOp *op = dc_cubeiform_eda_get_sch_op(eda, 0);
    ASSERT(op != NULL);
    ASSERT(op->type == DC_SCH_OP_ADD_WIRE);
    ASSERT(strcmp(op->name, "SIG") == 0);
    ASSERT(strcmp(op->ref, "R1.2") == 0);
    ASSERT(strcmp(op->str_value, "LED1.1") == 0);

    dc_cubeiform_eda_free(eda);
}

TEST(test_parse_schematic_power)
{
    DC_Error err = {0};
    const char *src = "schematic { power VCC at 50, 30; }";
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);
    ASSERT(dc_cubeiform_eda_sch_op_count(eda) == 1);

    const DC_SchOp *op = dc_cubeiform_eda_get_sch_op(eda, 0);
    ASSERT(op != NULL);
    ASSERT(op->type == DC_SCH_OP_ADD_POWER);
    ASSERT(strcmp(op->name, "VCC") == 0);
    ASSERT(op->x == 50.0);
    ASSERT(op->y == 30.0);

    dc_cubeiform_eda_free(eda);
}

TEST(test_parse_pipe_value_footprint)
{
    DC_Error err = {0};
    const char *src = "schematic { R1 >> value(\"10k\") >> footprint(\"R_0402\"); }";
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);
    ASSERT(dc_cubeiform_eda_sch_op_count(eda) == 2);

    const DC_SchOp *op0 = dc_cubeiform_eda_get_sch_op(eda, 0);
    ASSERT(op0->type == DC_SCH_OP_SET_VALUE);
    ASSERT(strcmp(op0->ref, "R1") == 0);
    ASSERT(strcmp(op0->str_value, "10k") == 0);

    const DC_SchOp *op1 = dc_cubeiform_eda_get_sch_op(eda, 1);
    ASSERT(op1->type == DC_SCH_OP_SET_FOOTPRINT);
    ASSERT(strcmp(op1->ref, "R1") == 0);
    ASSERT(strcmp(op1->str_value, "R_0402") == 0);

    dc_cubeiform_eda_free(eda);
}

TEST(test_parse_pcb_outline)
{
    DC_Error err = {0};
    const char *src = "pcb { outline { rect(50, 30); } }";
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);
    ASSERT(dc_cubeiform_eda_pcb_op_count(eda) == 1);

    const DC_PcbOp *op = dc_cubeiform_eda_get_pcb_op(eda, 0);
    ASSERT(op->type == DC_PCB_OP_SET_OUTLINE_RECT);
    ASSERT(op->x == 50.0);
    ASSERT(op->y == 30.0);

    dc_cubeiform_eda_free(eda);
}

TEST(test_parse_pcb_rules)
{
    DC_Error err = {0};
    const char *src = "pcb { rules { clearance = 0.15; track_width = 0.2; } }";
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);
    ASSERT(dc_cubeiform_eda_pcb_op_count(eda) == 2);

    const DC_PcbOp *op0 = dc_cubeiform_eda_get_pcb_op(eda, 0);
    ASSERT(op0->type == DC_PCB_OP_SET_RULE);
    ASSERT(strcmp(op0->rule_key, "clearance") == 0);
    ASSERT(op0->value == 0.15);

    const DC_PcbOp *op1 = dc_cubeiform_eda_get_pcb_op(eda, 1);
    ASSERT(op1->type == DC_PCB_OP_SET_RULE);
    ASSERT(strcmp(op1->rule_key, "track_width") == 0);
    ASSERT(op1->value == 0.2);

    dc_cubeiform_eda_free(eda);
}

TEST(test_parse_pcb_place)
{
    DC_Error err = {0};
    const char *src = "pcb { place U1 at 30, 15 on F.Cu >> rotate(45); }";
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);
    ASSERT(dc_cubeiform_eda_pcb_op_count(eda) == 1);

    const DC_PcbOp *op = dc_cubeiform_eda_get_pcb_op(eda, 0);
    ASSERT(op->type == DC_PCB_OP_PLACE);
    ASSERT(strcmp(op->ref, "U1") == 0);
    ASSERT(op->x == 30.0);
    ASSERT(op->y == 15.0);
    ASSERT(op->layer == 0); /* F.Cu = 0 */
    ASSERT(op->angle == 45.0);

    dc_cubeiform_eda_free(eda);
}

TEST(test_parse_pcb_route)
{
    DC_Error err = {0};
    const char *src =
        "pcb { route SIG layer F.Cu width 0.2 { from 10, 10; to 20, 15; to 30, 15; } }";
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);
    ASSERT(dc_cubeiform_eda_pcb_op_count(eda) == 2); /* 2 segments */

    const DC_PcbOp *op0 = dc_cubeiform_eda_get_pcb_op(eda, 0);
    ASSERT(op0->type == DC_PCB_OP_ROUTE_SEGMENT);
    ASSERT(op0->x == 10.0 && op0->y == 10.0);
    ASSERT(op0->x2 == 20.0 && op0->y2 == 15.0);
    ASSERT(op0->width == 0.2);

    const DC_PcbOp *op1 = dc_cubeiform_eda_get_pcb_op(eda, 1);
    ASSERT(op1->x == 20.0 && op1->y == 15.0);
    ASSERT(op1->x2 == 30.0 && op1->y2 == 15.0);

    dc_cubeiform_eda_free(eda);
}

TEST(test_parse_pcb_zone)
{
    DC_Error err = {0};
    const char *src = "pcb { zone GND layer F.Cu { rect(0, 0, 50, 30); } }";
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);
    ASSERT(dc_cubeiform_eda_pcb_op_count(eda) == 1);

    const DC_PcbOp *op = dc_cubeiform_eda_get_pcb_op(eda, 0);
    ASSERT(op->type == DC_PCB_OP_ADD_ZONE);
    ASSERT(strcmp(op->name, "GND") == 0);
    ASSERT(op->x == 0.0 && op->y == 0.0);
    ASSERT(op->x2 == 50.0 && op->y2 == 30.0);

    dc_cubeiform_eda_free(eda);
}

/* =========================================================================
 * Tests — Full file parse
 * ========================================================================= */

TEST(test_parse_full_file)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/simple_circuit.dcad", DC_TEST_DATA_DIR);
    char *src = read_file(path);
    ASSERT(src != NULL);

    DC_Error err = {0};
    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);

    /* Schematic: 2 components + 1 wire + 2 value ops + 2 footprint ops + 2 power ports */
    ASSERT(dc_cubeiform_eda_sch_op_count(eda) >= 7);

    /* PCB: 1 outline + 4 rules + 2 place + 1 route segment + 1 zone */
    ASSERT(dc_cubeiform_eda_pcb_op_count(eda) >= 8);

    dc_cubeiform_eda_free(eda);
    free(src);
}

/* =========================================================================
 * Tests — Apply + Execute
 * ========================================================================= */

TEST(test_apply_schematic)
{
    DC_Error err = {0};
    const char *src =
        "schematic {\n"
        "    component R1 = \"Device:R_Small\" at 100, 50;\n"
        "    component LED1 = \"Device:LED\" at 150, 50;\n"
        "    power VCC at 50, 30;\n"
        "}";

    DC_ESchematic *sch = dc_eschematic_new();
    ASSERT(sch != NULL);

    int rc = dc_cubeiform_execute(src, sch, NULL, NULL, NULL, &err);
    ASSERT(rc == 0);
    ASSERT(dc_eschematic_symbol_count(sch) == 2);
    ASSERT(dc_eschematic_power_port_count(sch) == 1);

    DC_SchSymbol *r1 = dc_eschematic_find_symbol(sch, "R1");
    ASSERT(r1 != NULL);
    ASSERT(r1->x == 100.0);

    dc_eschematic_free(sch);
}

TEST(test_apply_pcb)
{
    DC_Error err = {0};
    const char *src =
        "pcb {\n"
        "    outline { rect(50, 30); }\n"
        "    rules { clearance = 0.15; track_width = 0.2; }\n"
        "    place R1 at 10, 15 on F.Cu;\n"
        "}";

    DC_EPcb *pcb = dc_epcb_new();
    ASSERT(pcb != NULL);

    int rc = dc_cubeiform_execute(src, NULL, pcb, NULL, NULL, &err);
    ASSERT(rc == 0);

    /* 4 edge cuts tracks for outline + 0 copper tracks */
    ASSERT(dc_epcb_track_count(pcb) == 4);
    ASSERT(dc_epcb_footprint_count(pcb) == 1);

    DC_PcbDesignRules *rules = dc_epcb_get_design_rules(pcb);
    ASSERT(rules->clearance == 0.15);
    ASSERT(rules->track_width == 0.2);

    dc_epcb_free(pcb);
}

/* =========================================================================
 * Tests — Cubeiform export (roundtrip)
 * ========================================================================= */

TEST(test_export_schematic)
{
    DC_Error err = {0};

    DC_ESchematic *sch = dc_eschematic_new();
    ASSERT(sch != NULL);

    dc_eschematic_add_symbol(sch, "Device:R_Small", "R1", 100, 50);
    dc_eschematic_add_power_port(sch, "VCC", 50, 30);

    char *dcad = dc_eschematic_to_cubeiform(sch, &err);
    ASSERT(dcad != NULL);
    ASSERT(strstr(dcad, "schematic") != NULL);
    ASSERT(strstr(dcad, "R1") != NULL);
    ASSERT(strstr(dcad, "Device:R_Small") != NULL);
    ASSERT(strstr(dcad, "power VCC") != NULL);

    free(dcad);
    dc_eschematic_free(sch);
}

TEST(test_export_pcb)
{
    DC_Error err = {0};

    DC_EPcb *pcb = dc_epcb_new();
    ASSERT(pcb != NULL);

    dc_epcb_add_footprint(pcb, "Device:R_Small", "R1", 10, 15, DC_PCB_LAYER_F_CU);

    DC_PcbDesignRules *rules = dc_epcb_get_design_rules(pcb);
    rules->clearance = 0.15;
    rules->track_width = 0.2;

    char *dcad = dc_epcb_to_cubeiform(pcb, &err);
    ASSERT(dcad != NULL);
    ASSERT(strstr(dcad, "pcb") != NULL);
    ASSERT(strstr(dcad, "place R1") != NULL);
    ASSERT(strstr(dcad, "clearance") != NULL);

    free(dcad);
    dc_epcb_free(pcb);
}

/* =========================================================================
 * Tests — Mixed 3D + EDA (EDA blocks coexist with shape blocks)
 * ========================================================================= */

TEST(test_mixed_3d_eda)
{
    DC_Error err = {0};
    const char *src =
        "// 3D shape (should be ignored by EDA parser)\n"
        "shape enclosure() {\n"
        "    cube(10);\n"
        "}\n"
        "\n"
        "schematic {\n"
        "    component R1 = \"Device:R_Small\" at 100, 50;\n"
        "}\n"
        "\n"
        "pcb {\n"
        "    place R1 at 10, 10 on F.Cu;\n"
        "}\n";

    DC_CubeiformEda *eda = dc_cubeiform_parse_eda(src, &err);
    ASSERT(eda != NULL);
    ASSERT(dc_cubeiform_eda_sch_op_count(eda) == 1);
    ASSERT(dc_cubeiform_eda_pcb_op_count(eda) == 1);

    dc_cubeiform_eda_free(eda);
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    printf("test_cubeiform_eda\n");

    /* Parsing */
    RUN(test_parse_empty);
    RUN(test_parse_null);
    RUN(test_parse_schematic_component);
    RUN(test_parse_schematic_wire);
    RUN(test_parse_schematic_power);
    RUN(test_parse_pipe_value_footprint);
    RUN(test_parse_pcb_outline);
    RUN(test_parse_pcb_rules);
    RUN(test_parse_pcb_place);
    RUN(test_parse_pcb_route);
    RUN(test_parse_pcb_zone);
    RUN(test_parse_full_file);

    /* Apply + Execute */
    RUN(test_apply_schematic);
    RUN(test_apply_pcb);

    /* Export */
    RUN(test_export_schematic);
    RUN(test_export_pcb);

    /* Mixed */
    RUN(test_mixed_3d_eda);

    printf("\n  %d passed, %d failed\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
