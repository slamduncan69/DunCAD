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

#include <stdio.h>
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

/* --- Transform all vertices by mat4 --- */
/* Transforms positions as points (w=1), normals as directions (w=0).
 * Flips winding if determinant is negative (mirror transforms). */
static inline void ts_mesh_transform(ts_mesh *m, const double mat[16]) {
    /* Check if transform flips winding (negative determinant) */
    double det = mat[0] * (mat[5]*mat[10] - mat[6]*mat[9])
               - mat[1] * (mat[4]*mat[10] - mat[6]*mat[8])
               + mat[2] * (mat[4]*mat[9]  - mat[5]*mat[8]);
    int flip = (det < 0);

    for (int i = 0; i < m->vert_count; i++) {
        double *p = m->verts[i].pos;
        double *n = m->verts[i].normal;
        double px = p[0], py = p[1], pz = p[2];
        double nx = n[0], ny = n[1], nz = n[2];

        /* Transform position (w=1) */
        p[0] = mat[0]*px + mat[1]*py + mat[2]*pz  + mat[3];
        p[1] = mat[4]*px + mat[5]*py + mat[6]*pz  + mat[7];
        p[2] = mat[8]*px + mat[9]*py + mat[10]*pz + mat[11];

        /* Transform normal (w=0, no translation) */
        n[0] = mat[0]*nx + mat[1]*ny + mat[2]*nz;
        n[1] = mat[4]*nx + mat[5]*ny + mat[6]*nz;
        n[2] = mat[8]*nx + mat[9]*ny + mat[10]*nz;

        /* Renormalize */
        double len = sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (len > 1e-15) { n[0] /= len; n[1] /= len; n[2] /= len; }
    }

    /* Flip triangle winding if mirrored */
    if (flip) {
        for (int i = 0; i < m->tri_count; i++) {
            int tmp = m->tris[i].idx[1];
            m->tris[i].idx[1] = m->tris[i].idx[2];
            m->tris[i].idx[2] = tmp;
        }
    }
}

/* --- Append mesh b into mesh a --- */
static inline void ts_mesh_append(ts_mesh *a, const ts_mesh *b) {
    if (b->vert_count == 0) return;
    ts_mesh_reserve(a, a->vert_count + b->vert_count,
                    a->tri_count + b->tri_count);
    int base = a->vert_count;
    memcpy(a->verts + a->vert_count, b->verts,
           (size_t)b->vert_count * sizeof(ts_vertex));
    a->vert_count += b->vert_count;
    for (int i = 0; i < b->tri_count; i++) {
        ts_mesh_add_triangle(a,
            base + b->tris[i].idx[0],
            base + b->tris[i].idx[1],
            base + b->tris[i].idx[2]);
    }
}

/* --- Write binary STL --- */
static inline int ts_mesh_write_stl(const ts_mesh *m, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    /* 80-byte header */
    char header[80];
    memset(header, 0, sizeof(header));
    snprintf(header, sizeof(header), "Trinity Site STL");
    fwrite(header, 1, 80, fp);

    /* Triangle count */
    unsigned int ntri = (unsigned int)m->tri_count;
    fwrite(&ntri, sizeof(ntri), 1, fp);

    /* Per-triangle: normal(3xfloat), v0(3xfloat), v1(3xfloat), v2(3xfloat), attr(uint16) */
    for (int i = 0; i < m->tri_count; i++) {
        int a = m->tris[i].idx[0];
        int b = m->tris[i].idx[1];
        int c = m->tris[i].idx[2];

        /* Compute face normal */
        double e1[3], e2[3], fn[3];
        for (int j = 0; j < 3; j++) {
            e1[j] = m->verts[b].pos[j] - m->verts[a].pos[j];
            e2[j] = m->verts[c].pos[j] - m->verts[a].pos[j];
        }
        fn[0] = e1[1]*e2[2] - e1[2]*e2[1];
        fn[1] = e1[2]*e2[0] - e1[0]*e2[2];
        fn[2] = e1[0]*e2[1] - e1[1]*e2[0];
        double len = sqrt(fn[0]*fn[0] + fn[1]*fn[1] + fn[2]*fn[2]);
        if (len > 1e-15) { fn[0] /= len; fn[1] /= len; fn[2] /= len; }

        float tri_data[12];
        tri_data[0]  = (float)fn[0]; tri_data[1]  = (float)fn[1]; tri_data[2]  = (float)fn[2];
        tri_data[3]  = (float)m->verts[a].pos[0]; tri_data[4]  = (float)m->verts[a].pos[1]; tri_data[5]  = (float)m->verts[a].pos[2];
        tri_data[6]  = (float)m->verts[b].pos[0]; tri_data[7]  = (float)m->verts[b].pos[1]; tri_data[8]  = (float)m->verts[b].pos[2];
        tri_data[9]  = (float)m->verts[c].pos[0]; tri_data[10] = (float)m->verts[c].pos[1]; tri_data[11] = (float)m->verts[c].pos[2];
        fwrite(tri_data, sizeof(float), 12, fp);

        unsigned short attr = 0;
        fwrite(&attr, sizeof(attr), 1, fp);
    }

    fclose(fp);
    return 0;
}

#endif /* TS_MESH_H */
