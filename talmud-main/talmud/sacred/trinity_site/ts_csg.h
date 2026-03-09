/*
 * ts_csg.h — Constructive Solid Geometry operations (STUBS)
 *
 * CSG is the hardest part to parallelize. OpenSCAD uses CGAL's Nef
 * polyhedra, which is inherently sequential and slow.
 *
 * Our strategy for parallelization:
 *   1. BSP-tree based CSG (each node split is independent)
 *   2. Voxel-based CSG on GPU (SDF evaluation is embarrassingly parallel)
 *   3. Hybrid: coarse voxel pass on GPU, refined BSP on CPU
 *
 * Status: STUB — returns TS_CSG_NOT_IMPLEMENTED
 * These interfaces are locked. Implementation comes after the
 * mathematical foundation is proven correct.
 *
 * OpenSCAD equivalents:
 *   union()         — ts_csg_union
 *   difference()    — ts_csg_difference
 *   intersection()  — ts_csg_intersection
 *   hull()          — ts_csg_hull
 *   minkowski()     — ts_csg_minkowski
 */
#ifndef TS_CSG_H
#define TS_CSG_H

#define TS_CSG_OK               0
#define TS_CSG_NOT_IMPLEMENTED -99
#define TS_CSG_ERROR           -1

/* --- CSG operation type enum --- */
typedef enum {
    TS_CSG_OP_UNION,
    TS_CSG_OP_DIFFERENCE,
    TS_CSG_OP_INTERSECTION,
} ts_csg_op_t;

/*
 * Perform a CSG boolean operation on two meshes.
 * Result is written to out (which must be initialized).
 *
 * GPU parallelization plan:
 *   Phase 1: Classify all triangles of A against BSP of B (parallel per-tri)
 *   Phase 2: Classify all triangles of B against BSP of A (parallel per-tri)
 *   Phase 3: Split intersecting triangles along BSP planes (parallel per-edge)
 *   Phase 4: Select triangles based on operation (parallel per-tri)
 *   Phase 5: Merge and deduplicate vertices (parallel sort + reduce)
 *
 * Estimated GPU speedup: 10-50x for complex meshes (>10k triangles).
 * For simple meshes (<1k triangles), CPU is faster due to transfer overhead.
 */
static inline int ts_csg_boolean(const ts_mesh *a, const ts_mesh *b,
                                 ts_csg_op_t op, ts_mesh *out) {
    (void)a; (void)b; (void)op; (void)out;
    return TS_CSG_NOT_IMPLEMENTED;
}

/* Convenience wrappers */
static inline int ts_csg_union(const ts_mesh *a, const ts_mesh *b, ts_mesh *out) {
    return ts_csg_boolean(a, b, TS_CSG_OP_UNION, out);
}

static inline int ts_csg_difference(const ts_mesh *a, const ts_mesh *b, ts_mesh *out) {
    return ts_csg_boolean(a, b, TS_CSG_OP_DIFFERENCE, out);
}

static inline int ts_csg_intersection(const ts_mesh *a, const ts_mesh *b, ts_mesh *out) {
    return ts_csg_boolean(a, b, TS_CSG_OP_INTERSECTION, out);
}

/*
 * Convex hull of a mesh.
 *
 * GPU parallelization plan:
 *   - Quickhull algorithm with parallel convexity tests
 *   - Each face visibility check is independent
 *   - Horizon edge detection is reducible
 *
 * Estimated GPU speedup: 5-20x for large point sets.
 */
static inline int ts_csg_hull(const ts_mesh *input, ts_mesh *out) {
    (void)input; (void)out;
    return TS_CSG_NOT_IMPLEMENTED;
}

/*
 * Minkowski sum of two meshes.
 *
 * GPU parallelization plan:
 *   - For each vertex of A, translate B (embarrassingly parallel)
 *   - Compute convex hull of result (see hull above)
 *   - For non-convex inputs: decompose into convex parts first
 *
 * This is the most expensive CSG operation. GPU is essential.
 * Estimated GPU speedup: 50-200x for complex inputs.
 */
static inline int ts_csg_minkowski(const ts_mesh *a, const ts_mesh *b, ts_mesh *out) {
    (void)a; (void)b; (void)out;
    return TS_CSG_NOT_IMPLEMENTED;
}

#endif /* TS_CSG_H */
