/*
 * dc_topo.h — Mesh topology analysis: face grouping + edge extraction.
 *
 * Pure C, no GL dependency. Works on interleaved STL mesh data:
 *   [nx, ny, nz, vx, vy, vz] per vertex, 3 vertices per triangle.
 *
 * Face group = set of adjacent triangles sharing the same normal.
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
    DC_FaceGroup *face_groups;
    int           face_count;

    DC_Edge      *edges;
    int           edge_count;

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
 * Normal comparison
 * ========================================================================= */

#ifndef DC_TOPO_NORMAL_EPS
#define DC_TOPO_NORMAL_EPS 1e-4f
#endif

/* Returns 1 if normals are nearly identical (dot product close to 1.0). */
static inline int
dc_topo_normals_match(const float *n1, const float *n2)
{
    float dot = n1[0]*n2[0] + n1[1]*n2[1] + n1[2]*n2[2];
    return dot > (1.0f - DC_TOPO_NORMAL_EPS);
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

    /* Fill tri_indices and set normals from first triangle in each group */
    for (int t = 0; t < num_triangles; t++) {
        int g = topo->tri_to_face[t];
        DC_FaceGroup *fg = &topo->face_groups[g];
        if (fg->tri_count == 0)
            memcpy(fg->normal, normals + t * 3, 3 * sizeof(float));
        fg->tri_indices[fg->tri_count++] = t;
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
    free(topo->tri_to_face);
    free(topo);
}

#endif /* DC_TOPO_H */
