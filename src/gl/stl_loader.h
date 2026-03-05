#ifndef DC_STL_LOADER_H
#define DC_STL_LOADER_H

/*
 * stl_loader.h — Load STL files (binary + ASCII) into float arrays.
 *
 * No OpenGL or GTK dependency — pure C with libc only.
 *
 * Loaded mesh is interleaved: [nx,ny,nz, vx,vy,vz] per vertex,
 * 3 vertices per triangle, so stride = 6 floats per vertex.
 */

#include <stddef.h>

typedef struct {
    float *data;          /* interleaved [nx,ny,nz, vx,vy,vz, ...] */
    int    num_triangles;
    int    num_vertices;  /* = num_triangles * 3 */

    /* Bounding box */
    float  min[3];
    float  max[3];
    float  center[3];     /* (min+max)/2 */
    float  extent;        /* max dimension */
} DC_StlMesh;

/* Load an STL file. Returns NULL on failure. Caller must free with dc_stl_free(). */
DC_StlMesh *dc_stl_load(const char *path);

/* Free a loaded mesh. Safe with NULL. */
void dc_stl_free(DC_StlMesh *mesh);

#endif /* DC_STL_LOADER_H */
