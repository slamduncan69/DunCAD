/*
 * ts_geo.h — Geometry generation (2D + 3D primitives)
 *
 * Generates triangle meshes for OpenSCAD primitive shapes.
 * Each generator writes into a ts_mesh. All vertex generation
 * is per-vertex parallel. Indexing is sequential but trivial.
 *
 * OpenSCAD equivalents:
 *   cube(size) / cube([x,y,z])     — ts_gen_cube
 *   sphere(r)                       — ts_gen_sphere
 *   cylinder(h, r1, r2)            — ts_gen_cylinder
 *   circle(r)                       — ts_gen_circle_points
 *   square(size)                    — ts_gen_square_points
 *
 * GPU strategy:
 *   - Vertex positions: parallel (each vertex independent)
 *   - Normals: parallel per-face, reduction per-vertex
 *   - Index generation: sequential but O(n) trivial pattern
 */
#ifndef TS_GEO_H
#define TS_GEO_H

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- Cube --- */
/* OpenSCAD: cube([sx, sy, sz], center=true/false)
 * center=true: centered at origin
 * center=false: corner at origin (default)
 * We always generate centered, caller translates if needed.
 *
 * GPU: 8 vertices, 12 triangles. So small it's not worth GPU.
 * But the same pattern scales to subdivided cubes.
 */
static inline int ts_gen_cube(double sx, double sy, double sz, ts_mesh *m) {
    double hx = sx * 0.5, hy = sy * 0.5, hz = sz * 0.5;

    ts_mesh_reserve(m, m->vert_count + 24, m->tri_count + 12);

    /* 6 faces, 4 verts each (separate normals per face) = 24 verts */
    /* Front (+Z) */
    int v = m->vert_count;
    ts_mesh_add_vertex(m, -hx,-hy, hz,  0, 0, 1);
    ts_mesh_add_vertex(m,  hx,-hy, hz,  0, 0, 1);
    ts_mesh_add_vertex(m,  hx, hy, hz,  0, 0, 1);
    ts_mesh_add_vertex(m, -hx, hy, hz,  0, 0, 1);
    ts_mesh_add_triangle(m, v+0, v+1, v+2);
    ts_mesh_add_triangle(m, v+0, v+2, v+3);

    /* Back (-Z) */
    v = m->vert_count;
    ts_mesh_add_vertex(m,  hx,-hy,-hz,  0, 0,-1);
    ts_mesh_add_vertex(m, -hx,-hy,-hz,  0, 0,-1);
    ts_mesh_add_vertex(m, -hx, hy,-hz,  0, 0,-1);
    ts_mesh_add_vertex(m,  hx, hy,-hz,  0, 0,-1);
    ts_mesh_add_triangle(m, v+0, v+1, v+2);
    ts_mesh_add_triangle(m, v+0, v+2, v+3);

    /* Right (+X) */
    v = m->vert_count;
    ts_mesh_add_vertex(m,  hx,-hy, hz,  1, 0, 0);
    ts_mesh_add_vertex(m,  hx,-hy,-hz,  1, 0, 0);
    ts_mesh_add_vertex(m,  hx, hy,-hz,  1, 0, 0);
    ts_mesh_add_vertex(m,  hx, hy, hz,  1, 0, 0);
    ts_mesh_add_triangle(m, v+0, v+1, v+2);
    ts_mesh_add_triangle(m, v+0, v+2, v+3);

    /* Left (-X) */
    v = m->vert_count;
    ts_mesh_add_vertex(m, -hx,-hy,-hz, -1, 0, 0);
    ts_mesh_add_vertex(m, -hx,-hy, hz, -1, 0, 0);
    ts_mesh_add_vertex(m, -hx, hy, hz, -1, 0, 0);
    ts_mesh_add_vertex(m, -hx, hy,-hz, -1, 0, 0);
    ts_mesh_add_triangle(m, v+0, v+1, v+2);
    ts_mesh_add_triangle(m, v+0, v+2, v+3);

    /* Top (+Y) */
    v = m->vert_count;
    ts_mesh_add_vertex(m, -hx, hy, hz,  0, 1, 0);
    ts_mesh_add_vertex(m,  hx, hy, hz,  0, 1, 0);
    ts_mesh_add_vertex(m,  hx, hy,-hz,  0, 1, 0);
    ts_mesh_add_vertex(m, -hx, hy,-hz,  0, 1, 0);
    ts_mesh_add_triangle(m, v+0, v+1, v+2);
    ts_mesh_add_triangle(m, v+0, v+2, v+3);

    /* Bottom (-Y) */
    v = m->vert_count;
    ts_mesh_add_vertex(m, -hx,-hy,-hz,  0,-1, 0);
    ts_mesh_add_vertex(m,  hx,-hy,-hz,  0,-1, 0);
    ts_mesh_add_vertex(m,  hx,-hy, hz,  0,-1, 0);
    ts_mesh_add_vertex(m, -hx,-hy, hz,  0,-1, 0);
    ts_mesh_add_triangle(m, v+0, v+1, v+2);
    ts_mesh_add_triangle(m, v+0, v+2, v+3);

    return 0;
}

/* --- Sphere --- */
/* UV sphere: fn segments around, fn/2 rings.
 * GPU: each vertex position is independent (sin/cos of angles).
 * This is the bread and butter of GPU mesh generation.
 *
 * OpenSCAD: sphere(r, $fn=N)
 */
static inline int ts_gen_sphere(double radius, int fn, ts_mesh *m) {
    if (fn < 4) fn = 4;
    int rings = fn / 2;
    int sectors = fn;

    int num_verts = (rings + 1) * (sectors + 1);
    int num_tris  = rings * sectors * 2;
    ts_mesh_reserve(m, m->vert_count + num_verts, m->tri_count + num_tris);

    int base = m->vert_count;

    /* Generate vertices — each is independent (parallel) */
    for (int r = 0; r <= rings; r++) {
        double phi = M_PI * (double)r / (double)rings;  /* 0 to PI */
        double sp = sin(phi), cp = cos(phi);

        for (int s = 0; s <= sectors; s++) {
            double theta = 2.0 * M_PI * (double)s / (double)sectors;
            double st = sin(theta), ct = cos(theta);

            double nx = sp * ct;
            double ny = cp;
            double nz = sp * st;

            ts_mesh_add_vertex(m,
                radius * nx, radius * ny, radius * nz,
                nx, ny, nz);
        }
    }

    /* Generate indices — sequential but trivial pattern */
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < sectors; s++) {
            int a = base + r * (sectors + 1) + s;
            int b = a + sectors + 1;

            ts_mesh_add_triangle(m, a, b, a + 1);
            ts_mesh_add_triangle(m, a + 1, b, b + 1);
        }
    }

    return 0;
}

/* --- Cylinder --- */
/* OpenSCAD: cylinder(h, r1, r2, $fn=N)
 * r1 = bottom radius, r2 = top radius
 * If r1 == r2, it's a regular cylinder. If r2 == 0, it's a cone.
 *
 * GPU: vertex generation per-ring parallel.
 */
static inline int ts_gen_cylinder(double h, double r1, double r2,
                                  int fn, ts_mesh *m) {
    if (fn < 3) fn = 3;

    /* Vertices: bottom ring + top ring + 2 center points */
    int num_verts = fn * 2 + 2;
    int num_tris  = fn * 4;  /* side quads (2 tri each) + top cap + bottom cap */
    ts_mesh_reserve(m, m->vert_count + num_verts, m->tri_count + num_tris);

    int base = m->vert_count;
    double half_h = h * 0.5;

    /* Bottom center */
    int bot_center = ts_mesh_add_vertex(m, 0, 0, -half_h, 0, 0, -1);
    /* Top center */
    int top_center = ts_mesh_add_vertex(m, 0, 0, half_h, 0, 0, 1);

    /* Bottom ring + Top ring */
    int bot_ring = m->vert_count;
    for (int i = 0; i < fn; i++) {
        double angle = 2.0 * M_PI * (double)i / (double)fn;
        double ca = cos(angle), sa = sin(angle);

        /* Side normal: account for cone slope */
        double slope = (r1 - r2) / h;
        double nlen = sqrt(1.0 + slope * slope);
        double nr = 1.0 / nlen;
        double nz_side = slope / nlen;

        /* Bottom vertex (with side normal for side faces) */
        ts_mesh_add_vertex(m, r1*ca, r1*sa, -half_h, ca*nr, sa*nr, nz_side);
    }

    int top_ring = m->vert_count;
    for (int i = 0; i < fn; i++) {
        double angle = 2.0 * M_PI * (double)i / (double)fn;
        double ca = cos(angle), sa = sin(angle);

        double slope = (r1 - r2) / h;
        double nlen = sqrt(1.0 + slope * slope);
        double nr = 1.0 / nlen;
        double nz_side = slope / nlen;

        /* Top vertex */
        ts_mesh_add_vertex(m, r2*ca, r2*sa, half_h, ca*nr, sa*nr, nz_side);
    }

    (void)base;

    /* Side faces */
    for (int i = 0; i < fn; i++) {
        int next = (i + 1) % fn;
        int b0 = bot_ring + i, b1 = bot_ring + next;
        int t0 = top_ring + i, t1 = top_ring + next;
        ts_mesh_add_triangle(m, b0, b1, t1);
        ts_mesh_add_triangle(m, b0, t1, t0);
    }

    /* Bottom cap (fan from center) */
    for (int i = 0; i < fn; i++) {
        int next = (i + 1) % fn;
        ts_mesh_add_triangle(m, bot_center, bot_ring + next, bot_ring + i);
    }

    /* Top cap (fan from center) */
    for (int i = 0; i < fn; i++) {
        int next = (i + 1) % fn;
        ts_mesh_add_triangle(m, top_center, top_ring + i, top_ring + next);
    }

    return 0;
}

/* ================================================================
 * Platonic Solids
 *
 * The 5 Platonic solids: tetrahedron, cube (already above),
 * octahedron, dodecahedron, icosahedron.
 * All centered at origin, circumscribed by sphere of given radius.
 * ================================================================ */

/* --- Tetrahedron --- */
/* 4 vertices, 4 triangular faces. Circumradius = r. */
static inline int ts_gen_tetrahedron(double r, ts_mesh *m) {
    ts_mesh_reserve(m, m->vert_count + 12, m->tri_count + 4);

    /* Vertices of a regular tetrahedron inscribed in sphere of radius r.
     * Place one vertex at top (+Z), three below equally spaced. */
    double a = r * (2.0 * sqrt(2.0) / 3.0); /* edge from circumradius */
    (void)a;
    /* Direct coordinates: */
    double v0z = r;
    double v1z = -r / 3.0;
    double v1r = r * 2.0 * sqrt(2.0) / 3.0; /* horizontal radius of bottom triangle */

    double verts[4][3] = {
        { 0,          0,          v0z },
        { v1r,        0,          v1z },
        { -v1r * 0.5, v1r * sqrt(3.0) / 2.0, v1z },
        { -v1r * 0.5, -v1r * sqrt(3.0) / 2.0, v1z }
    };

    /* Each face gets its own vertices with face normals */
    int faces[4][3] = { {0,1,2}, {0,2,3}, {0,3,1}, {1,3,2} };

    for (int f = 0; f < 4; f++) {
        int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2];
        /* Compute face normal */
        double e1x = verts[i1][0]-verts[i0][0], e1y = verts[i1][1]-verts[i0][1], e1z = verts[i1][2]-verts[i0][2];
        double e2x = verts[i2][0]-verts[i0][0], e2y = verts[i2][1]-verts[i0][1], e2z = verts[i2][2]-verts[i0][2];
        double nx = e1y*e2z - e1z*e2y, ny = e1z*e2x - e1x*e2z, nz = e1x*e2y - e1y*e2x;
        double nl = sqrt(nx*nx + ny*ny + nz*nz);
        if (nl > 1e-12) { nx /= nl; ny /= nl; nz /= nl; }

        int v = m->vert_count;
        ts_mesh_add_vertex(m, verts[i0][0], verts[i0][1], verts[i0][2], nx, ny, nz);
        ts_mesh_add_vertex(m, verts[i1][0], verts[i1][1], verts[i1][2], nx, ny, nz);
        ts_mesh_add_vertex(m, verts[i2][0], verts[i2][1], verts[i2][2], nx, ny, nz);
        ts_mesh_add_triangle(m, v, v+1, v+2);
    }
    return 0;
}

/* --- Octahedron --- */
/* 6 vertices, 8 triangular faces. Circumradius = r. */
static inline int ts_gen_octahedron(double r, ts_mesh *m) {
    ts_mesh_reserve(m, m->vert_count + 24, m->tri_count + 8);

    double verts[6][3] = {
        { r, 0, 0}, {-r, 0, 0}, {0, r, 0}, {0,-r, 0}, {0, 0, r}, {0, 0,-r}
    };
    int faces[8][3] = {
        {4,0,2}, {4,2,1}, {4,1,3}, {4,3,0},
        {5,2,0}, {5,1,2}, {5,3,1}, {5,0,3}
    };

    for (int f = 0; f < 8; f++) {
        int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2];
        double e1x = verts[i1][0]-verts[i0][0], e1y = verts[i1][1]-verts[i0][1], e1z = verts[i1][2]-verts[i0][2];
        double e2x = verts[i2][0]-verts[i0][0], e2y = verts[i2][1]-verts[i0][1], e2z = verts[i2][2]-verts[i0][2];
        double nx = e1y*e2z - e1z*e2y, ny = e1z*e2x - e1x*e2z, nz = e1x*e2y - e1y*e2x;
        double nl = sqrt(nx*nx + ny*ny + nz*nz);
        if (nl > 1e-12) { nx /= nl; ny /= nl; nz /= nl; }

        int v = m->vert_count;
        ts_mesh_add_vertex(m, verts[i0][0], verts[i0][1], verts[i0][2], nx, ny, nz);
        ts_mesh_add_vertex(m, verts[i1][0], verts[i1][1], verts[i1][2], nx, ny, nz);
        ts_mesh_add_vertex(m, verts[i2][0], verts[i2][1], verts[i2][2], nx, ny, nz);
        ts_mesh_add_triangle(m, v, v+1, v+2);
    }
    return 0;
}

/* --- Dodecahedron --- */
/* 20 vertices, 12 pentagonal faces = 36 triangles. Circumradius = r.
 * The sacred shape. Each face is a regular pentagon, triangulated as a fan. */
static inline int ts_gen_dodecahedron(double r, ts_mesh *m) {
    ts_mesh_reserve(m, m->vert_count + 108, m->tri_count + 36);

    /* Golden ratio */
    double phi = (1.0 + sqrt(5.0)) / 2.0;
    double iphi = 1.0 / phi;

    /* 20 vertices of a dodecahedron inscribed in a sphere.
     * Unscaled circumradius = sqrt(3). Scale by r/sqrt(3). */
    double s = r / sqrt(3.0);

    double verts[20][3] = {
        /* 8 cube vertices (+-1, +-1, +-1) */
        { s, s, s}, { s, s,-s}, { s,-s, s}, { s,-s,-s},
        {-s, s, s}, {-s, s,-s}, {-s,-s, s}, {-s,-s,-s},
        /* 4 on YZ plane: (0, +-phi, +-1/phi) */
        {0, s*phi, s*iphi}, {0, s*phi,-s*iphi},
        {0,-s*phi, s*iphi}, {0,-s*phi,-s*iphi},
        /* 4 on XZ plane: (+-1/phi, 0, +-phi) */
        { s*iphi, 0, s*phi}, {-s*iphi, 0, s*phi},
        { s*iphi, 0,-s*phi}, {-s*iphi, 0,-s*phi},
        /* 4 on XY plane: (+-phi, +-1/phi, 0) */
        { s*phi, s*iphi, 0}, { s*phi,-s*iphi, 0},
        {-s*phi, s*iphi, 0}, {-s*phi,-s*iphi, 0}
    };

    /* 12 pentagonal faces (vertex indices, CCW from outside) */
    int faces[12][5] = {
        { 0, 12, 13,  4,  8},
        { 0,  8,  9,  1, 16},
        { 0, 16, 17,  2, 12},
        { 1,  9,  5, 15, 14},
        { 1, 14,  3, 17, 16},
        { 2, 17,  3, 11, 10},
        { 2, 10,  6, 13, 12},
        { 4, 13,  6, 19, 18},
        { 4, 18,  5,  9,  8},
        { 7, 11,  3, 14, 15},
        { 7, 15,  5, 18, 19},
        { 7, 19,  6, 10, 11}
    };

    for (int f = 0; f < 12; f++) {
        /* Compute face normal from first 3 vertices */
        int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2];
        double e1x = verts[i1][0]-verts[i0][0], e1y = verts[i1][1]-verts[i0][1], e1z = verts[i1][2]-verts[i0][2];
        double e2x = verts[i2][0]-verts[i0][0], e2y = verts[i2][1]-verts[i0][1], e2z = verts[i2][2]-verts[i0][2];
        double nx = e1y*e2z - e1z*e2y, ny = e1z*e2x - e1x*e2z, nz = e1x*e2y - e1y*e2x;
        double nl = sqrt(nx*nx + ny*ny + nz*nz);
        if (nl > 1e-12) { nx /= nl; ny /= nl; nz /= nl; }

        /* Triangulate pentagon as fan from vertex 0 */
        int base = m->vert_count;
        for (int k = 0; k < 5; k++) {
            int vi = faces[f][k];
            ts_mesh_add_vertex(m, verts[vi][0], verts[vi][1], verts[vi][2], nx, ny, nz);
        }
        for (int k = 1; k < 4; k++)
            ts_mesh_add_triangle(m, base, base + k, base + k + 1);
    }
    return 0;
}

/* --- Icosahedron --- */
/* 12 vertices, 20 triangular faces. Circumradius = r. */
static inline int ts_gen_icosahedron(double r, ts_mesh *m) {
    ts_mesh_reserve(m, m->vert_count + 60, m->tri_count + 20);

    /* Golden ratio */
    double phi = (1.0 + sqrt(5.0)) / 2.0;

    /* 12 vertices: unscaled circumradius = sqrt(1 + phi^2). Scale to r. */
    double s = r / sqrt(1.0 + phi * phi);

    double verts[12][3] = {
        { 0,  s,  s*phi}, { 0,  s, -s*phi}, { 0, -s,  s*phi}, { 0, -s, -s*phi},
        { s,  s*phi, 0}, {-s,  s*phi, 0}, { s, -s*phi, 0}, {-s, -s*phi, 0},
        { s*phi, 0,  s}, { s*phi, 0, -s}, {-s*phi, 0,  s}, {-s*phi, 0, -s}
    };

    int faces[20][3] = {
        {0,2,8}, {0,8,4}, {0,4,5}, {0,5,10}, {0,10,2},
        {2,6,8}, {8,6,9}, {8,9,4}, {4,9,1}, {4,1,5},
        {5,1,11}, {5,11,10}, {10,11,7}, {10,7,2}, {2,7,6},
        {3,6,7}, {3,7,11}, {3,11,1}, {3,1,9}, {3,9,6}
    };

    for (int f = 0; f < 20; f++) {
        int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2];
        double e1x = verts[i1][0]-verts[i0][0], e1y = verts[i1][1]-verts[i0][1], e1z = verts[i1][2]-verts[i0][2];
        double e2x = verts[i2][0]-verts[i0][0], e2y = verts[i2][1]-verts[i0][1], e2z = verts[i2][2]-verts[i0][2];
        double nx = e1y*e2z - e1z*e2y, ny = e1z*e2x - e1x*e2z, nz = e1x*e2y - e1y*e2x;
        double nl = sqrt(nx*nx + ny*ny + nz*nz);
        if (nl > 1e-12) { nx /= nl; ny /= nl; nz /= nl; }

        int v = m->vert_count;
        ts_mesh_add_vertex(m, verts[i0][0], verts[i0][1], verts[i0][2], nx, ny, nz);
        ts_mesh_add_vertex(m, verts[i1][0], verts[i1][1], verts[i1][2], nx, ny, nz);
        ts_mesh_add_vertex(m, verts[i2][0], verts[i2][1], verts[i2][2], nx, ny, nz);
        ts_mesh_add_triangle(m, v, v+1, v+2);
    }
    return 0;
}

/* --- Circle (2D point generation) --- */
/* OpenSCAD: circle(r, $fn=N)
 * Returns array of 2D points (stored as vec3 with z=0).
 * GPU: each point is independent (cos/sin of angle).
 *
 * Caller provides points array of size >= fn.
 * Returns number of points written.
 */
static inline int ts_gen_circle_points(double radius, int fn,
                                       double *points_xy, int max_points) {
    if (fn < 3) fn = 3;
    if (fn > max_points) fn = max_points;

    for (int i = 0; i < fn; i++) {
        double angle = 2.0 * M_PI * (double)i / (double)fn;
        points_xy[i*2 + 0] = radius * cos(angle);
        points_xy[i*2 + 1] = radius * sin(angle);
    }
    return fn;
}

/* --- Square (2D point generation) --- */
/* OpenSCAD: square([sx, sy], center=true)
 * 4 corner points, centered at origin.
 */
static inline int ts_gen_square_points(double sx, double sy,
                                       double *points_xy, int max_points) {
    if (max_points < 4) return 0;
    double hx = sx * 0.5, hy = sy * 0.5;

    points_xy[0] = -hx; points_xy[1] = -hy;
    points_xy[2] =  hx; points_xy[3] = -hy;
    points_xy[4] =  hx; points_xy[5] =  hy;
    points_xy[6] = -hx; points_xy[7] =  hy;
    return 4;
}

/* --- Polyhedron (user-defined mesh) --- */
/* OpenSCAD: polyhedron(points, faces)
 * This just validates and copies user data into our mesh format.
 * points: array of [x,y,z] triples (n_points * 3 doubles)
 * faces: array of triangle index triples (n_faces * 3 ints)
 */
static inline int ts_gen_polyhedron(const double *points, int n_points,
                                    const int *faces, int n_faces,
                                    ts_mesh *m) {
    ts_mesh_reserve(m, m->vert_count + n_points, m->tri_count + n_faces);

    int base = m->vert_count;
    for (int i = 0; i < n_points; i++) {
        ts_mesh_add_vertex(m,
            points[i*3+0], points[i*3+1], points[i*3+2],
            0, 0, 0);  /* normals computed after */
    }

    for (int i = 0; i < n_faces; i++) {
        int a = faces[i*3+0], b = faces[i*3+1], c = faces[i*3+2];
        if (a < 0 || a >= n_points || b < 0 || b >= n_points ||
            c < 0 || c >= n_points)
            return -1;  /* invalid index */
        ts_mesh_add_triangle(m, base + a, base + b, base + c);
    }

    ts_mesh_compute_normals(m);
    return 0;
}

/* ================================================================
 * SURFACE (heightmap)
 *
 * Generates a solid mesh from a 2D grid of heights.
 * Grid has cols × rows values. Each cell becomes 2 triangles on top.
 * Bottom face is a flat plane at z=0. Side walls close the solid.
 * center=1 shifts origin to center of XY extent.
 * ================================================================ */
static inline int ts_gen_surface(const double *heights, int cols, int rows,
                                  int center, ts_mesh *m) {
    if (cols < 2 || rows < 2 || !heights) return -1;

    /* Allocate: top grid + bottom grid + side walls */
    int top_verts = cols * rows;
    int top_tris = (cols - 1) * (rows - 1) * 2;
    int bot_tris = (cols - 1) * (rows - 1) * 2;
    int side_tris = 2 * ((cols - 1) + (rows - 1)) * 2;
    ts_mesh_reserve(m, m->vert_count + top_verts * 2 + (cols + rows) * 4,
                    m->tri_count + top_tris + bot_tris + side_tris);

    double ox = center ? -(cols - 1) * 0.5 : 0;
    double oy = center ? -(rows - 1) * 0.5 : 0;

    /* Top surface vertices */
    int base_top = m->vert_count;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            ts_mesh_add_vertex(m, ox + c, oy + r, heights[r * cols + c],
                               0, 0, 1);

    /* Top surface triangles */
    for (int r = 0; r < rows - 1; r++) {
        for (int c = 0; c < cols - 1; c++) {
            int i00 = base_top + r * cols + c;
            int i10 = i00 + 1;
            int i01 = i00 + cols;
            int i11 = i01 + 1;
            ts_mesh_add_triangle(m, i00, i10, i11);
            ts_mesh_add_triangle(m, i00, i11, i01);
        }
    }

    /* Bottom face at z=0 */
    int base_bot = m->vert_count;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            ts_mesh_add_vertex(m, ox + c, oy + r, 0, 0, 0, -1);

    for (int r = 0; r < rows - 1; r++) {
        for (int c = 0; c < cols - 1; c++) {
            int i00 = base_bot + r * cols + c;
            int i10 = i00 + 1;
            int i01 = i00 + cols;
            int i11 = i01 + 1;
            ts_mesh_add_triangle(m, i00, i11, i10);
            ts_mesh_add_triangle(m, i00, i01, i11);
        }
    }

    /* Side walls: connect top edge to bottom at z=0 */
    /* Front edge (r=0) */
    for (int c = 0; c < cols - 1; c++) {
        int t0 = base_top + c, t1 = base_top + c + 1;
        int b0 = base_bot + c, b1 = base_bot + c + 1;
        ts_mesh_add_triangle(m, b0, b1, t1);
        ts_mesh_add_triangle(m, b0, t1, t0);
    }
    /* Back edge (r=rows-1) */
    for (int c = 0; c < cols - 1; c++) {
        int t0 = base_top + (rows-1)*cols + c;
        int t1 = t0 + 1;
        int b0 = base_bot + (rows-1)*cols + c;
        int b1 = b0 + 1;
        ts_mesh_add_triangle(m, t0, t1, b1);
        ts_mesh_add_triangle(m, t0, b1, b0);
    }
    /* Left edge (c=0) */
    for (int r = 0; r < rows - 1; r++) {
        int t0 = base_top + r*cols, t1 = base_top + (r+1)*cols;
        int b0 = base_bot + r*cols, b1 = base_bot + (r+1)*cols;
        ts_mesh_add_triangle(m, t0, t1, b1);
        ts_mesh_add_triangle(m, t0, b1, b0);
    }
    /* Right edge (c=cols-1) */
    for (int c2 = cols-1, r = 0; r < rows - 1; r++) {
        int t0 = base_top + r*cols + c2, t1 = base_top + (r+1)*cols + c2;
        int b0 = base_bot + r*cols + c2, b1 = base_bot + (r+1)*cols + c2;
        ts_mesh_add_triangle(m, b0, b1, t1);
        ts_mesh_add_triangle(m, b0, t1, t0);
    }

    ts_mesh_compute_normals(m);
    return 0;
}

/* Parse a .dat heightmap file (space/tab-separated numbers, # comments).
 * Returns heap-allocated array, sets *out_cols and *out_rows. */
static inline double *ts_parse_dat(const char *path, int *out_cols, int *out_rows) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    int cap = 256, count = 0, cols = 0, rows = 0, cur_col = 0;
    double *data = (double *)malloc((size_t)cap * sizeof(double));
    char line[8192];

    while (fgets(line, (int)sizeof(line), fp)) {
        /* Skip comments */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        cur_col = 0;
        while (*p) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\n' || *p == '\r' || *p == '\0') break;
            char *end;
            double val = strtod(p, &end);
            if (end == p) break;
            p = end;
            if (count >= cap) {
                cap *= 2;
                data = (double *)realloc(data, (size_t)cap * sizeof(double));
            }
            data[count++] = val;
            cur_col++;
        }
        if (cur_col > 0) {
            if (cols == 0) cols = cur_col;
            rows++;
        }
    }
    fclose(fp);

    if (cols < 2 || rows < 2) { free(data); return NULL; }
    *out_cols = cols;
    *out_rows = rows;
    return data;
}

#endif /* TS_GEO_H */
