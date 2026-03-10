/*
 * ts_opencl.h — GPU acceleration via OpenCL
 *
 * Provides batch operations that offload to GPU when available.
 * Single-element operations stay on CPU (transfer overhead not worth it).
 * The GPU wins when processing ARRAYS of data in parallel.
 *
 * Architecture:
 *   ts_gpu_ctx  — OpenCL context (platform, device, queue, program)
 *   ts_gpu_*    — Batch operations (vec3 arrays, mat4 transforms, mesh ops)
 *
 * Fallback: if OpenCL init fails, all batch ops fall back to CPU loops.
 * This means ts_opencl.h is ALWAYS safe to include — no hard dependency.
 *
 * Build: link with -lOpenCL (or skip if not available)
 *
 * Kernel strategy:
 *   - Kernels are embedded as string literals (no external .cl files)
 *   - Program compiled once at context init
 *   - Buffers created per-call (future: persistent buffer pool)
 *
 * Target hardware: NVIDIA RTX 3080 Ti (CUDA/OpenCL 1.2+)
 * Double precision: required (fp64). Most NVIDIA GPUs support this.
 */
#ifndef TS_OPENCL_H
#define TS_OPENCL_H

#ifdef TS_NO_OPENCL
/* Stub mode: no GPU, all operations fall back to CPU */
#define TS_GPU_AVAILABLE 0
#else
#define TS_GPU_AVAILABLE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TS_GPU_AVAILABLE
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#endif

/* ================================================================
 * EMBEDDED OPENCL KERNELS
 *
 * All kernels in one program string. Each kernel processes arrays.
 * GPU: one work-item per element.
 * ================================================================ */

#if TS_GPU_AVAILABLE

/* Part 1: vec3 + mat4 + scalar kernels */
static const char *TS_OPENCL_K1 =
"#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n"
"__kernel void vec3_add(__global const double *a,\n"
"    __global const double *b, __global double *out, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return; int j = i*3;\n"
"    out[j]=a[j]+b[j]; out[j+1]=a[j+1]+b[j+1]; out[j+2]=a[j+2]+b[j+2];\n"
"}\n"
"__kernel void vec3_scale(__global const double *a,\n"
"    __global double *out, double s, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return; int j = i*3;\n"
"    out[j]=a[j]*s; out[j+1]=a[j+1]*s; out[j+2]=a[j+2]*s;\n"
"}\n"
"__kernel void vec3_normalize(__global const double *a,\n"
"    __global double *out, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return; int j = i*3;\n"
"    double x=a[j],y=a[j+1],z=a[j+2];\n"
"    double len=sqrt(x*x+y*y+z*z);\n"
"    double inv=(len>1e-15)?1.0/len:0.0;\n"
"    out[j]=x*inv; out[j+1]=y*inv; out[j+2]=z*inv;\n"
"}\n"
"__kernel void vec3_cross(__global const double *a,\n"
"    __global const double *b, __global double *out, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return; int j = i*3;\n"
"    double ax=a[j],ay=a[j+1],az=a[j+2],bx=b[j],by=b[j+1],bz=b[j+2];\n"
"    out[j]=ay*bz-az*by; out[j+1]=az*bx-ax*bz; out[j+2]=ax*by-ay*bx;\n"
"}\n"
"__kernel void vec3_dot(__global const double *a,\n"
"    __global const double *b, __global double *out, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return; int j = i*3;\n"
"    out[i]=a[j]*b[j]+a[j+1]*b[j+1]+a[j+2]*b[j+2];\n"
"}\n"
"__kernel void mat4_transform_points(__global const double *mat,\n"
"    __global const double *pts, __global double *out, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return; int j = i*3;\n"
"    double x=pts[j],y=pts[j+1],z=pts[j+2];\n"
"    out[j]=mat[0]*x+mat[1]*y+mat[2]*z+mat[3];\n"
"    out[j+1]=mat[4]*x+mat[5]*y+mat[6]*z+mat[7];\n"
"    out[j+2]=mat[8]*x+mat[9]*y+mat[10]*z+mat[11];\n"
"}\n"
"__kernel void scalar_sqrt(__global const double *a,\n"
"    __global double *out, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return; out[i]=sqrt(a[i]);\n"
"}\n"
"__kernel void scalar_sin(__global const double *a,\n"
"    __global double *out, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return;\n"
"    out[i]=sin(a[i]*0.017453292519943295);\n"
"}\n"
"__kernel void scalar_cos(__global const double *a,\n"
"    __global double *out, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return;\n"
"    out[i]=cos(a[i]*0.017453292519943295);\n"
"}\n"
"__kernel void scalar_pow(__global const double *base,\n"
"    __global const double *exp, __global double *out, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return;\n"
"    out[i]=pow(base[i],exp[i]);\n"
"}\n";

/* Part 2: mesh transform + RNG kernels */
static const char *TS_OPENCL_K2 =
"__kernel void mesh_transform(__global const double *mat,\n"
"    __global const double *norm_mat, __global double *verts, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return; int j = i*6;\n"
"    double px=verts[j],py=verts[j+1],pz=verts[j+2];\n"
"    double nx=verts[j+3],ny=verts[j+4],nz=verts[j+5];\n"
"    verts[j]=mat[0]*px+mat[1]*py+mat[2]*pz+mat[3];\n"
"    verts[j+1]=mat[4]*px+mat[5]*py+mat[6]*pz+mat[7];\n"
"    verts[j+2]=mat[8]*px+mat[9]*py+mat[10]*pz+mat[11];\n"
"    verts[j+3]=norm_mat[0]*nx+norm_mat[1]*ny+norm_mat[2]*nz;\n"
"    verts[j+4]=norm_mat[4]*nx+norm_mat[5]*ny+norm_mat[6]*nz;\n"
"    verts[j+5]=norm_mat[8]*nx+norm_mat[9]*ny+norm_mat[10]*nz;\n"
"    double nnx=verts[j+3],nny=verts[j+4],nnz=verts[j+5];\n"
"    double len=sqrt(nnx*nnx+nny*nny+nnz*nnz);\n"
"    double inv=(len>1e-15)?1.0/len:0.0;\n"
"    verts[j+3]*=inv; verts[j+4]*=inv; verts[j+5]*=inv;\n"
"}\n"
"__kernel void rng_uniform(ulong seed, double lo, double hi,\n"
"    __global double *out, int n) {\n"
"    int i = get_global_id(0); if (i >= n) return;\n"
"    ulong z = seed + (ulong)i * 0x9E3779B97F4A7C15UL;\n"
"    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9UL;\n"
"    z = (z ^ (z >> 27)) * 0x94D049BB133111EBUL;\n"
"    z = z ^ (z >> 31);\n"
"    double t = (double)(z >> 11) * (1.0 / 9007199254740992.0);\n"
"    out[i] = lo + t * (hi - lo);\n"
"}\n";

/* Part 3: CSG classification kernel
 * Input: N polygons packed as triangles (9 doubles each: 3 verts × xyz)
 *        1 plane (4 doubles: nx, ny, nz, w)
 *        epsilon for coplanar threshold
 * Output: N ints — classification per polygon
 *   0=COPLANAR, 1=FRONT, 2=BACK, 3=SPANNING
 *
 * One work-item per polygon. Each classifies its 3 vertices against
 * the plane, then ORs the per-vertex types.
 */
static const char *TS_OPENCL_K3 =
"#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n"
"__kernel void csg_classify_tris(\n"
"    __global const double *polys,\n"    /* 9 doubles per tri: v0xyz v1xyz v2xyz */
"    __global const double *plane,\n"    /* 4 doubles: nx ny nz w */
"    double eps,\n"
"    __global int *out,\n"              /* N ints: classification */
"    int n) {\n"
"    int i = get_global_id(0); if (i >= n) return;\n"
"    int j = i * 9;\n"
"    double nx=plane[0], ny=plane[1], nz=plane[2], w=plane[3];\n"
"    int type = 0;\n"
"    for (int k = 0; k < 3; k++) {\n"
"        double d = nx*polys[j+k*3] + ny*polys[j+k*3+1] + nz*polys[j+k*3+2] - w;\n"
"        int vtype = (d < -eps) ? 2 : ((d > eps) ? 1 : 0);\n"
"        type |= vtype;\n"
"    }\n"
"    out[i] = type;\n"
"}\n";

#endif /* TS_GPU_AVAILABLE */

/* ================================================================
 * GPU CONTEXT
 * ================================================================ */

typedef struct {
#if TS_GPU_AVAILABLE
    cl_platform_id   platform;
    cl_device_id     device;
    cl_context       context;
    cl_command_queue  queue;
    cl_program       program;
    /* Cached kernels */
    cl_kernel k_vec3_add;
    cl_kernel k_vec3_scale;
    cl_kernel k_vec3_normalize;
    cl_kernel k_vec3_cross;
    cl_kernel k_vec3_dot;
    cl_kernel k_mat4_transform_points;
    cl_kernel k_scalar_sqrt;
    cl_kernel k_scalar_sin;
    cl_kernel k_scalar_cos;
    cl_kernel k_scalar_pow;
    cl_kernel k_mesh_transform;
    cl_kernel k_rng_uniform;
    cl_kernel k_csg_classify_tris;
#endif
    int active;         /* 1 if GPU is available and initialized */
    char device_name[256];
    size_t max_work_group;
    unsigned long global_mem_mb;
    int has_fp64;
} ts_gpu_ctx;

/* ================================================================
 * INIT / SHUTDOWN
 * ================================================================ */

static inline ts_gpu_ctx ts_gpu_init(void) {
    ts_gpu_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));

#if TS_GPU_AVAILABLE
    cl_int err;

    /* Find first GPU platform and device */
    cl_uint num_platforms = 0;
    err = clGetPlatformIDs(0, NULL, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) return ctx;

    cl_platform_id *platforms = (cl_platform_id *)malloc(
        num_platforms * sizeof(cl_platform_id));
    if (!platforms) return ctx;
    clGetPlatformIDs(num_platforms, platforms, NULL);

    cl_device_id device = 0;
    cl_platform_id platform = 0;
    for (cl_uint i = 0; i < num_platforms; i++) {
        err = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 1, &device, NULL);
        if (err == CL_SUCCESS) {
            platform = platforms[i];
            break;
        }
    }
    free(platforms);
    if (!device) return ctx;

    /* Check fp64 support */
    cl_device_fp_config fp64 = 0;
    clGetDeviceInfo(device, CL_DEVICE_DOUBLE_FP_CONFIG, sizeof(fp64), &fp64, NULL);
    if (!fp64) {
        fprintf(stderr, "ts_gpu: device lacks fp64 support, falling back to CPU\n");
        return ctx;
    }

    /* Get device info */
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(ctx.device_name),
                    ctx.device_name, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE,
                    sizeof(ctx.max_work_group), &ctx.max_work_group, NULL);
    cl_ulong gmem = 0;
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(gmem), &gmem, NULL);
    ctx.global_mem_mb = (unsigned long)(gmem / (1024 * 1024));
    ctx.has_fp64 = 1;

    /* Create context and command queue */
    ctx.context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (err != CL_SUCCESS) return ctx;

    ctx.queue = clCreateCommandQueue(ctx.context, device, 0, &err);
    if (err != CL_SUCCESS) {
        clReleaseContext(ctx.context);
        ctx.context = 0;
        return ctx;
    }

    /* Compile kernel program (three source strings concatenated) */
    const char *srcs[3] = { TS_OPENCL_K1, TS_OPENCL_K2, TS_OPENCL_K3 };
    size_t src_lens[3] = { strlen(TS_OPENCL_K1), strlen(TS_OPENCL_K2), strlen(TS_OPENCL_K3) };
    ctx.program = clCreateProgramWithSource(ctx.context, 3, srcs, src_lens, &err);
    if (err != CL_SUCCESS) {
        clReleaseCommandQueue(ctx.queue);
        clReleaseContext(ctx.context);
        ctx.queue = 0; ctx.context = 0;
        return ctx;
    }

    err = clBuildProgram(ctx.program, 1, &device, "-cl-std=CL1.2", NULL, NULL);
    if (err != CL_SUCCESS) {
        /* Get build log */
        size_t log_size = 0;
        clGetProgramBuildInfo(ctx.program, device, CL_PROGRAM_BUILD_LOG,
                              0, NULL, &log_size);
        if (log_size > 1) {
            char *log = (char *)malloc(log_size);
            if (log) {
                clGetProgramBuildInfo(ctx.program, device, CL_PROGRAM_BUILD_LOG,
                                      log_size, log, NULL);
                fprintf(stderr, "ts_gpu: kernel build failed:\n%s\n", log);
                free(log);
            }
        }
        clReleaseProgram(ctx.program);
        clReleaseCommandQueue(ctx.queue);
        clReleaseContext(ctx.context);
        ctx.program = 0; ctx.queue = 0; ctx.context = 0;
        return ctx;
    }

    /* Cache kernel objects */
    ctx.k_vec3_add = clCreateKernel(ctx.program, "vec3_add", NULL);
    ctx.k_vec3_scale = clCreateKernel(ctx.program, "vec3_scale", NULL);
    ctx.k_vec3_normalize = clCreateKernel(ctx.program, "vec3_normalize", NULL);
    ctx.k_vec3_cross = clCreateKernel(ctx.program, "vec3_cross", NULL);
    ctx.k_vec3_dot = clCreateKernel(ctx.program, "vec3_dot", NULL);
    ctx.k_mat4_transform_points = clCreateKernel(ctx.program, "mat4_transform_points", NULL);
    ctx.k_scalar_sqrt = clCreateKernel(ctx.program, "scalar_sqrt", NULL);
    ctx.k_scalar_sin = clCreateKernel(ctx.program, "scalar_sin", NULL);
    ctx.k_scalar_cos = clCreateKernel(ctx.program, "scalar_cos", NULL);
    ctx.k_scalar_pow = clCreateKernel(ctx.program, "scalar_pow", NULL);
    ctx.k_mesh_transform = clCreateKernel(ctx.program, "mesh_transform", NULL);
    ctx.k_rng_uniform = clCreateKernel(ctx.program, "rng_uniform", NULL);
    ctx.k_csg_classify_tris = clCreateKernel(ctx.program, "csg_classify_tris", NULL);

    ctx.platform = platform;
    ctx.device = device;
    ctx.active = 1;
#endif

    return ctx;
}

static inline void ts_gpu_shutdown(ts_gpu_ctx *ctx) {
    if (!ctx->active) return;

#if TS_GPU_AVAILABLE
    if (ctx->k_vec3_add) clReleaseKernel(ctx->k_vec3_add);
    if (ctx->k_vec3_scale) clReleaseKernel(ctx->k_vec3_scale);
    if (ctx->k_vec3_normalize) clReleaseKernel(ctx->k_vec3_normalize);
    if (ctx->k_vec3_cross) clReleaseKernel(ctx->k_vec3_cross);
    if (ctx->k_vec3_dot) clReleaseKernel(ctx->k_vec3_dot);
    if (ctx->k_mat4_transform_points) clReleaseKernel(ctx->k_mat4_transform_points);
    if (ctx->k_scalar_sqrt) clReleaseKernel(ctx->k_scalar_sqrt);
    if (ctx->k_scalar_sin) clReleaseKernel(ctx->k_scalar_sin);
    if (ctx->k_scalar_cos) clReleaseKernel(ctx->k_scalar_cos);
    if (ctx->k_scalar_pow) clReleaseKernel(ctx->k_scalar_pow);
    if (ctx->k_mesh_transform) clReleaseKernel(ctx->k_mesh_transform);
    if (ctx->k_rng_uniform) clReleaseKernel(ctx->k_rng_uniform);
    if (ctx->k_csg_classify_tris) clReleaseKernel(ctx->k_csg_classify_tris);
    if (ctx->program) clReleaseProgram(ctx->program);
    if (ctx->queue) clReleaseCommandQueue(ctx->queue);
    if (ctx->context) clReleaseContext(ctx->context);
#endif

    memset(ctx, 0, sizeof(*ctx));
}

static inline void ts_gpu_print_info(const ts_gpu_ctx *ctx) {
    if (!ctx->active) {
        printf("  GPU: not available (CPU fallback)\n");
        return;
    }
    printf("  GPU: %s\n", ctx->device_name);
    printf("  Memory: %lu MB\n", ctx->global_mem_mb);
    printf("  Max work group: %zu\n", ctx->max_work_group);
    printf("  FP64: %s\n", ctx->has_fp64 ? "yes" : "no");
}

/* ================================================================
 * BATCH OPERATIONS — GPU accelerated with CPU fallback
 *
 * Convention: all batch functions take arrays of doubles.
 * vec3: packed as [x0,y0,z0, x1,y1,z1, ...]
 * mat4: 16 doubles, row-major
 * Returns 0 on success, -1 on error.
 *
 * The GPU is only used if ctx->active AND n >= threshold.
 * Small batches stay on CPU (transfer overhead not worth it).
 * ================================================================ */

/* Minimum batch size to bother with GPU (below this, CPU is faster) */
#define TS_GPU_MIN_BATCH 256

#if TS_GPU_AVAILABLE

/* Internal: run a kernel with 2 input buffers + 1 output buffer */
static inline int ts_gpu_run_2in_1out(const ts_gpu_ctx *ctx,
                                       cl_kernel kernel,
                                       const double *a, const double *b,
                                       double *out,
                                       size_t a_bytes, size_t b_bytes,
                                       size_t out_bytes,
                                       int n, size_t global_size) {
    cl_int err;
    cl_mem buf_a = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   a_bytes, (void *)a, &err);
    if (err != CL_SUCCESS) return -1;
    cl_mem buf_b = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   b_bytes, (void *)b, &err);
    if (err != CL_SUCCESS) { clReleaseMemObject(buf_a); return -1; }
    cl_mem buf_out = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY,
                                     out_bytes, NULL, &err);
    if (err != CL_SUCCESS) { clReleaseMemObject(buf_a); clReleaseMemObject(buf_b); return -1; }

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &buf_a);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &buf_b);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &buf_out);
    clSetKernelArg(kernel, 3, sizeof(int), &n);

    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                  &global_size, NULL, 0, NULL, NULL);
    if (err == CL_SUCCESS) {
        clEnqueueReadBuffer(ctx->queue, buf_out, CL_TRUE, 0,
                            out_bytes, out, 0, NULL, NULL);
    }

    clReleaseMemObject(buf_a);
    clReleaseMemObject(buf_b);
    clReleaseMemObject(buf_out);
    return (err == CL_SUCCESS) ? 0 : -1;
}

/* Internal: run a kernel with 1 input buffer + 1 output buffer */
static inline int ts_gpu_run_1in_1out(const ts_gpu_ctx *ctx,
                                       cl_kernel kernel,
                                       const double *a, double *out,
                                       size_t a_bytes, size_t out_bytes,
                                       int n, size_t global_size) {
    cl_int err;
    cl_mem buf_a = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   a_bytes, (void *)a, &err);
    if (err != CL_SUCCESS) return -1;
    cl_mem buf_out = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY,
                                     out_bytes, NULL, &err);
    if (err != CL_SUCCESS) { clReleaseMemObject(buf_a); return -1; }

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &buf_a);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &buf_out);
    clSetKernelArg(kernel, 2, sizeof(int), &n);

    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                  &global_size, NULL, 0, NULL, NULL);
    if (err == CL_SUCCESS) {
        clEnqueueReadBuffer(ctx->queue, buf_out, CL_TRUE, 0,
                            out_bytes, out, 0, NULL, NULL);
    }

    clReleaseMemObject(buf_a);
    clReleaseMemObject(buf_out);
    return (err == CL_SUCCESS) ? 0 : -1;
}

#endif /* TS_GPU_AVAILABLE */

/* ================================================================
 * PUBLIC BATCH API
 * ================================================================ */

/* --- Batch vec3 add: out[i] = a[i] + b[i] --- */
static inline int ts_gpu_vec3_add(const ts_gpu_ctx *ctx,
                                   const double *a, const double *b,
                                   double *out, int n) {
#if TS_GPU_AVAILABLE
    if (ctx->active && n >= TS_GPU_MIN_BATCH) {
        size_t bytes = (size_t)n * 3 * sizeof(double);
        return ts_gpu_run_2in_1out(ctx, ctx->k_vec3_add,
                                    a, b, out, bytes, bytes, bytes,
                                    n, (size_t)n);
    }
#else
    (void)ctx;
#endif
    /* CPU fallback */
    for (int i = 0; i < n * 3; i++)
        out[i] = a[i] + b[i];
    return 0;
}

/* --- Batch vec3 normalize --- */
static inline int ts_gpu_vec3_normalize(const ts_gpu_ctx *ctx,
                                         const double *a, double *out, int n) {
#if TS_GPU_AVAILABLE
    if (ctx->active && n >= TS_GPU_MIN_BATCH) {
        size_t bytes = (size_t)n * 3 * sizeof(double);
        return ts_gpu_run_1in_1out(ctx, ctx->k_vec3_normalize,
                                    a, out, bytes, bytes, n, (size_t)n);
    }
#else
    (void)ctx;
#endif
    for (int i = 0; i < n; i++) {
        int j = i * 3;
        double x = a[j], y = a[j+1], z = a[j+2];
        double len = sqrt(x*x + y*y + z*z);
        double inv = (len > 1e-15) ? 1.0 / len : 0.0;
        out[j] = x*inv; out[j+1] = y*inv; out[j+2] = z*inv;
    }
    return 0;
}

/* --- Batch vec3 cross --- */
static inline int ts_gpu_vec3_cross(const ts_gpu_ctx *ctx,
                                     const double *a, const double *b,
                                     double *out, int n) {
#if TS_GPU_AVAILABLE
    if (ctx->active && n >= TS_GPU_MIN_BATCH) {
        size_t bytes = (size_t)n * 3 * sizeof(double);
        return ts_gpu_run_2in_1out(ctx, ctx->k_vec3_cross,
                                    a, b, out, bytes, bytes, bytes,
                                    n, (size_t)n);
    }
#else
    (void)ctx;
#endif
    for (int i = 0; i < n; i++) {
        int j = i * 3;
        double ax=a[j], ay=a[j+1], az=a[j+2];
        double bx=b[j], by=b[j+1], bz=b[j+2];
        out[j]   = ay*bz - az*by;
        out[j+1] = az*bx - ax*bz;
        out[j+2] = ax*by - ay*bx;
    }
    return 0;
}

/* --- Batch vec3 dot --- */
static inline int ts_gpu_vec3_dot(const ts_gpu_ctx *ctx,
                                   const double *a, const double *b,
                                   double *out, int n) {
#if TS_GPU_AVAILABLE
    if (ctx->active && n >= TS_GPU_MIN_BATCH) {
        size_t a_bytes = (size_t)n * 3 * sizeof(double);
        size_t out_bytes = (size_t)n * sizeof(double);
        return ts_gpu_run_2in_1out(ctx, ctx->k_vec3_dot,
                                    a, b, out, a_bytes, a_bytes, out_bytes,
                                    n, (size_t)n);
    }
#else
    (void)ctx;
#endif
    for (int i = 0; i < n; i++) {
        int j = i * 3;
        out[i] = a[j]*b[j] + a[j+1]*b[j+1] + a[j+2]*b[j+2];
    }
    return 0;
}

/* --- Batch mat4 transform points --- */
static inline int ts_gpu_mat4_transform(const ts_gpu_ctx *ctx,
                                         const double mat[16],
                                         const double *pts, double *out,
                                         int n) {
#if TS_GPU_AVAILABLE
    if (ctx->active && n >= TS_GPU_MIN_BATCH) {
        cl_int err;
        size_t mat_bytes = 16 * sizeof(double);
        size_t pts_bytes = (size_t)n * 3 * sizeof(double);

        cl_mem buf_mat = clCreateBuffer(ctx->context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            mat_bytes, (void *)mat, &err);
        if (err != CL_SUCCESS) return -1;
        cl_mem buf_pts = clCreateBuffer(ctx->context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            pts_bytes, (void *)pts, &err);
        if (err != CL_SUCCESS) { clReleaseMemObject(buf_mat); return -1; }
        cl_mem buf_out = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY,
                                         pts_bytes, NULL, &err);
        if (err != CL_SUCCESS) {
            clReleaseMemObject(buf_mat); clReleaseMemObject(buf_pts);
            return -1;
        }

        clSetKernelArg(ctx->k_mat4_transform_points, 0, sizeof(cl_mem), &buf_mat);
        clSetKernelArg(ctx->k_mat4_transform_points, 1, sizeof(cl_mem), &buf_pts);
        clSetKernelArg(ctx->k_mat4_transform_points, 2, sizeof(cl_mem), &buf_out);
        clSetKernelArg(ctx->k_mat4_transform_points, 3, sizeof(int), &n);

        size_t global = (size_t)n;
        err = clEnqueueNDRangeKernel(ctx->queue, ctx->k_mat4_transform_points,
                                      1, NULL, &global, NULL, 0, NULL, NULL);
        if (err == CL_SUCCESS) {
            clEnqueueReadBuffer(ctx->queue, buf_out, CL_TRUE, 0,
                                pts_bytes, out, 0, NULL, NULL);
        }

        clReleaseMemObject(buf_mat);
        clReleaseMemObject(buf_pts);
        clReleaseMemObject(buf_out);
        return (err == CL_SUCCESS) ? 0 : -1;
    }
#else
    (void)ctx;
#endif
    /* CPU fallback */
    for (int i = 0; i < n; i++) {
        int j = i * 3;
        double x = pts[j], y = pts[j+1], z = pts[j+2];
        out[j]   = mat[0]*x + mat[1]*y + mat[2]*z  + mat[3];
        out[j+1] = mat[4]*x + mat[5]*y + mat[6]*z  + mat[7];
        out[j+2] = mat[8]*x + mat[9]*y + mat[10]*z + mat[11];
    }
    return 0;
}

/* --- Batch scalar sqrt --- */
static inline int ts_gpu_scalar_sqrt(const ts_gpu_ctx *ctx,
                                      const double *a, double *out, int n) {
#if TS_GPU_AVAILABLE
    if (ctx->active && n >= TS_GPU_MIN_BATCH) {
        size_t bytes = (size_t)n * sizeof(double);
        return ts_gpu_run_1in_1out(ctx, ctx->k_scalar_sqrt,
                                    a, out, bytes, bytes, n, (size_t)n);
    }
#else
    (void)ctx;
#endif
    for (int i = 0; i < n; i++) out[i] = sqrt(a[i]);
    return 0;
}

/* --- Batch scalar sin (degrees) --- */
static inline int ts_gpu_scalar_sin(const ts_gpu_ctx *ctx,
                                     const double *a, double *out, int n) {
#if TS_GPU_AVAILABLE
    if (ctx->active && n >= TS_GPU_MIN_BATCH) {
        size_t bytes = (size_t)n * sizeof(double);
        return ts_gpu_run_1in_1out(ctx, ctx->k_scalar_sin,
                                    a, out, bytes, bytes, n, (size_t)n);
    }
#else
    (void)ctx;
#endif
    for (int i = 0; i < n; i++) out[i] = sin(a[i] * 0.017453292519943295);
    return 0;
}

/* --- Batch scalar cos (degrees) --- */
static inline int ts_gpu_scalar_cos(const ts_gpu_ctx *ctx,
                                     const double *a, double *out, int n) {
#if TS_GPU_AVAILABLE
    if (ctx->active && n >= TS_GPU_MIN_BATCH) {
        size_t bytes = (size_t)n * sizeof(double);
        return ts_gpu_run_1in_1out(ctx, ctx->k_scalar_cos,
                                    a, out, bytes, bytes, n, (size_t)n);
    }
#else
    (void)ctx;
#endif
    for (int i = 0; i < n; i++) out[i] = cos(a[i] * 0.017453292519943295);
    return 0;
}

/* --- Batch parallel RNG --- */
static inline int ts_gpu_rng_uniform(const ts_gpu_ctx *ctx,
                                      unsigned long seed,
                                      double lo, double hi,
                                      double *out, int n) {
#if TS_GPU_AVAILABLE
    if (ctx->active && n >= TS_GPU_MIN_BATCH) {
        cl_int err;
        size_t bytes = (size_t)n * sizeof(double);
        cl_mem buf_out = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY,
                                         bytes, NULL, &err);
        if (err != CL_SUCCESS) return -1;

        cl_ulong cl_seed = (cl_ulong)seed;
        clSetKernelArg(ctx->k_rng_uniform, 0, sizeof(cl_ulong), &cl_seed);
        clSetKernelArg(ctx->k_rng_uniform, 1, sizeof(double), &lo);
        clSetKernelArg(ctx->k_rng_uniform, 2, sizeof(double), &hi);
        clSetKernelArg(ctx->k_rng_uniform, 3, sizeof(cl_mem), &buf_out);
        clSetKernelArg(ctx->k_rng_uniform, 4, sizeof(int), &n);

        size_t global = (size_t)n;
        err = clEnqueueNDRangeKernel(ctx->queue, ctx->k_rng_uniform,
                                      1, NULL, &global, NULL, 0, NULL, NULL);
        if (err == CL_SUCCESS) {
            clEnqueueReadBuffer(ctx->queue, buf_out, CL_TRUE, 0,
                                bytes, out, 0, NULL, NULL);
        }

        clReleaseMemObject(buf_out);
        return (err == CL_SUCCESS) ? 0 : -1;
    }
#else
    (void)ctx;
#endif
    /* CPU fallback — use ts_random.h style SplitMix64 */
    for (int i = 0; i < n; i++) {
        unsigned long z = seed + (unsigned long)i * 0x9E3779B97F4A7C15UL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9UL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBUL;
        z = z ^ (z >> 31);
        double t = (double)(z >> 11) * (1.0 / 9007199254740992.0);
        out[i] = lo + t * (hi - lo);
    }
    return 0;
}

/* ================================================================
 * CSG BATCH CLASSIFICATION
 *
 * Classify N triangular polygons against a splitting plane.
 * Each polygon is 3 vertices (9 doubles: v0x,v0y,v0z, v1..., v2...).
 * Output: N ints (0=COPLANAR, 1=FRONT, 2=BACK, 3=SPANNING).
 *
 * GPU: one work-item per polygon (3 dot products each).
 * CPU fallback: simple loop.
 *
 * This is the hot inner loop of BSP build and clip operations.
 * At 48K polygons, GPU gives massive parallelism benefit.
 * ================================================================ */

#define TS_GPU_CSG_MIN_BATCH 1024

static inline int ts_gpu_csg_classify_tris(const ts_gpu_ctx *ctx,
                                            const double *packed_verts,
                                            const double plane[4],
                                            double eps,
                                            int *out,
                                            int n) {
#if TS_GPU_AVAILABLE
    if (ctx && ctx->active && n >= TS_GPU_CSG_MIN_BATCH) {
        cl_int err;
        size_t poly_bytes = (size_t)n * 9 * sizeof(double);
        size_t plane_bytes = 4 * sizeof(double);
        size_t out_bytes = (size_t)n * sizeof(int);

        cl_mem buf_polys = clCreateBuffer(ctx->context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            poly_bytes, (void *)packed_verts, &err);
        if (err != CL_SUCCESS) return -1;

        cl_mem buf_plane = clCreateBuffer(ctx->context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            plane_bytes, (void *)plane, &err);
        if (err != CL_SUCCESS) {
            clReleaseMemObject(buf_polys);
            return -1;
        }

        cl_mem buf_out = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY,
                                         out_bytes, NULL, &err);
        if (err != CL_SUCCESS) {
            clReleaseMemObject(buf_polys);
            clReleaseMemObject(buf_plane);
            return -1;
        }

        clSetKernelArg(ctx->k_csg_classify_tris, 0, sizeof(cl_mem), &buf_polys);
        clSetKernelArg(ctx->k_csg_classify_tris, 1, sizeof(cl_mem), &buf_plane);
        clSetKernelArg(ctx->k_csg_classify_tris, 2, sizeof(double), &eps);
        clSetKernelArg(ctx->k_csg_classify_tris, 3, sizeof(cl_mem), &buf_out);
        clSetKernelArg(ctx->k_csg_classify_tris, 4, sizeof(int), &n);

        size_t global = (size_t)n;
        err = clEnqueueNDRangeKernel(ctx->queue, ctx->k_csg_classify_tris,
                                      1, NULL, &global, NULL, 0, NULL, NULL);
        if (err == CL_SUCCESS) {
            clEnqueueReadBuffer(ctx->queue, buf_out, CL_TRUE, 0,
                                out_bytes, out, 0, NULL, NULL);
        }

        clReleaseMemObject(buf_polys);
        clReleaseMemObject(buf_plane);
        clReleaseMemObject(buf_out);
        return (err == CL_SUCCESS) ? 0 : -1;
    }
#else
    (void)ctx;
#endif
    /* CPU fallback */
    for (int i = 0; i < n; i++) {
        int j = i * 9;
        int type = 0;
        for (int k = 0; k < 3; k++) {
            double d = plane[0]*packed_verts[j+k*3] +
                       plane[1]*packed_verts[j+k*3+1] +
                       plane[2]*packed_verts[j+k*3+2] - plane[3];
            int vtype = (d < -eps) ? 2 : ((d > eps) ? 1 : 0);
            type |= vtype;
        }
        out[i] = type;
    }
    return 0;
}

#endif /* TS_OPENCL_H */
