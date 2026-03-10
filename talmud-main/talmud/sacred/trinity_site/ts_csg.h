/*
 * ts_csg.h — Constructive Solid Geometry operations
 *
 * BSP-tree based CSG for union, difference, intersection.
 * Quickhull for convex hull. Minkowski sum for convex meshes.
 *
 * Algorithm: Based on the classic Laidlaw/Trumbore/Hughes BSP-tree CSG
 * (same approach as csg.js). Each mesh is converted to a list of polygons,
 * a BSP tree is built, then clip/invert operations produce the result.
 *
 * GPU parallelization plan:
 *   Phase 1: Classify all triangles of A against BSP of B (parallel per-tri)
 *   Phase 2: Classify all triangles of B against BSP of A (parallel per-tri)
 *   Phase 3: Split intersecting triangles along BSP planes (parallel per-edge)
 *   Phase 4: Select triangles based on operation (parallel per-tri)
 *   Phase 5: Merge and deduplicate vertices (parallel sort + reduce)
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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#define TS_CSG_OK               0
#define TS_CSG_ERROR           -1

/* Adaptive epsilon for plane classification.
 * Default 1e-5. ts_csg_boolean sets this relative to mesh extents
 * at the start of each operation: eps = max_extent * 1e-8, clamped
 * to [1e-10, 1e-4]. Prevents misclassification for both tiny and huge meshes. */
static double ts_csg_eps_ = 1e-5;
#define TS_CSG_EPS ts_csg_eps_

/* --- CSG operation type enum --- */
typedef enum {
    TS_CSG_OP_UNION,
    TS_CSG_OP_DIFFERENCE,
    TS_CSG_OP_INTERSECTION,
} ts_csg_op_t;

/* ================================================================
 * CSG POLYGON — variable-vertex polygon with plane
 * ================================================================ */

typedef struct {
    double x, y, z;
    double nx, ny, nz;
} ts_csg_vertex;

/* A convex polygon lying in a plane. */
typedef struct {
    ts_csg_vertex *verts;
    int count;
    int cap;
    double plane[4]; /* nx, ny, nz, w (dot with normal) */
} ts_csg_poly;

/* Dynamic array of polygons */
typedef struct {
    ts_csg_poly *items;
    int count;
    int cap;
} ts_csg_polylist;

/* --- Polygon lifecycle --- */

static inline ts_csg_poly ts_csg_poly_init(void) {
    return (ts_csg_poly){ NULL, 0, 0, {0,0,0,0} };
}

static inline void ts_csg_poly_free(ts_csg_poly *p) {
    free(p->verts);
    p->verts = NULL;
    p->count = p->cap = 0;
}

static inline int ts_csg_poly_add_vert(ts_csg_poly *p, ts_csg_vertex v) {
    if (p->count >= p->cap) {
        int nc = p->cap ? p->cap * 2 : 4;
        ts_csg_vertex *nv = (ts_csg_vertex *)realloc(p->verts,
            (size_t)nc * sizeof(ts_csg_vertex));
        if (!nv) return -1;
        p->verts = nv;
        p->cap = nc;
    }
    p->verts[p->count++] = v;
    return 0;
}

static inline ts_csg_poly ts_csg_poly_clone(const ts_csg_poly *src) {
    ts_csg_poly p = ts_csg_poly_init();
    if (src->count > 0) {
        p.verts = (ts_csg_vertex *)malloc((size_t)src->count * sizeof(ts_csg_vertex));
        if (p.verts) {
            memcpy(p.verts, src->verts, (size_t)src->count * sizeof(ts_csg_vertex));
            p.count = p.cap = src->count;
        }
    }
    memcpy(p.plane, src->plane, sizeof(p.plane));
    return p;
}

/* Compute plane from first 3 vertices */
static inline void ts_csg_poly_calc_plane(ts_csg_poly *p) {
    if (p->count < 3) return;
    double ax = p->verts[1].x - p->verts[0].x;
    double ay = p->verts[1].y - p->verts[0].y;
    double az = p->verts[1].z - p->verts[0].z;
    double bx = p->verts[2].x - p->verts[0].x;
    double by = p->verts[2].y - p->verts[0].y;
    double bz = p->verts[2].z - p->verts[0].z;
    double nx = ay*bz - az*by;
    double ny = az*bx - ax*bz;
    double nz = ax*by - ay*bx;
    double len = sqrt(nx*nx + ny*ny + nz*nz);
    if (len < 1e-15) { p->plane[0]=p->plane[1]=p->plane[2]=p->plane[3]=0; return; }
    nx /= len; ny /= len; nz /= len;
    p->plane[0] = nx;
    p->plane[1] = ny;
    p->plane[2] = nz;
    p->plane[3] = nx*p->verts[0].x + ny*p->verts[0].y + nz*p->verts[0].z;
}

/* Flip polygon orientation */
static inline void ts_csg_poly_flip(ts_csg_poly *p) {
    /* Reverse vertex order */
    for (int i = 0; i < p->count / 2; i++) {
        ts_csg_vertex tmp = p->verts[i];
        p->verts[i] = p->verts[p->count - 1 - i];
        p->verts[p->count - 1 - i] = tmp;
    }
    /* Flip normals */
    for (int i = 0; i < p->count; i++) {
        p->verts[i].nx = -p->verts[i].nx;
        p->verts[i].ny = -p->verts[i].ny;
        p->verts[i].nz = -p->verts[i].nz;
    }
    p->plane[0] = -p->plane[0];
    p->plane[1] = -p->plane[1];
    p->plane[2] = -p->plane[2];
    p->plane[3] = -p->plane[3];
}

/* --- Polylist lifecycle --- */

static inline ts_csg_polylist ts_csg_polylist_init(void) {
    return (ts_csg_polylist){ NULL, 0, 0 };
}

static inline void ts_csg_polylist_free(ts_csg_polylist *pl) {
    for (int i = 0; i < pl->count; i++)
        ts_csg_poly_free(&pl->items[i]);
    free(pl->items);
    pl->items = NULL;
    pl->count = pl->cap = 0;
}

static inline int ts_csg_polylist_push(ts_csg_polylist *pl, ts_csg_poly p) {
    if (pl->count >= pl->cap) {
        int nc = pl->cap ? pl->cap * 2 : 16;
        ts_csg_poly *ni = (ts_csg_poly *)realloc(pl->items,
            (size_t)nc * sizeof(ts_csg_poly));
        if (!ni) return -1;
        pl->items = ni;
        pl->cap = nc;
    }
    pl->items[pl->count++] = p;
    return 0;
}

/* Append all from src into dst (moves ownership, zeroes src) */
static inline void ts_csg_polylist_steal(ts_csg_polylist *dst,
                                          ts_csg_polylist *src) {
    for (int i = 0; i < src->count; i++)
        ts_csg_polylist_push(dst, src->items[i]);
    /* Don't free individual polys — ownership transferred */
    free(src->items);
    src->items = NULL;
    src->count = src->cap = 0;
}

/* ================================================================
 * VERTEX INTERPOLATION
 * ================================================================ */

static inline ts_csg_vertex ts_csg_vertex_lerp(ts_csg_vertex a,
                                                 ts_csg_vertex b, double t) {
    ts_csg_vertex r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;
    r.nx = a.nx + (b.nx - a.nx) * t;
    r.ny = a.ny + (b.ny - a.ny) * t;
    r.nz = a.nz + (b.nz - a.nz) * t;
    /* Renormalize interpolated normal */
    double len = sqrt(r.nx*r.nx + r.ny*r.ny + r.nz*r.nz);
    if (len > 1e-15) { r.nx /= len; r.ny /= len; r.nz /= len; }
    return r;
}

/* ================================================================
 * POLYGON SPLITTING AGAINST A PLANE
 *
 * Each vertex classified as COPLANAR, FRONT, or BACK.
 * GPU: per-vertex classification is embarrassingly parallel.
 * Splitting requires sequential walk around polygon edges.
 * ================================================================ */

#define TS_CSG_COPLANAR 0
#define TS_CSG_FRONT    1
#define TS_CSG_BACK     2
#define TS_CSG_SPANNING 3

static inline void ts_csg_split_polygon(const double plane[4],
                                         const ts_csg_poly *poly,
                                         ts_csg_polylist *coplanar_front,
                                         ts_csg_polylist *coplanar_back,
                                         ts_csg_polylist *front,
                                         ts_csg_polylist *back) {
    int polygon_type = 0;

    /* Stack buffer for vertex types (avoid malloc for small polys) */
    int types_buf[64];
    int *types = (poly->count <= 64) ? types_buf :
        (int *)malloc((size_t)poly->count * sizeof(int));
    if (!types) return;

    /* Classify each vertex */
    for (int i = 0; i < poly->count; i++) {
        double d = plane[0]*poly->verts[i].x +
                   plane[1]*poly->verts[i].y +
                   plane[2]*poly->verts[i].z - plane[3];
        int type = (d < -TS_CSG_EPS) ? TS_CSG_BACK :
                   (d > TS_CSG_EPS)  ? TS_CSG_FRONT : TS_CSG_COPLANAR;
        polygon_type |= type;
        types[i] = type;
    }

    switch (polygon_type) {
    case TS_CSG_COPLANAR: {
        /* Dot polygon normal with plane normal to determine side */
        double dot = plane[0]*poly->plane[0] +
                     plane[1]*poly->plane[1] +
                     plane[2]*poly->plane[2];
        ts_csg_polylist *target = (dot > 0) ? coplanar_front : coplanar_back;
        ts_csg_polylist_push(target, ts_csg_poly_clone(poly));
        break;
    }
    case TS_CSG_FRONT:
        ts_csg_polylist_push(front, ts_csg_poly_clone(poly));
        break;
    case TS_CSG_BACK:
        ts_csg_polylist_push(back, ts_csg_poly_clone(poly));
        break;
    case TS_CSG_SPANNING: {
        ts_csg_poly f = ts_csg_poly_init();
        ts_csg_poly b = ts_csg_poly_init();
        memcpy(f.plane, poly->plane, sizeof(f.plane));
        memcpy(b.plane, poly->plane, sizeof(b.plane));

        for (int i = 0; i < poly->count; i++) {
            int j = (i + 1) % poly->count;
            int ti = types[i], tj = types[j];
            ts_csg_vertex vi = poly->verts[i];
            ts_csg_vertex vj = poly->verts[j];

            if (ti != TS_CSG_BACK)
                ts_csg_poly_add_vert(&f, vi);
            if (ti != TS_CSG_FRONT)
                ts_csg_poly_add_vert(&b, vi);

            if ((ti | tj) == TS_CSG_SPANNING) {
                /* Compute interpolation parameter */
                double di = plane[0]*vi.x + plane[1]*vi.y +
                            plane[2]*vi.z - plane[3];
                double dj = plane[0]*vj.x + plane[1]*vj.y +
                            plane[2]*vj.z - plane[3];
                double t = di / (di - dj);
                ts_csg_vertex mid = ts_csg_vertex_lerp(vi, vj, t);
                ts_csg_poly_add_vert(&f, mid);
                ts_csg_poly_add_vert(&b, mid);
            }
        }
        if (f.count >= 3)
            ts_csg_polylist_push(front, f);
        else
            ts_csg_poly_free(&f);
        if (b.count >= 3)
            ts_csg_polylist_push(back, b);
        else
            ts_csg_poly_free(&b);
        break;
    }
    }

    if (types != types_buf) free(types);
}

/*
 * Move-semantic variant: transfers ownership of non-spanning polygons
 * instead of cloning them. Moved polygons have their verts pointer
 * NULLed so the caller can safely free(NULL). Only SPANNING polygons
 * allocate new memory (creating two new polys from one).
 *
 * This eliminates O(N) malloc+memcpy per BSP level for the common case
 * where most polygons are purely FRONT or BACK.
 */
static inline void ts_csg_split_polygon_move(const double plane[4],
                                              ts_csg_poly *poly,
                                              ts_csg_polylist *coplanar_front,
                                              ts_csg_polylist *coplanar_back,
                                              ts_csg_polylist *front,
                                              ts_csg_polylist *back) {
    int polygon_type = 0;
    int types_buf[64];
    int *types = (poly->count <= 64) ? types_buf :
        (int *)malloc((size_t)poly->count * sizeof(int));
    if (!types) return;

    for (int i = 0; i < poly->count; i++) {
        double d = plane[0]*poly->verts[i].x +
                   plane[1]*poly->verts[i].y +
                   plane[2]*poly->verts[i].z - plane[3];
        int type = (d < -TS_CSG_EPS) ? TS_CSG_BACK :
                   (d > TS_CSG_EPS)  ? TS_CSG_FRONT : TS_CSG_COPLANAR;
        polygon_type |= type;
        types[i] = type;
    }

    switch (polygon_type) {
    case TS_CSG_COPLANAR: {
        double dot = plane[0]*poly->plane[0] +
                     plane[1]*poly->plane[1] +
                     plane[2]*poly->plane[2];
        ts_csg_polylist *target = (dot > 0) ? coplanar_front : coplanar_back;
        ts_csg_polylist_push(target, *poly);  /* move */
        poly->verts = NULL; poly->count = poly->cap = 0;
        break;
    }
    case TS_CSG_FRONT:
        ts_csg_polylist_push(front, *poly);   /* move */
        poly->verts = NULL; poly->count = poly->cap = 0;
        break;
    case TS_CSG_BACK:
        ts_csg_polylist_push(back, *poly);    /* move */
        poly->verts = NULL; poly->count = poly->cap = 0;
        break;
    case TS_CSG_SPANNING: {
        ts_csg_poly f = ts_csg_poly_init();
        ts_csg_poly b = ts_csg_poly_init();
        memcpy(f.plane, poly->plane, sizeof(f.plane));
        memcpy(b.plane, poly->plane, sizeof(b.plane));

        for (int i = 0; i < poly->count; i++) {
            int j = (i + 1) % poly->count;
            int ti = types[i], tj = types[j];
            ts_csg_vertex vi = poly->verts[i];
            ts_csg_vertex vj = poly->verts[j];

            if (ti != TS_CSG_BACK)
                ts_csg_poly_add_vert(&f, vi);
            if (ti != TS_CSG_FRONT)
                ts_csg_poly_add_vert(&b, vi);

            if ((ti | tj) == TS_CSG_SPANNING) {
                double di = plane[0]*vi.x + plane[1]*vi.y +
                            plane[2]*vi.z - plane[3];
                double dj = plane[0]*vj.x + plane[1]*vj.y +
                            plane[2]*vj.z - plane[3];
                double t = di / (di - dj);
                ts_csg_vertex mid = ts_csg_vertex_lerp(vi, vj, t);
                ts_csg_poly_add_vert(&f, mid);
                ts_csg_poly_add_vert(&b, mid);
            }
        }
        if (f.count >= 3)
            ts_csg_polylist_push(front, f);
        else
            ts_csg_poly_free(&f);
        if (b.count >= 3)
            ts_csg_polylist_push(back, b);
        else
            ts_csg_poly_free(&b);
        break;
    }
    }

    if (types != types_buf) free(types);
}

/* ================================================================
 * BSP NODE — recursive binary space partition tree
 *
 * Build: pick splitting plane from first polygon, partition rest.
 * GPU plan: tree traversal is sequential, but classification of
 * polygons against planes is parallel per-polygon.
 * ================================================================ */

typedef struct ts_csg_bsp ts_csg_bsp;
struct ts_csg_bsp {
    double plane[4];
    ts_csg_polylist polys;   /* coplanar polygons at this node */
    ts_csg_bsp *front;
    ts_csg_bsp *back;
};

static inline ts_csg_bsp *ts_csg_bsp_new(void) {
    ts_csg_bsp *node = (ts_csg_bsp *)calloc(1, sizeof(ts_csg_bsp));
    if (node) node->polys = ts_csg_polylist_init();
    return node;
}

static inline void ts_csg_bsp_free(ts_csg_bsp *node) {
    if (!node) return;
    ts_csg_polylist_free(&node->polys);
    ts_csg_bsp_free(node->front);
    ts_csg_bsp_free(node->back);
    free(node);
}

/* Forward declarations */
static void ts_csg_bsp_build(ts_csg_bsp *node, ts_csg_polylist *polys);
static void ts_csg_bsp_clip_polys(const ts_csg_bsp *bsp, ts_csg_polylist *polys);
static void ts_csg_bsp_clip_to(ts_csg_bsp *node, const ts_csg_bsp *other);
static void ts_csg_bsp_invert(ts_csg_bsp *node);
static void ts_csg_bsp_all_polys(const ts_csg_bsp *node, ts_csg_polylist *out);

/* Build BSP tree from a list of polygons. Consumes the polylist.
 *
 * Two-pass algorithm:
 *   Pass 1: Classify all polygons against splitting plane (cache-friendly)
 *   Pass 2: Partition into front/back/coplanar using pre-computed classes
 *
 * Optimizations:
 *   - Move semantics: non-spanning polys transferred without cloning
 *   - Pre-allocated partition lists: exact sizes known from pass 1
 *   - Batch-friendly: pass 1 can be GPU-accelerated
 */
static void ts_csg_bsp_build(ts_csg_bsp *node, ts_csg_polylist *polys) {
    if (!polys || polys->count == 0) return;

    /* Use first polygon's plane as the splitting plane */
    if (node->polys.count == 0) {
        memcpy(node->plane, polys->items[0].plane, sizeof(node->plane));
    }

    /* Pass 1: Classify all polygons */
    int *classes = (int *)malloc((size_t)polys->count * sizeof(int));
    if (!classes) return;
    int n_front = 0, n_back = 0, n_spanning = 0;

    for (int i = 0; i < polys->count; i++) {
        int type = 0;
        const ts_csg_poly *p = &polys->items[i];
        for (int k = 0; k < p->count; k++) {
            double d = node->plane[0]*p->verts[k].x +
                       node->plane[1]*p->verts[k].y +
                       node->plane[2]*p->verts[k].z - node->plane[3];
            int vtype = (d < -TS_CSG_EPS) ? TS_CSG_BACK :
                        (d > TS_CSG_EPS)  ? TS_CSG_FRONT : TS_CSG_COPLANAR;
            type |= vtype;
        }
        classes[i] = type;
        if      (type == TS_CSG_FRONT) n_front++;
        else if (type == TS_CSG_BACK)  n_back++;
        else if (type == TS_CSG_SPANNING) { n_front++; n_back++; n_spanning++; }
        /* COPLANAR goes to node->polys, no pre-count needed */
    }

    /* Pass 2: Pre-allocate partition lists (no reallocs during partition) */
    ts_csg_polylist front_list = ts_csg_polylist_init();
    ts_csg_polylist back_list = ts_csg_polylist_init();
    if (n_front > 0) {
        front_list.items = (ts_csg_poly *)malloc((size_t)n_front * sizeof(ts_csg_poly));
        front_list.cap = n_front;
    }
    if (n_back > 0) {
        back_list.items = (ts_csg_poly *)malloc((size_t)n_back * sizeof(ts_csg_poly));
        back_list.cap = n_back;
    }

    /* Pass 3: Partition using pre-computed classifications */
    for (int i = 0; i < polys->count; i++) {
        ts_csg_poly *p = &polys->items[i];
        switch (classes[i]) {
        case TS_CSG_COPLANAR: {
            double dot = node->plane[0]*p->plane[0] +
                         node->plane[1]*p->plane[1] +
                         node->plane[2]*p->plane[2];
            (void)dot; /* coplanar polys go to node->polys regardless of facing */
            ts_csg_polylist_push(&node->polys, *p);  /* move */
            p->verts = NULL; p->count = p->cap = 0;
            break;
        }
        case TS_CSG_FRONT:
            front_list.items[front_list.count++] = *p;  /* move (no realloc) */
            p->verts = NULL; p->count = p->cap = 0;
            break;
        case TS_CSG_BACK:
            back_list.items[back_list.count++] = *p;    /* move (no realloc) */
            p->verts = NULL; p->count = p->cap = 0;
            break;
        case TS_CSG_SPANNING: {
            /* Split spanning polygon — creates new allocations */
            ts_csg_poly f = ts_csg_poly_init();
            ts_csg_poly b = ts_csg_poly_init();
            memcpy(f.plane, p->plane, sizeof(f.plane));
            memcpy(b.plane, p->plane, sizeof(b.plane));

            /* Need per-vertex types for the split */
            int types_buf[64];
            int *types = (p->count <= 64) ? types_buf :
                (int *)malloc((size_t)p->count * sizeof(int));
            if (types) {
                for (int k = 0; k < p->count; k++) {
                    double d = node->plane[0]*p->verts[k].x +
                               node->plane[1]*p->verts[k].y +
                               node->plane[2]*p->verts[k].z - node->plane[3];
                    types[k] = (d < -TS_CSG_EPS) ? TS_CSG_BACK :
                               (d > TS_CSG_EPS)  ? TS_CSG_FRONT : TS_CSG_COPLANAR;
                }
                for (int k = 0; k < p->count; k++) {
                    int j = (k + 1) % p->count;
                    int tk = types[k], tj = types[j];
                    ts_csg_vertex vk = p->verts[k];
                    ts_csg_vertex vj = p->verts[j];

                    if (tk != TS_CSG_BACK)  ts_csg_poly_add_vert(&f, vk);
                    if (tk != TS_CSG_FRONT) ts_csg_poly_add_vert(&b, vk);

                    if ((tk | tj) == TS_CSG_SPANNING) {
                        double dk = node->plane[0]*vk.x + node->plane[1]*vk.y +
                                    node->plane[2]*vk.z - node->plane[3];
                        double dj = node->plane[0]*vj.x + node->plane[1]*vj.y +
                                    node->plane[2]*vj.z - node->plane[3];
                        double t = dk / (dk - dj);
                        ts_csg_vertex mid = ts_csg_vertex_lerp(vk, vj, t);
                        ts_csg_poly_add_vert(&f, mid);
                        ts_csg_poly_add_vert(&b, mid);
                    }
                }
                if (types != types_buf) free(types);
            }

            if (f.count >= 3)
                front_list.items[front_list.count++] = f;
            else
                ts_csg_poly_free(&f);
            if (b.count >= 3)
                back_list.items[back_list.count++] = b;
            else
                ts_csg_poly_free(&b);
            break;
        }
        }
    }
    free(classes);

    if (front_list.count > 0) {
        if (!node->front) node->front = ts_csg_bsp_new();
        ts_csg_bsp_build(node->front, &front_list);
    }
    if (back_list.count > 0) {
        if (!node->back) node->back = ts_csg_bsp_new();
        ts_csg_bsp_build(node->back, &back_list);
    }

    /* Free remaining polys (moved polys have verts=NULL, free(NULL) is safe) */
    for (int i = 0; i < polys->count; i++)
        ts_csg_poly_free(&polys->items[i]);
    free(polys->items);
    polys->items = NULL;
    polys->count = polys->cap = 0;

    /* Free the partition lists' arrays (polys moved into children) */
    free(front_list.items);
    free(back_list.items);
}

/*
 * Clip a list of polygons against this BSP tree.
 * Removes polygons on the back side of every splitting plane.
 * Result replaces the input polylist.
 */
static void ts_csg_bsp_clip_polys(const ts_csg_bsp *bsp,
                                    ts_csg_polylist *polys) {
    if (!bsp) return;

    /* Two-pass: classify then partition (avoids reallocs) */
    int *classes = (int *)malloc((size_t)polys->count * sizeof(int));
    if (!classes) return;
    int n_front = 0, n_back = 0, n_spanning = 0;

    for (int i = 0; i < polys->count; i++) {
        int type = 0;
        const ts_csg_poly *p = &polys->items[i];
        for (int k = 0; k < p->count; k++) {
            double d = bsp->plane[0]*p->verts[k].x +
                       bsp->plane[1]*p->verts[k].y +
                       bsp->plane[2]*p->verts[k].z - bsp->plane[3];
            int vtype = (d < -TS_CSG_EPS) ? TS_CSG_BACK :
                        (d > TS_CSG_EPS)  ? TS_CSG_FRONT : TS_CSG_COPLANAR;
            type |= vtype;
        }
        classes[i] = type;
        if      (type == TS_CSG_FRONT || type == TS_CSG_COPLANAR) n_front++;
        else if (type == TS_CSG_BACK)  n_back++;
        else { n_front++; n_back++; n_spanning++; }
    }

    ts_csg_polylist front_list = ts_csg_polylist_init();
    ts_csg_polylist back_list = ts_csg_polylist_init();
    if (n_front > 0) {
        front_list.items = (ts_csg_poly *)malloc((size_t)n_front * sizeof(ts_csg_poly));
        front_list.cap = n_front;
    }
    if (n_back > 0) {
        back_list.items = (ts_csg_poly *)malloc((size_t)n_back * sizeof(ts_csg_poly));
        back_list.cap = n_back;
    }

    for (int i = 0; i < polys->count; i++) {
        ts_csg_poly *p = &polys->items[i];
        switch (classes[i]) {
        case TS_CSG_COPLANAR:
        case TS_CSG_FRONT:
            front_list.items[front_list.count++] = *p;
            p->verts = NULL; p->count = p->cap = 0;
            break;
        case TS_CSG_BACK:
            back_list.items[back_list.count++] = *p;
            p->verts = NULL; p->count = p->cap = 0;
            break;
        case TS_CSG_SPANNING: {
            ts_csg_poly f = ts_csg_poly_init();
            ts_csg_poly b = ts_csg_poly_init();
            memcpy(f.plane, p->plane, sizeof(f.plane));
            memcpy(b.plane, p->plane, sizeof(b.plane));
            int types_buf[64];
            int *types = (p->count <= 64) ? types_buf :
                (int *)malloc((size_t)p->count * sizeof(int));
            if (types) {
                for (int k = 0; k < p->count; k++) {
                    double d = bsp->plane[0]*p->verts[k].x +
                               bsp->plane[1]*p->verts[k].y +
                               bsp->plane[2]*p->verts[k].z - bsp->plane[3];
                    types[k] = (d < -TS_CSG_EPS) ? TS_CSG_BACK :
                               (d > TS_CSG_EPS)  ? TS_CSG_FRONT : TS_CSG_COPLANAR;
                }
                for (int k = 0; k < p->count; k++) {
                    int j = (k + 1) % p->count;
                    int tk = types[k], tj = types[j];
                    ts_csg_vertex vk = p->verts[k];
                    ts_csg_vertex vj = p->verts[j];
                    if (tk != TS_CSG_BACK)  ts_csg_poly_add_vert(&f, vk);
                    if (tk != TS_CSG_FRONT) ts_csg_poly_add_vert(&b, vk);
                    if ((tk | tj) == TS_CSG_SPANNING) {
                        double dk = bsp->plane[0]*vk.x + bsp->plane[1]*vk.y +
                                    bsp->plane[2]*vk.z - bsp->plane[3];
                        double dj = bsp->plane[0]*vj.x + bsp->plane[1]*vj.y +
                                    bsp->plane[2]*vj.z - bsp->plane[3];
                        double t = dk / (dk - dj);
                        ts_csg_vertex mid = ts_csg_vertex_lerp(vk, vj, t);
                        ts_csg_poly_add_vert(&f, mid);
                        ts_csg_poly_add_vert(&b, mid);
                    }
                }
                if (types != types_buf) free(types);
            }
            if (f.count >= 3) front_list.items[front_list.count++] = f;
            else ts_csg_poly_free(&f);
            if (b.count >= 3) back_list.items[back_list.count++] = b;
            else ts_csg_poly_free(&b);
            break;
        }
        }
    }
    free(classes);

    /* Free remaining (moved polys have verts=NULL, free(NULL) is safe) */
    for (int i = 0; i < polys->count; i++)
        ts_csg_poly_free(&polys->items[i]);
    free(polys->items);
    polys->items = NULL;
    polys->count = polys->cap = 0;

    /* Recursively clip front and back */
    if (bsp->front)
        ts_csg_bsp_clip_polys(bsp->front, &front_list);
    if (bsp->back)
        ts_csg_bsp_clip_polys(bsp->back, &back_list);
    else {
        /* No back node = everything behind this plane is removed */
        for (int i = 0; i < back_list.count; i++)
            ts_csg_poly_free(&back_list.items[i]);
        back_list.count = 0;
    }

    /* Merge results back into polys */
    *polys = front_list;
    ts_csg_polylist_steal(polys, &back_list);
}

/* Clip all polygons in this BSP tree against another BSP tree */
static void ts_csg_bsp_clip_to(ts_csg_bsp *node, const ts_csg_bsp *other) {
    if (!node) return;
    ts_csg_bsp_clip_polys(other, &node->polys);
    ts_csg_bsp_clip_to(node->front, other);
    ts_csg_bsp_clip_to(node->back, other);
}

/* Invert the BSP tree (swap inside/outside) */
static void ts_csg_bsp_invert(ts_csg_bsp *node) {
    if (!node) return;
    for (int i = 0; i < node->polys.count; i++)
        ts_csg_poly_flip(&node->polys.items[i]);
    node->plane[0] = -node->plane[0];
    node->plane[1] = -node->plane[1];
    node->plane[2] = -node->plane[2];
    node->plane[3] = -node->plane[3];
    ts_csg_bsp_invert(node->front);
    ts_csg_bsp_invert(node->back);
    /* Swap front and back subtrees */
    ts_csg_bsp *tmp = node->front;
    node->front = node->back;
    node->back = tmp;
}

/* Collect all polygons from the BSP tree */
static void ts_csg_bsp_all_polys(const ts_csg_bsp *node,
                                   ts_csg_polylist *out) {
    if (!node) return;
    for (int i = 0; i < node->polys.count; i++)
        ts_csg_polylist_push(out, ts_csg_poly_clone(&node->polys.items[i]));
    ts_csg_bsp_all_polys(node->front, out);
    ts_csg_bsp_all_polys(node->back, out);
}

/* Add polygons to an existing BSP tree */
static void ts_csg_bsp_add_polys(ts_csg_bsp *node, ts_csg_polylist *polys) {
    ts_csg_bsp_build(node, polys);
}

/* ================================================================
 * MESH <-> POLYGON CONVERSION
 * ================================================================ */

/* Convert ts_mesh triangles into CSG polygons */
static inline ts_csg_polylist ts_csg_mesh_to_polys(const ts_mesh *m) {
    ts_csg_polylist pl = ts_csg_polylist_init();
    for (int i = 0; i < m->tri_count; i++) {
        ts_csg_poly p = ts_csg_poly_init();
        for (int j = 0; j < 3; j++) {
            int vi = m->tris[i].idx[j];
            ts_csg_vertex v;
            v.x = m->verts[vi].pos[0];
            v.y = m->verts[vi].pos[1];
            v.z = m->verts[vi].pos[2];
            v.nx = m->verts[vi].normal[0];
            v.ny = m->verts[vi].normal[1];
            v.nz = m->verts[vi].normal[2];
            ts_csg_poly_add_vert(&p, v);
        }
        ts_csg_poly_calc_plane(&p);
        ts_csg_polylist_push(&pl, p);
    }
    return pl;
}

/* Convert CSG polygons back to ts_mesh (fan triangulation for n>3 polys) */
static inline int ts_csg_polys_to_mesh(const ts_csg_polylist *pl, ts_mesh *out) {
    /* Count total vertices and triangles needed */
    int total_verts = 0;
    int total_tris = 0;
    for (int i = 0; i < pl->count; i++) {
        if (pl->items[i].count < 3) continue;
        total_verts += pl->items[i].count;
        total_tris += pl->items[i].count - 2;  /* fan triangulation */
    }

    ts_mesh_reserve(out, out->vert_count + total_verts,
                    out->tri_count + total_tris);

    for (int i = 0; i < pl->count; i++) {
        const ts_csg_poly *p = &pl->items[i];
        if (p->count < 3) continue;

        int base = out->vert_count;
        for (int j = 0; j < p->count; j++) {
            ts_mesh_add_vertex(out,
                p->verts[j].x, p->verts[j].y, p->verts[j].z,
                p->verts[j].nx, p->verts[j].ny, p->verts[j].nz);
        }

        /* Fan triangulation from vertex 0 */
        for (int j = 1; j < p->count - 1; j++) {
            ts_mesh_add_triangle(out, base, base + j, base + j + 1);
        }
    }

    return 0;
}

/* ================================================================
 * CSG BOOLEAN OPERATIONS
 *
 * union(A, B):
 *   a.clipTo(b), b.clipTo(a), b.invert(), b.clipTo(a), b.invert()
 *   result = a.allPolys + b.allPolys
 *
 * difference(A, B) = invert(union(invert(A), B)):
 *   a.invert(), a.clipTo(b), b.clipTo(a), b.invert(), b.clipTo(a),
 *   b.invert(), a.addPolys(b), a.invert()
 *
 * intersection(A, B):
 *   a.invert(), b.clipTo(a), b.invert(), a.clipTo(b), b.clipTo(a),
 *   a.addPolys(b), a.invert()
 * ================================================================ */

/*
 * Test if a polygon's AABB is entirely outside a bounding box.
 * Returns 1 if the polygon cannot intersect the box, 0 otherwise.
 */
static inline int ts_csg_poly_outside_aabb(const ts_csg_poly *p,
                                            const double mn[3],
                                            const double mx[3]) {
    for (int axis = 0; axis < 3; axis++) {
        int all_below = 1, all_above = 1;
        for (int i = 0; i < p->count; i++) {
            double v = (axis == 0) ? p->verts[i].x :
                       (axis == 1) ? p->verts[i].y : p->verts[i].z;
            if (v >= mn[axis]) all_below = 0;
            if (v <= mx[axis]) all_above = 0;
        }
        if (all_below || all_above) return 1;
    }
    return 0;
}

static inline void ts_csg_polylist_aabb(const ts_csg_polylist *pl,
                                         double mn[3], double mx[3]) {
    mn[0] = mn[1] = mn[2] = 1e30;
    mx[0] = mx[1] = mx[2] = -1e30;
    for (int i = 0; i < pl->count; i++) {
        for (int j = 0; j < pl->items[i].count; j++) {
            double x = pl->items[i].verts[j].x;
            double y = pl->items[i].verts[j].y;
            double z = pl->items[i].verts[j].z;
            if (x < mn[0]) mn[0] = x;
            if (x > mx[0]) mx[0] = x;
            if (y < mn[1]) mn[1] = y;
            if (y > mx[1]) mx[1] = y;
            if (z < mn[2]) mn[2] = z;
            if (z > mx[2]) mx[2] = z;
        }
    }
}

/*
 * Check if two AABBs overlap.
 */
static inline int ts_csg_aabb_overlap(const double a_mn[3], const double a_mx[3],
                                       const double b_mn[3], const double b_mx[3]) {
    return a_mn[0] <= b_mx[0] && a_mx[0] >= b_mn[0] &&
           a_mn[1] <= b_mx[1] && a_mx[1] >= b_mn[1] &&
           a_mn[2] <= b_mx[2] && a_mx[2] >= b_mn[2];
}

static inline int ts_csg_boolean(const ts_mesh *a, const ts_mesh *b,
                                  ts_csg_op_t op, ts_mesh *out) {
    if (!a || !b || !out) return TS_CSG_ERROR;
    if (a->tri_count == 0 && b->tri_count == 0) return TS_CSG_OK;

    /* Convert meshes to polygon lists */
    ts_csg_polylist polys_a = ts_csg_mesh_to_polys(a);
    ts_csg_polylist polys_b = ts_csg_mesh_to_polys(b);

    /* ============================================================
     * AABB EARLY EXIT: If meshes don't overlap, CSG is trivial.
     * ============================================================ */
    double a_mn[3], a_mx[3], b_mn[3], b_mx[3];
    ts_csg_polylist_aabb(&polys_a, a_mn, a_mx);
    ts_csg_polylist_aabb(&polys_b, b_mn, b_mx);

    /* Adaptive epsilon: scale relative to mesh extents.
     * For tiny meshes (mm scale), use smaller eps to avoid erasing features.
     * For huge meshes (1000+ units), use larger eps for numerical stability. */
    {
        double extent = 0;
        for (int i = 0; i < 3; i++) {
            double ea = a_mx[i] - a_mn[i];
            double eb = b_mx[i] - b_mn[i];
            if (ea > extent) extent = ea;
            if (eb > extent) extent = eb;
        }
        double eps = extent * 1e-8;
        if (eps < 1e-10) eps = 1e-10;
        if (eps > 1e-4) eps = 1e-4;
        ts_csg_eps_ = eps;
    }

    if (!ts_csg_aabb_overlap(a_mn, a_mx, b_mn, b_mx)) {
        /* No overlap: union = A+B, difference = A, intersection = empty */
        switch (op) {
        case TS_CSG_OP_UNION:
            ts_csg_polys_to_mesh(&polys_a, out);
            ts_csg_polys_to_mesh(&polys_b, out);
            break;
        case TS_CSG_OP_DIFFERENCE:
            ts_csg_polys_to_mesh(&polys_a, out);
            break;
        case TS_CSG_OP_INTERSECTION:
            /* Empty result */
            break;
        }
        ts_csg_polylist_free(&polys_a);
        ts_csg_polylist_free(&polys_b);
        return TS_CSG_OK;
    }

    /* ============================================================
     * SPATIAL PRE-FILTER for A's polygons
     *
     * Split A into A_near (polygons that overlap B's AABB) and
     * A_far (entirely outside B's AABB).
     *
     * A_near: needs full BSP processing against B
     * A_far: for difference/union, survives unchanged (outside B)
     *         for intersection, discarded (outside B)
     *
     * Both A_near and A_far go into BSP_A (needed for B.clipTo(A)),
     * but A_far is pre-tagged to skip the B-clipping pass.
     * ============================================================ */

    /* Build BSP trees */
    ts_csg_bsp *bsp_a = ts_csg_bsp_new();
    ts_csg_bsp *bsp_b = ts_csg_bsp_new();
    if (!bsp_a || !bsp_b) {
        ts_csg_polylist_free(&polys_a);
        ts_csg_polylist_free(&polys_b);
        ts_csg_bsp_free(bsp_a);
        ts_csg_bsp_free(bsp_b);
        return TS_CSG_ERROR;
    }

    ts_csg_bsp_build(bsp_a, &polys_a);
    ts_csg_bsp_build(bsp_b, &polys_b);

    switch (op) {
    case TS_CSG_OP_UNION:
        ts_csg_bsp_clip_to(bsp_a, bsp_b);
        ts_csg_bsp_clip_to(bsp_b, bsp_a);
        ts_csg_bsp_invert(bsp_b);
        ts_csg_bsp_clip_to(bsp_b, bsp_a);
        ts_csg_bsp_invert(bsp_b);
        {
            ts_csg_polylist b_polys = ts_csg_polylist_init();
            ts_csg_bsp_all_polys(bsp_b, &b_polys);
            ts_csg_bsp_add_polys(bsp_a, &b_polys);
        }
        break;

    case TS_CSG_OP_DIFFERENCE:
        ts_csg_bsp_invert(bsp_a);
        ts_csg_bsp_clip_to(bsp_a, bsp_b);
        ts_csg_bsp_clip_to(bsp_b, bsp_a);
        ts_csg_bsp_invert(bsp_b);
        ts_csg_bsp_clip_to(bsp_b, bsp_a);
        ts_csg_bsp_invert(bsp_b);
        {
            ts_csg_polylist b_polys = ts_csg_polylist_init();
            ts_csg_bsp_all_polys(bsp_b, &b_polys);
            ts_csg_bsp_add_polys(bsp_a, &b_polys);
        }
        ts_csg_bsp_invert(bsp_a);
        break;

    case TS_CSG_OP_INTERSECTION:
        ts_csg_bsp_invert(bsp_a);
        ts_csg_bsp_clip_to(bsp_b, bsp_a);
        ts_csg_bsp_invert(bsp_b);
        ts_csg_bsp_clip_to(bsp_a, bsp_b);
        ts_csg_bsp_clip_to(bsp_b, bsp_a);
        {
            ts_csg_polylist b_polys = ts_csg_polylist_init();
            ts_csg_bsp_all_polys(bsp_b, &b_polys);
            ts_csg_bsp_add_polys(bsp_a, &b_polys);
        }
        ts_csg_bsp_invert(bsp_a);
        break;
    }

    /* Collect result */
    ts_csg_polylist result = ts_csg_polylist_init();
    ts_csg_bsp_all_polys(bsp_a, &result);
    ts_csg_polys_to_mesh(&result, out);

    /* Cleanup */
    ts_csg_polylist_free(&result);
    ts_csg_bsp_free(bsp_a);
    ts_csg_bsp_free(bsp_b);

    return TS_CSG_OK;
}

/* Convenience wrappers */
static inline int ts_csg_union(const ts_mesh *a, const ts_mesh *b,
                                ts_mesh *out) {
    return ts_csg_boolean(a, b, TS_CSG_OP_UNION, out);
}

static inline int ts_csg_difference(const ts_mesh *a, const ts_mesh *b,
                                     ts_mesh *out) {
    return ts_csg_boolean(a, b, TS_CSG_OP_DIFFERENCE, out);
}

static inline int ts_csg_intersection(const ts_mesh *a, const ts_mesh *b,
                                       ts_mesh *out) {
    return ts_csg_boolean(a, b, TS_CSG_OP_INTERSECTION, out);
}

/* ================================================================
 * CONVEX HULL — Quickhull algorithm
 *
 * GPU parallelization:
 *   - Point-to-face distance: embarrassingly parallel
 *   - Furthest point selection: parallel reduction
 *   - Horizon edge detection: sequential per face, parallel across faces
 *
 * For meshes, we hull the vertex positions.
 * ================================================================ */

/* Internal: distance from point to plane */
static inline double ts_csg_point_plane_dist(double px, double py, double pz,
                                              double nx, double ny, double nz,
                                              double d) {
    return nx*px + ny*py + nz*pz - d;
}

/* Internal face for quickhull */
typedef struct {
    int v[3];       /* vertex indices */
    double n[3];    /* outward normal */
    double d;       /* plane offset */
    int dead;       /* marked for removal */
} ts_qh_face;

typedef struct {
    ts_qh_face *faces;
    int count, cap;
} ts_qh_facelist;

static inline void ts_qh_facelist_push(ts_qh_facelist *fl, ts_qh_face f) {
    if (fl->count >= fl->cap) {
        int nc = fl->cap ? fl->cap * 2 : 32;
        fl->faces = (ts_qh_face *)realloc(fl->faces,
            (size_t)nc * sizeof(ts_qh_face));
        fl->cap = nc;
    }
    fl->faces[fl->count++] = f;
}

static inline ts_qh_face ts_qh_make_face(const double *pts, int a, int b, int c) {
    ts_qh_face f;
    f.v[0] = a; f.v[1] = b; f.v[2] = c;
    f.dead = 0;
    double ex = pts[b*3+0]-pts[a*3+0], ey = pts[b*3+1]-pts[a*3+1], ez = pts[b*3+2]-pts[a*3+2];
    double fx = pts[c*3+0]-pts[a*3+0], fy = pts[c*3+1]-pts[a*3+1], fz = pts[c*3+2]-pts[a*3+2];
    f.n[0] = ey*fz - ez*fy;
    f.n[1] = ez*fx - ex*fz;
    f.n[2] = ex*fy - ey*fx;
    double len = sqrt(f.n[0]*f.n[0] + f.n[1]*f.n[1] + f.n[2]*f.n[2]);
    if (len > 1e-15) { f.n[0]/=len; f.n[1]/=len; f.n[2]/=len; }
    f.d = f.n[0]*pts[a*3+0] + f.n[1]*pts[a*3+1] + f.n[2]*pts[a*3+2];
    return f;
}

/* Quickhull: compute convex hull of point cloud, output as mesh */
static inline int ts_csg_hull(const ts_mesh *input, ts_mesh *out) {
    if (!input || !out) return TS_CSG_ERROR;
    if (input->vert_count < 4) return TS_CSG_ERROR;

    int npts = input->vert_count;
    /* Extract point array */
    double *pts = (double *)malloc((size_t)npts * 3 * sizeof(double));
    if (!pts) return TS_CSG_ERROR;
    for (int i = 0; i < npts; i++) {
        pts[i*3+0] = input->verts[i].pos[0];
        pts[i*3+1] = input->verts[i].pos[1];
        pts[i*3+2] = input->verts[i].pos[2];
    }

    /* Find initial tetrahedron: 4 non-coplanar points */
    /* Find two most distant points along X axis */
    int p0 = 0, p1 = 0;
    for (int i = 1; i < npts; i++) {
        if (pts[i*3] < pts[p0*3]) p0 = i;
        if (pts[i*3] > pts[p1*3]) p1 = i;
    }
    if (p0 == p1) { p1 = (p0 + 1) % npts; }

    /* Find point furthest from line p0-p1 */
    int p2 = -1;
    double max_dist = -1;
    {
        double lx = pts[p1*3]-pts[p0*3], ly = pts[p1*3+1]-pts[p0*3+1],
               lz = pts[p1*3+2]-pts[p0*3+2];
        for (int i = 0; i < npts; i++) {
            if (i == p0 || i == p1) continue;
            double dx = pts[i*3]-pts[p0*3], dy = pts[i*3+1]-pts[p0*3+1],
                   dz = pts[i*3+2]-pts[p0*3+2];
            /* Cross product magnitude */
            double cx = ly*dz - lz*dy, cy = lz*dx - lx*dz, cz = lx*dy - ly*dx;
            double d2 = cx*cx + cy*cy + cz*cz;
            if (d2 > max_dist) { max_dist = d2; p2 = i; }
        }
    }
    if (p2 < 0) { free(pts); return TS_CSG_ERROR; }

    /* Find point furthest from plane p0-p1-p2 */
    int p3 = -1;
    max_dist = -1;
    {
        double ex = pts[p1*3]-pts[p0*3], ey = pts[p1*3+1]-pts[p0*3+1],
               ez = pts[p1*3+2]-pts[p0*3+2];
        double fx = pts[p2*3]-pts[p0*3], fy = pts[p2*3+1]-pts[p0*3+1],
               fz = pts[p2*3+2]-pts[p0*3+2];
        double nx = ey*fz - ez*fy, ny = ez*fx - ex*fz, nz = ex*fy - ey*fx;
        double dd = nx*pts[p0*3] + ny*pts[p0*3+1] + nz*pts[p0*3+2];
        for (int i = 0; i < npts; i++) {
            if (i == p0 || i == p1 || i == p2) continue;
            double dist = fabs(nx*pts[i*3] + ny*pts[i*3+1] + nz*pts[i*3+2] - dd);
            if (dist > max_dist) { max_dist = dist; p3 = i; }
        }
    }
    if (p3 < 0) { free(pts); return TS_CSG_ERROR; }

    /* Build initial tetrahedron (4 faces) */
    ts_qh_facelist fl = { NULL, 0, 0 };

    /* Orient faces so normals point outward */
    ts_qh_face f0 = ts_qh_make_face(pts, p0, p1, p2);
    /* Check if p3 is in front or behind f0 */
    double test_d = ts_csg_point_plane_dist(
        pts[p3*3], pts[p3*3+1], pts[p3*3+2],
        f0.n[0], f0.n[1], f0.n[2], f0.d);
    if (test_d > 0) {
        /* p3 is in front of f0, so f0's normal should face away from p3.
         * Flip f0 by swapping two vertices. */
        ts_qh_facelist_push(&fl, ts_qh_make_face(pts, p0, p2, p1));
        ts_qh_facelist_push(&fl, ts_qh_make_face(pts, p0, p1, p3));
        ts_qh_facelist_push(&fl, ts_qh_make_face(pts, p1, p2, p3));
        ts_qh_facelist_push(&fl, ts_qh_make_face(pts, p2, p0, p3));
    } else {
        ts_qh_facelist_push(&fl, ts_qh_make_face(pts, p0, p1, p2));
        ts_qh_facelist_push(&fl, ts_qh_make_face(pts, p0, p3, p1));
        ts_qh_facelist_push(&fl, ts_qh_make_face(pts, p1, p3, p2));
        ts_qh_facelist_push(&fl, ts_qh_make_face(pts, p2, p3, p0));
    }

    /* Assign each remaining point to the face it's furthest in front of */
    int *assign = (int *)malloc((size_t)npts * sizeof(int));
    if (!assign) { free(pts); free(fl.faces); return TS_CSG_ERROR; }
    for (int i = 0; i < npts; i++) assign[i] = -1;

    /* Mark initial tetrahedron vertices as done */
    assign[p0] = assign[p1] = assign[p2] = assign[p3] = -2;

    for (int i = 0; i < npts; i++) {
        if (assign[i] == -2) continue;
        double best = TS_CSG_EPS;
        for (int f = 0; f < fl.count; f++) {
            if (fl.faces[f].dead) continue;
            double d = ts_csg_point_plane_dist(
                pts[i*3], pts[i*3+1], pts[i*3+2],
                fl.faces[f].n[0], fl.faces[f].n[1], fl.faces[f].n[2],
                fl.faces[f].d);
            if (d > best) { best = d; assign[i] = f; }
        }
    }

    /* Iterative quickhull expansion */
    int changed = 1;
    /* Horizon edge buffer */
    int *horizon_a = (int *)malloc((size_t)npts * 4 * sizeof(int));
    int *horizon_b = horizon_a ? horizon_a + npts : NULL;
    if (!horizon_a) { free(pts); free(assign); free(fl.faces); return TS_CSG_ERROR; }

    while (changed) {
        changed = 0;
        for (int fi = 0; fi < fl.count; fi++) {
            if (fl.faces[fi].dead) continue;

            /* Find furthest point assigned to this face */
            int best_pt = -1;
            double best_d = TS_CSG_EPS;
            for (int i = 0; i < npts; i++) {
                if (assign[i] != fi) continue;
                double d = ts_csg_point_plane_dist(
                    pts[i*3], pts[i*3+1], pts[i*3+2],
                    fl.faces[fi].n[0], fl.faces[fi].n[1], fl.faces[fi].n[2],
                    fl.faces[fi].d);
                if (d > best_d) { best_d = d; best_pt = i; }
            }
            if (best_pt < 0) continue;

            /* Find all visible faces from this point */
            int *visible = (int *)calloc((size_t)fl.count, sizeof(int));
            if (!visible) continue;
            for (int f = 0; f < fl.count; f++) {
                if (fl.faces[f].dead) continue;
                double d = ts_csg_point_plane_dist(
                    pts[best_pt*3], pts[best_pt*3+1], pts[best_pt*3+2],
                    fl.faces[f].n[0], fl.faces[f].n[1], fl.faces[f].n[2],
                    fl.faces[f].d);
                if (d > TS_CSG_EPS) visible[f] = 1;
            }

            /* Find horizon edges: edges shared between visible and non-visible */
            int n_horizon = 0;
            for (int f = 0; f < fl.count; f++) {
                if (!visible[f] || fl.faces[f].dead) continue;
                for (int e = 0; e < 3; e++) {
                    int ea = fl.faces[f].v[e];
                    int eb = fl.faces[f].v[(e+1)%3];

                    /* Check if the other face sharing this edge is not visible */
                    int shared_visible = 0;
                    for (int g = 0; g < fl.count; g++) {
                        if (g == f || !visible[g] || fl.faces[g].dead) continue;
                        /* Check if face g shares edge ea-eb (in reverse) */
                        for (int e2 = 0; e2 < 3; e2++) {
                            if (fl.faces[g].v[e2] == eb &&
                                fl.faces[g].v[(e2+1)%3] == ea) {
                                shared_visible = 1;
                                break;
                            }
                        }
                        if (shared_visible) break;
                    }
                    if (!shared_visible && n_horizon < npts) {
                        horizon_a[n_horizon] = ea;
                        horizon_b[n_horizon] = eb;
                        n_horizon++;
                    }
                }
            }

            /* Remove visible faces */
            for (int f = 0; f < fl.count; f++) {
                if (visible[f]) fl.faces[f].dead = 1;
            }

            /* Create new faces from horizon edges to the new point */
            for (int h = 0; h < n_horizon; h++) {
                ts_qh_face nf = ts_qh_make_face(pts,
                    horizon_a[h], horizon_b[h], best_pt);
                ts_qh_facelist_push(&fl, nf);
            }

            /* Mark point as used */
            assign[best_pt] = -2;

            /* Reassign orphaned points */
            for (int i = 0; i < npts; i++) {
                if (assign[i] >= 0 && (assign[i] >= fl.count ||
                    fl.faces[assign[i]].dead)) {
                    assign[i] = -1;
                    double best2 = TS_CSG_EPS;
                    for (int f = 0; f < fl.count; f++) {
                        if (fl.faces[f].dead) continue;
                        double d = ts_csg_point_plane_dist(
                            pts[i*3], pts[i*3+1], pts[i*3+2],
                            fl.faces[f].n[0], fl.faces[f].n[1],
                            fl.faces[f].n[2], fl.faces[f].d);
                        if (d > best2) { best2 = d; assign[i] = f; }
                    }
                }
            }

            free(visible);
            changed = 1;
            break; /* Restart scan */
        }
    }

    /* Build output mesh from non-dead faces */
    /* First, build a vertex index map (compact unique vertices) */
    int *vmap = (int *)malloc((size_t)npts * sizeof(int));
    if (!vmap) { free(pts); free(assign); free(horizon_a); free(fl.faces); return TS_CSG_ERROR; }
    for (int i = 0; i < npts; i++) vmap[i] = -1;

    for (int f = 0; f < fl.count; f++) {
        if (fl.faces[f].dead) continue;
        for (int j = 0; j < 3; j++) {
            int vi = fl.faces[f].v[j];
            if (vmap[vi] < 0) {
                vmap[vi] = ts_mesh_add_vertex(out,
                    pts[vi*3], pts[vi*3+1], pts[vi*3+2], 0, 0, 0);
            }
        }
        ts_mesh_add_triangle(out, vmap[fl.faces[f].v[0]],
                             vmap[fl.faces[f].v[1]],
                             vmap[fl.faces[f].v[2]]);
    }

    ts_mesh_compute_normals(out);

    free(pts);
    free(assign);
    free(horizon_a);
    free(fl.faces);
    free(vmap);

    return TS_CSG_OK;
}

/* ================================================================
 * MINKOWSKI SUM — Convex case
 *
 * For two convex meshes A and B:
 *   For each vertex a in A, translate all vertices of B by a.
 *   Take the convex hull of the combined point set.
 *
 * GPU: vertex translation is embarrassingly parallel.
 * Hull computation uses quickhull (see above).
 *
 * Non-convex case: decompose into convex parts first (future work).
 * For now, we support convex inputs only.
 * ================================================================ */

static inline int ts_csg_minkowski(const ts_mesh *a, const ts_mesh *b,
                                    ts_mesh *out) {
    if (!a || !b || !out) return TS_CSG_ERROR;
    if (a->vert_count == 0 || b->vert_count == 0) return TS_CSG_OK;

    /* Generate all Minkowski vertex sums */
    int total = a->vert_count * b->vert_count;
    ts_mesh combined = ts_mesh_init();
    ts_mesh_reserve(&combined, total, 0);

    for (int i = 0; i < a->vert_count; i++) {
        for (int j = 0; j < b->vert_count; j++) {
            ts_mesh_add_vertex(&combined,
                a->verts[i].pos[0] + b->verts[j].pos[0],
                a->verts[i].pos[1] + b->verts[j].pos[1],
                a->verts[i].pos[2] + b->verts[j].pos[2],
                0, 0, 0);
        }
    }

    /* Compute convex hull of the combined points */
    int ret = ts_csg_hull(&combined, out);
    ts_mesh_free(&combined);
    return ret;
}

#endif /* TS_CSG_H */
