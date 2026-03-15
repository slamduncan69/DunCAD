#ifndef DC_CUBEIFORM_H
#define DC_CUBEIFORM_H

/*
 * cubeiform.h — Cubeiform-to-OpenSCAD transpiler.
 *
 * Cubeiform is DunCAD's native scripting language. It produces the same
 * geometry as OpenSCAD but with cleaner syntax:
 *   - Pipe transforms:  cube(5) >> move(x=10) >> rotate(z=45);
 *   - CSG operators:    body - hole   (difference)
 *                        a + b         (union)
 *                        a & b         (intersection)
 *   - shape/fn keywords instead of module/function
 *   - for x in range {} instead of for (x = range) {}
 *   - Named axes: move(x=10) instead of translate([10,0,0])
 *
 * This module transpiles Cubeiform source (.dcad) into OpenSCAD source
 * that Trinity Site can execute.
 */

#include "core/error.h"

/* Transpile Cubeiform source to OpenSCAD.
 * Returns a malloc'd string of OpenSCAD source, or NULL on error.
 * Caller must free() the returned string. */
char *dc_cubeiform_to_scad(const char *dcad_src, DC_Error *err);

#endif /* DC_CUBEIFORM_H */
