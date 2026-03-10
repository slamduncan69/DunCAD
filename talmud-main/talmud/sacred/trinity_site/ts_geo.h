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
