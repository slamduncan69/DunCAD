/*
 * dc_topo.h — Mesh topology analysis: face grouping + edge extraction.
 *
 * Pure C, no GL dependency. Works on interleaved STL mesh data:
 *   [nx, ny, nz, vx, vy, vz] per vertex, 3 vertices per triangle.
 *
 * Face group = set of adjacent triangles on a smooth surface.
 * Two adjacent triangles are merged if their dihedral angle is below
 * DC_TOPO_SMOOTH_ANGLE (default 30°). This groups curved surfaces
 * (cylinder sides, spheres) as single selectable faces.
 * Edge = boundary between two different face groups (or mesh boundary).
 *
 * Usage:
 *   DC_Topo *topo = dc_topo_build(mesh_data, num_triangles);
 *   // query face groups, edges, tri-to-face map
 *   dc_topo_free(topo);
 */
#ifndef DC_TOPO_H
#define DC_TOPO_H

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* =========================================================================
 * Public types
 * ========================================================================= */

typedef struct {
    float    normal[3];       /* unit normal of face group */
    int     *tri_indices;     /* triangle indices in this group */
    int      tri_count;       /* number of triangles */
} DC_FaceGroup;

typedef struct {
    float    a[3], b[3];      /* edge endpoints (canonical order: a < b) */
    int      face_a;          /* face group on one side (-1 if boundary) */
    int      face_b;          /* face group on other side (-1 if boundary) */
} DC_Edge;

typedef struct {
    int     *edge_indices;    /* indices into DC_Topo.edges[] */
    int      edge_count;      /* number of edge segments in this group */
} DC_EdgeGroup;

typedef struct {
    DC_FaceGroup *face_groups;
    int           face_count;

    DC_Edge      *edges;
    int           edge_count;

    DC_EdgeGroup *edge_groups;  /* smooth edge groups (curves/loops) */
    int           edge_group_count;
    int          *edge_to_group; /* [edge_count] -> edge group index */

    int          *tri_to_face; /* [num_triangles] -> face group index */
    int           num_triangles;
} DC_Topo;

/* =========================================================================
 * Public API
 * ========================================================================= */

/* Build topology from interleaved mesh data.
 * data layout: [nx,ny,nz, vx,vy,vz] * 3 verts * num_triangles
 * Returns NULL on failure. Caller frees with dc_topo_free(). */
static DC_Topo *dc_topo_build(const float *data, int num_triangles);

/* Free topology. Safe with NULL. */
static void dc_topo_free(DC_Topo *topo);

/* ---- Pick-color encoding for face/edge selection ----
 *
 * In object mode:  id = obj_idx + 1  (existing behavior)
 * In face/edge mode: encode (obj_idx, sub_id) as 24-bit RGB.
 *   Layout: R = low 8 bits of sub_id
 *           G = high 8 bits of sub_id
 *           B = obj_idx + 1
 *   This gives 65536 sub-elements per object, 255 objects. */

#define DC_TOPO_MAX_SUB 65536

static inline void
dc_topo_sub_to_color(int obj_idx, int sub_id, float *rgb)
{
    int id = sub_id + 1; /* 0 reserved for background */
    int obj = obj_idx + 1;
    rgb[0] = (float)(id & 0xFF) / 255.0f;
    rgb[1] = (float)((id >> 8) & 0xFF) / 255.0f;
    rgb[2] = (float)(obj & 0xFF) / 255.0f;
}

/* Decode pixel back to (obj_idx, sub_id). Returns -1,-1 if background. */
static inline void
dc_topo_color_to_sub(unsigned char r, unsigned char g, unsigned char b,
                     int *out_obj, int *out_sub)
{
    int obj = (int)b;
    int id = (int)r | ((int)g << 8);
    *out_obj = obj - 1;  /* -1 if b==0 (background) */
    *out_sub = id - 1;   /* -1 if r==0 && g==0 (background) */
}

/* =========================================================================
 * Internal: hash map for edge adjacency
 * ========================================================================= */

/* Quantize a float to an integer grid for vertex comparison.
 * Grid size = 1e-4 (0.1mm precision, suitable for 3D print meshes). */
#define DC_TOPO_QUANT 10000

typedef struct {
    int ax, ay, az;  /* quantized vertex A */
    int bx, by, bz;  /* quantized vertex B (canonical: A <= B) */
} DC_TopoEdgeKey;

typedef struct {
    DC_TopoEdgeKey key;
    int            tri_a;    /* first triangle using this edge */
    int            tri_b;    /* second triangle (-1 if only one) */
    float          a[3];     /* actual (non-quantized) vertex A */
    float          b[3];     /* actual (non-quantized) vertex B */
    int            occupied; /* 1 = slot in use */
} DC_TopoEdgeSlot;

/* Open-addressed hash table for edge adjacency */
typedef struct {
    DC_TopoEdgeSlot *slots;
    int              capacity;
    int              count;
} DC_TopoEdgeMap;

static inline int
dc_topo_quant(float v)
{
    return (int)roundf(v * DC_TOPO_QUANT);
}

/* Canonical ordering: ensure A <= B lexicographically */
static inline void
dc_topo_canon_edge(int ax, int ay, int az, int bx, int by, int bz,
                   DC_TopoEdgeKey *out)
{
    if (ax < bx || (ax == bx && ay < by) ||
        (ax == bx && ay == by && az < bz)) {
        out->ax = ax; out->ay = ay; out->az = az;
        out->bx = bx; out->by = by; out->bz = bz;
    } else {
        out->ax = bx; out->ay = by; out->az = bz;
        out->bx = ax; out->by = ay; out->bz = az;
    }
}

static inline unsigned int
dc_topo_hash_key(const DC_TopoEdgeKey *k)
{
    /* FNV-1a inspired mixing */
    unsigned int h = 2166136261u;
    h ^= (unsigned int)k->ax; h *= 16777619u;
    h ^= (unsigned int)k->ay; h *= 16777619u;
    h ^= (unsigned int)k->az; h *= 16777619u;
    h ^= (unsigned int)k->bx; h *= 16777619u;
    h ^= (unsigned int)k->by; h *= 16777619u;
    h ^= (unsigned int)k->bz; h *= 16777619u;
    return h;
}

static inline int
dc_topo_key_eq(const DC_TopoEdgeKey *a, const DC_TopoEdgeKey *b)
{
    return a->ax == b->ax && a->ay == b->ay && a->az == b->az &&
           a->bx == b->bx && a->by == b->by && a->bz == b->bz;
}

static DC_TopoEdgeMap *
dc_topo_edgemap_new(int capacity)
{
    DC_TopoEdgeMap *m = (DC_TopoEdgeMap *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    /* Round up to power of 2 */
    int cap = 64;
    while (cap < capacity) cap *= 2;
    m->slots = (DC_TopoEdgeSlot *)calloc((size_t)cap, sizeof(DC_TopoEdgeSlot));
    if (!m->slots) { free(m); return NULL; }
    m->capacity = cap;
    m->count = 0;
    return m;
}

static void
dc_topo_edgemap_free(DC_TopoEdgeMap *m)
{
    if (!m) return;
    free(m->slots);
    free(m);
}

/* Insert or update an edge. If edge exists, sets tri_b. If new, sets tri_a.
 * Returns pointer to the slot. */
static DC_TopoEdgeSlot *
dc_topo_edgemap_insert(DC_TopoEdgeMap *m, const DC_TopoEdgeKey *key,
                       int tri_idx, const float *va, const float *vb)
{
    unsigned int mask = (unsigned int)(m->capacity - 1);
    unsigned int idx = dc_topo_hash_key(key) & mask;

    for (;;) {
        DC_TopoEdgeSlot *s = &m->slots[idx];
        if (!s->occupied) {
            /* New slot */
            s->key = *key;
            s->tri_a = tri_idx;
            s->tri_b = -1;
            memcpy(s->a, va, 3 * sizeof(float));
            memcpy(s->b, vb, 3 * sizeof(float));
            s->occupied = 1;
            m->count++;
            return s;
        }
        if (dc_topo_key_eq(&s->key, key)) {
            /* Existing edge — record second triangle */
            if (s->tri_b == -1) s->tri_b = tri_idx;
            return s;
        }
        idx = (idx + 1) & mask;
    }
}

/* =========================================================================
 * Union-Find for flood-fill grouping
 * ========================================================================= */

static int
dc_topo_uf_find(int *parent, int x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]]; /* path compression */
        x = parent[x];
    }
    return x;
}

static void
dc_topo_uf_union(int *parent, int *rank, int a, int b)
{
    a = dc_topo_uf_find(parent, a);
    b = dc_topo_uf_find(parent, b);
    if (a == b) return;
    if (rank[a] < rank[b]) { int t = a; a = b; b = t; }
    parent[b] = a;
    if (rank[a] == rank[b]) rank[a]++;
}

/* =========================================================================
 * Smooth surface detection (dihedral angle threshold)
 * ========================================================================= */

/* Maximum dihedral angle (degrees) for two adjacent triangles to be
 * considered part of the same smooth surface. Default 30° works well
 * for cylinders ($fn>=16), spheres, cones, and fillets while preserving
 * sharp edges (90° cube edges, 45° chamfers). */
#ifndef DC_TOPO_SMOOTH_ANGLE
#define DC_TOPO_SMOOTH_ANGLE 30.0f
#endif

/* Returns 1 if adjacent triangle normals are smooth (dihedral angle
 * below threshold). This merges curved surfaces like cylinder sides.
 * cos(30°) ≈ 0.866, cos(45°) ≈ 0.707, cos(60°) ≈ 0.5. */
static inline int
dc_topo_normals_match(const float *n1, const float *n2)
{
    static float cos_thresh = -1.0f;
    if (cos_thresh < 0.0f)
        cos_thresh = cosf(DC_TOPO_SMOOTH_ANGLE * 3.14159265f / 180.0f);
    float dot = n1[0]*n2[0] + n1[1]*n2[1] + n1[2]*n2[2];
    return dot > cos_thresh;
}

/* =========================================================================
 * Main build function
 * ========================================================================= */

/* Get triangle normal from interleaved data.
 * data layout per tri: [nx,ny,nz, vx,vy,vz] * 3 vertices
 * Normal is the same for all 3 vertices (STL format). */
static inline void
dc_topo_tri_normal(const float *data, int tri, float *n)
{
    const float *v = data + tri * 18; /* 3 verts * 6 floats */
    n[0] = v[0]; n[1] = v[1]; n[2] = v[2];
}

/* Get triangle vertex position.
 * vert_in_tri: 0, 1, or 2. */
static inline void
dc_topo_tri_vert(const float *data, int tri, int vert_in_tri, float *pos)
{
    const float *v = data + tri * 18 + vert_in_tri * 6;
    pos[0] = v[3]; pos[1] = v[4]; pos[2] = v[5];
}

static DC_Topo *
dc_topo_build(const float *data, int num_triangles)
{
    if (!data || num_triangles <= 0) return NULL;

    DC_Topo *topo = (DC_Topo *)calloc(1, sizeof(*topo));
    if (!topo) return NULL;
    topo->num_triangles = num_triangles;

    /* Allocate union-find arrays */
    int *parent = (int *)malloc((size_t)num_triangles * sizeof(int));
    int *rank_arr = (int *)calloc((size_t)num_triangles, sizeof(int));
    if (!parent || !rank_arr) {
        free(parent); free(rank_arr); free(topo);
        return NULL;
    }
    for (int i = 0; i < num_triangles; i++) parent[i] = i;

    /* Build edge adjacency map.
     * Each triangle has 3 edges. Total edges <= 3*T, but many shared.
     * Load factor < 0.6: capacity = T * 6. */
    DC_TopoEdgeMap *emap = dc_topo_edgemap_new(num_triangles * 6);
    if (!emap) {
        free(parent); free(rank_arr); free(topo);
        return NULL;
    }

    for (int t = 0; t < num_triangles; t++) {
        float v[3][3];
        for (int vi = 0; vi < 3; vi++)
            dc_topo_tri_vert(data, t, vi, v[vi]);

        /* 3 edges per triangle: (0,1), (1,2), (2,0) */
        int pairs[3][2] = {{0,1},{1,2},{2,0}};
        for (int e = 0; e < 3; e++) {
            int a = pairs[e][0], b = pairs[e][1];
            int qax = dc_topo_quant(v[a][0]), qay = dc_topo_quant(v[a][1]),
                qaz = dc_topo_quant(v[a][2]);
            int qbx = dc_topo_quant(v[b][0]), qby = dc_topo_quant(v[b][1]),
                qbz = dc_topo_quant(v[b][2]);

            DC_TopoEdgeKey key;
            dc_topo_canon_edge(qax, qay, qaz, qbx, qby, qbz, &key);
            dc_topo_edgemap_insert(emap, &key, t, v[a], v[b]);
        }
    }

    /* Union adjacent triangles with matching normals */
    float *normals = (float *)malloc((size_t)num_triangles * 3 * sizeof(float));
    if (!normals) {
        dc_topo_edgemap_free(emap);
        free(parent); free(rank_arr); free(topo);
        return NULL;
    }
    for (int t = 0; t < num_triangles; t++)
        dc_topo_tri_normal(data, t, normals + t * 3);

    for (int i = 0; i < emap->capacity; i++) {
        DC_TopoEdgeSlot *s = &emap->slots[i];
        if (!s->occupied || s->tri_b == -1) continue;
        /* Two triangles share this edge — merge if normals match */
        if (dc_topo_normals_match(normals + s->tri_a * 3,
                                  normals + s->tri_b * 3)) {
            dc_topo_uf_union(parent, rank_arr, s->tri_a, s->tri_b);
        }
    }

    /* Collect face groups from union-find roots */
    /* First pass: count groups and map root->group_idx */
    int *root_to_group = (int *)malloc((size_t)num_triangles * sizeof(int));
    topo->tri_to_face = (int *)malloc((size_t)num_triangles * sizeof(int));
    if (!root_to_group || !topo->tri_to_face) {
        free(normals); free(root_to_group); dc_topo_edgemap_free(emap);
        free(parent); free(rank_arr); dc_topo_free(topo);
        return NULL;
    }
    memset(root_to_group, -1, (size_t)num_triangles * sizeof(int));

    int group_count = 0;
    for (int t = 0; t < num_triangles; t++) {
        int root = dc_topo_uf_find(parent, t);
        if (root_to_group[root] == -1) {
            root_to_group[root] = group_count++;
        }
        topo->tri_to_face[t] = root_to_group[root];
    }

    /* Allocate face groups */
    topo->face_count = group_count;
    topo->face_groups = (DC_FaceGroup *)calloc((size_t)group_count,
                                               sizeof(DC_FaceGroup));
    if (!topo->face_groups) {
        free(normals); free(root_to_group); dc_topo_edgemap_free(emap);
        free(parent); free(rank_arr); dc_topo_free(topo);
        return NULL;
    }

    /* Count triangles per group */
    for (int t = 0; t < num_triangles; t++)
        topo->face_groups[topo->tri_to_face[t]].tri_count++;

    /* Allocate tri_indices arrays and set normals */
    for (int g = 0; g < group_count; g++) {
        DC_FaceGroup *fg = &topo->face_groups[g];
        fg->tri_indices = (int *)malloc((size_t)fg->tri_count * sizeof(int));
        if (!fg->tri_indices) {
            free(normals); free(root_to_group); dc_topo_edgemap_free(emap);
            free(parent); free(rank_arr); dc_topo_free(topo);
            return NULL;
        }
        fg->tri_count = 0; /* reset for fill pass */
    }

    /* Fill tri_indices and accumulate normals (averaged for smooth groups) */
    for (int t = 0; t < num_triangles; t++) {
        int g = topo->tri_to_face[t];
        DC_FaceGroup *fg = &topo->face_groups[g];
        fg->normal[0] += normals[t * 3 + 0];
        fg->normal[1] += normals[t * 3 + 1];
        fg->normal[2] += normals[t * 3 + 2];
        fg->tri_indices[fg->tri_count++] = t;
    }
    /* Normalize averaged normals */
    for (int g = 0; g < group_count; g++) {
        DC_FaceGroup *fg = &topo->face_groups[g];
        float len = sqrtf(fg->normal[0]*fg->normal[0] +
                          fg->normal[1]*fg->normal[1] +
                          fg->normal[2]*fg->normal[2]);
        if (len > 1e-10f) {
            fg->normal[0] /= len;
            fg->normal[1] /= len;
            fg->normal[2] /= len;
        }
    }

    /* Extract edges: boundaries between different face groups */
    /* Count first */
    int edge_count = 0;
    for (int i = 0; i < emap->capacity; i++) {
        DC_TopoEdgeSlot *s = &emap->slots[i];
        if (!s->occupied) continue;
        int fa = topo->tri_to_face[s->tri_a];
        int fb = (s->tri_b >= 0) ? topo->tri_to_face[s->tri_b] : -1;
        if (fa != fb) edge_count++;
    }

    topo->edge_count = edge_count;
    topo->edges = (DC_Edge *)malloc((size_t)edge_count * sizeof(DC_Edge));
    if (!topo->edges && edge_count > 0) {
        free(normals); free(root_to_group); dc_topo_edgemap_free(emap);
        free(parent); free(rank_arr); dc_topo_free(topo);
        return NULL;
    }

    /* Fill edges */
    int ei = 0;
    for (int i = 0; i < emap->capacity; i++) {
        DC_TopoEdgeSlot *s = &emap->slots[i];
        if (!s->occupied) continue;
        int fa = topo->tri_to_face[s->tri_a];
        int fb = (s->tri_b >= 0) ? topo->tri_to_face[s->tri_b] : -1;
        if (fa != fb) {
            DC_Edge *e = &topo->edges[ei++];
            memcpy(e->a, s->a, 3 * sizeof(float));
            memcpy(e->b, s->b, 3 * sizeof(float));
            e->face_a = fa;
            e->face_b = fb;
        }
    }

    /* ---- Edge grouping: merge connected edges with smooth direction ---- */
    if (edge_count > 0) {
        /* Reuse parent/rank for edge union-find */
        int *eparent = (int *)malloc((size_t)edge_count * sizeof(int));
        int *erank = (int *)calloc((size_t)edge_count, sizeof(int));
        if (eparent && erank) {
            for (int i = 0; i < edge_count; i++) eparent[i] = i;

            /* For each pair of edges sharing a vertex, check direction
             * continuity: angle between edge directions < threshold.
             *
             * Use a vertex hash map: quantized vertex → first edge index.
             * When a second edge maps to the same vertex, test and merge. */
            int vcap = edge_count * 4;
            if (vcap < 64) vcap = 64;
            /* Round to power of 2 */
            int vc2 = 64;
            while (vc2 < vcap) vc2 *= 2;
            vcap = vc2;

            /* Vertex hash table: key = quantized vertex, value = list of
             * (edge_idx, is_b) pairs. Simple approach: for each edge endpoint,
             * find other edges at the same vertex and test merging. */
            typedef struct { int qx, qy, qz; int edge_idx; int is_b; int next; } VSlot;
            int vslot_count = edge_count * 2;
            VSlot *vslots = (VSlot *)calloc((size_t)vslot_count, sizeof(VSlot));
            int *vbuckets = (int *)malloc((size_t)vcap * sizeof(int));
            if (vslots && vbuckets) {
                memset(vbuckets, -1, (size_t)vcap * sizeof(int));
                unsigned int vmask = (unsigned int)(vcap - 1);
                int vsi = 0;

                float cos_thresh_edge = cosf(DC_TOPO_SMOOTH_ANGLE * 3.14159265f / 180.0f);

                for (int e = 0; e < edge_count; e++) {
                    DC_Edge *ed = &topo->edges[e];
                    /* Direction vector of this edge */
                    float dx = ed->b[0] - ed->a[0];
                    float dy = ed->b[1] - ed->a[1];
                    float dz = ed->b[2] - ed->a[2];
                    float dlen = sqrtf(dx*dx + dy*dy + dz*dz);
                    if (dlen < 1e-10f) continue;
                    dx /= dlen; dy /= dlen; dz /= dlen;

                    /* Process both endpoints */
                    for (int ep = 0; ep < 2; ep++) {
                        float *pt = (ep == 0) ? ed->a : ed->b;
                        /* Direction pointing away from this endpoint */
                        float ex = (ep == 0) ? dx : -dx;
                        float ey = (ep == 0) ? dy : -dy;
                        float ez = (ep == 0) ? dz : -dz;

                        int qx = dc_topo_quant(pt[0]);
                        int qy = dc_topo_quant(pt[1]);
                        int qz = dc_topo_quant(pt[2]);
                        unsigned int vh = ((unsigned int)qx * 73856093u) ^
                                          ((unsigned int)qy * 19349663u) ^
                                          ((unsigned int)qz * 83492791u);
                        unsigned int bi = vh & vmask;

                        /* Check existing edges at this vertex */
                        int si = vbuckets[bi];
                        while (si >= 0) {
                            VSlot *vs = &vslots[si];
                            if (vs->qx == qx && vs->qy == qy && vs->qz == qz) {
                                /* Same vertex — check direction continuity */
                                int oe = vs->edge_idx;
                                DC_Edge *other = &topo->edges[oe];
                                float odx = other->b[0] - other->a[0];
                                float ody = other->b[1] - other->a[1];
                                float odz = other->b[2] - other->a[2];
                                float olen = sqrtf(odx*odx + ody*ody + odz*odz);
                                if (olen > 1e-10f) {
                                    odx /= olen; ody /= olen; odz /= olen;
                                    /* Direction pointing away from shared vertex */
                                    float ox, oy, oz;
                                    if (vs->is_b) {
                                        ox = -odx; oy = -ody; oz = -odz;
                                    } else {
                                        ox = odx; oy = ody; oz = odz;
                                    }
                                    /* Dot product of outgoing directions.
                                     * For smooth continuation, directions should
                                     * be nearly opposite (edges continue through
                                     * the vertex), so dot ≈ -1. We check
                                     * |dot| > cos_thresh which catches both
                                     * same-direction and opposite-direction. */
                                    float dot = ex*ox + ey*oy + ez*oz;
                                    if (dot < -cos_thresh_edge) {
                                        /* Smooth continuation — merge */
                                        dc_topo_uf_union(eparent, erank, e, oe);
                                    }
                                }
                            }
                            si = vs->next;
                        }

                        /* Insert this edge endpoint */
                        if (vsi < vslot_count) {
                            vslots[vsi].qx = qx;
                            vslots[vsi].qy = qy;
                            vslots[vsi].qz = qz;
                            vslots[vsi].edge_idx = e;
                            vslots[vsi].is_b = ep;
                            vslots[vsi].next = vbuckets[bi];
                            vbuckets[bi] = vsi;
                            vsi++;
                        }
                    }
                }

                /* Collect edge groups from union-find */
                int *eroot_to_group = (int *)malloc((size_t)edge_count * sizeof(int));
                topo->edge_to_group = (int *)malloc((size_t)edge_count * sizeof(int));
                if (eroot_to_group && topo->edge_to_group) {
                    memset(eroot_to_group, -1, (size_t)edge_count * sizeof(int));
                    int egroup_count = 0;
                    for (int e = 0; e < edge_count; e++) {
                        int root = dc_topo_uf_find(eparent, e);
                        if (eroot_to_group[root] == -1)
                            eroot_to_group[root] = egroup_count++;
                        topo->edge_to_group[e] = eroot_to_group[root];
                    }

                    topo->edge_group_count = egroup_count;
                    topo->edge_groups = (DC_EdgeGroup *)calloc(
                        (size_t)egroup_count, sizeof(DC_EdgeGroup));
                    if (topo->edge_groups) {
                        /* Count edges per group */
                        for (int e = 0; e < edge_count; e++)
                            topo->edge_groups[topo->edge_to_group[e]].edge_count++;
                        /* Allocate */
                        for (int g = 0; g < egroup_count; g++) {
                            topo->edge_groups[g].edge_indices = (int *)malloc(
                                (size_t)topo->edge_groups[g].edge_count * sizeof(int));
                            topo->edge_groups[g].edge_count = 0; /* reset for fill */
                        }
                        /* Fill */
                        for (int e = 0; e < edge_count; e++) {
                            int g = topo->edge_to_group[e];
                            DC_EdgeGroup *eg = &topo->edge_groups[g];
                            eg->edge_indices[eg->edge_count++] = e;
                        }
                    }
                }
                free(eroot_to_group);
            }
            free(vslots);
            free(vbuckets);
        }
        free(eparent);
        free(erank);
    }

    /* Cleanup */
    free(normals);
    free(root_to_group);
    dc_topo_edgemap_free(emap);
    free(parent);
    free(rank_arr);

    return topo;
}

static void
dc_topo_free(DC_Topo *topo)
{
    if (!topo) return;
    if (topo->face_groups) {
        for (int i = 0; i < topo->face_count; i++)
            free(topo->face_groups[i].tri_indices);
        free(topo->face_groups);
    }
    free(topo->edges);
    if (topo->edge_groups) {
        for (int i = 0; i < topo->edge_group_count; i++)
            free(topo->edge_groups[i].edge_indices);
        free(topo->edge_groups);
    }
    free(topo->edge_to_group);
    free(topo->tri_to_face);
    free(topo);
}

#endif /* DC_TOPO_H */
