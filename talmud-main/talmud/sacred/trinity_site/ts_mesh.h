/*
 * ts_mesh.h — Triangle mesh data structure
 *
 * Dynamic arrays of vertices and triangles.
 * This is the output format for all geometry generation.
 *
 * GPU: vertex buffers map directly to OpenCL/OpenGL buffers.
 * Normal computation is per-face parallel, then per-vertex reducible.
 *
 * Used by: ts_geo.h (cube, sphere, cylinder generation)
 */
#ifndef TS_MESH_H
#define TS_MESH_H

#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    double pos[3];
    double normal[3];
} ts_vertex;

typedef struct {
    int idx[3];   /* indices into vertex array */
} ts_triangle;

typedef struct {
    ts_vertex   *verts;
    ts_triangle *tris;
    int          vert_count;
    int          tri_count;
    int          vert_cap;
    int          tri_cap;
} ts_mesh;

/* --- Lifecycle --- */

static inline ts_mesh ts_mesh_init(void) {
    return (ts_mesh){ NULL, NULL, 0, 0, 0, 0 };
}

static inline void ts_mesh_free(ts_mesh *m) {
    free(m->verts);
    free(m->tris);
    m->verts = NULL;
    m->tris = NULL;
    m->vert_count = m->tri_count = 0;
    m->vert_cap = m->tri_cap = 0;
}

/* Pre-allocate capacity */
static inline int ts_mesh_reserve(ts_mesh *m, int verts, int tris) {
    if (verts > m->vert_cap) {
        ts_vertex *nv = (ts_vertex *)realloc(m->verts, (size_t)verts * sizeof(ts_vertex));
        if (!nv) return -1;
        m->verts = nv;
        m->vert_cap = verts;
    }
    if (tris > m->tri_cap) {
        ts_triangle *nt = (ts_triangle *)realloc(m->tris, (size_t)tris * sizeof(ts_triangle));
        if (!nt) return -1;
        m->tris = nt;
        m->tri_cap = tris;
    }
    return 0;
}

/* --- Add vertex, returns index --- */
static inline int ts_mesh_add_vertex(ts_mesh *m, double x, double y, double z,
                                     double nx, double ny, double nz) {
    if (m->vert_count >= m->vert_cap) {
        int newcap = m->vert_cap ? m->vert_cap * 2 : 64;
        ts_vertex *nv = (ts_vertex *)realloc(m->verts, (size_t)newcap * sizeof(ts_vertex));
        if (!nv) return -1;
        m->verts = nv;
        m->vert_cap = newcap;
    }
    ts_vertex *v = &m->verts[m->vert_count];
    v->pos[0] = x;  v->pos[1] = y;  v->pos[2] = z;
    v->normal[0] = nx; v->normal[1] = ny; v->normal[2] = nz;
    return m->vert_count++;
}

/* --- Add triangle --- */
static inline int ts_mesh_add_triangle(ts_mesh *m, int a, int b, int c) {
    if (m->tri_count >= m->tri_cap) {
        int newcap = m->tri_cap ? m->tri_cap * 2 : 64;
        ts_triangle *nt = (ts_triangle *)realloc(m->tris, (size_t)newcap * sizeof(ts_triangle));
        if (!nt) return -1;
        m->tris = nt;
        m->tri_cap = newcap;
    }
    ts_triangle *t = &m->tris[m->tri_count];
    t->idx[0] = a; t->idx[1] = b; t->idx[2] = c;
    m->tri_count++;
    return 0;
}

/* --- Recompute face normals --- */
/* GPU: each face independent — trivially parallel */
static inline void ts_mesh_compute_normals(ts_mesh *m) {
    /* Zero all normals */
    for (int i = 0; i < m->vert_count; i++) {
        m->verts[i].normal[0] = 0.0;
        m->verts[i].normal[1] = 0.0;
        m->verts[i].normal[2] = 0.0;
    }

    /* Accumulate face normals to vertices */
    for (int i = 0; i < m->tri_count; i++) {
        int a = m->tris[i].idx[0];
        int b = m->tris[i].idx[1];
        int c = m->tris[i].idx[2];

        double e1[3], e2[3], n[3];
        for (int j = 0; j < 3; j++) {
            e1[j] = m->verts[b].pos[j] - m->verts[a].pos[j];
            e2[j] = m->verts[c].pos[j] - m->verts[a].pos[j];
        }
        n[0] = e1[1]*e2[2] - e1[2]*e2[1];
        n[1] = e1[2]*e2[0] - e1[0]*e2[2];
        n[2] = e1[0]*e2[1] - e1[1]*e2[0];

        /* Add to each vertex of the face */
        for (int j = 0; j < 3; j++) {
            m->verts[a].normal[j] += n[j];
            m->verts[b].normal[j] += n[j];
            m->verts[c].normal[j] += n[j];
        }
    }

    /* Normalize all vertex normals */
    for (int i = 0; i < m->vert_count; i++) {
        double *n = m->verts[i].normal;
        double len = sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (len > 1e-15) {
            n[0] /= len; n[1] /= len; n[2] /= len;
        }
    }
}

/* --- Bounding box --- */
static inline void ts_mesh_bounds(const ts_mesh *m,
                                  double min_out[3], double max_out[3]) {
    if (m->vert_count == 0) {
        min_out[0] = min_out[1] = min_out[2] = 0.0;
        max_out[0] = max_out[1] = max_out[2] = 0.0;
        return;
    }
    for (int j = 0; j < 3; j++) {
        min_out[j] = max_out[j] = m->verts[0].pos[j];
    }
    for (int i = 1; i < m->vert_count; i++) {
        for (int j = 0; j < 3; j++) {
            if (m->verts[i].pos[j] < min_out[j]) min_out[j] = m->verts[i].pos[j];
            if (m->verts[i].pos[j] > max_out[j]) max_out[j] = m->verts[i].pos[j];
        }
    }
}

#endif /* TS_MESH_H */
