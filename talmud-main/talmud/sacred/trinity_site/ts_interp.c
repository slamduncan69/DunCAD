/*
 * ts_interp.c — OpenSCAD interpreter powered by Trinity Site
 *
 * Usage:
 *   ts_interp <file.scad>                   Render to output.stl
 *   ts_interp <file.scad> -o <output.stl>   Render to specified STL
 *   ts_interp --help                        Show usage
 *
 * "Now I am become Death, the destroyer of worlds."
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Interpreter (pulls in all Trinity Site headers) */
#include "ts_eval.h"

static void print_help(void) {
    printf(
        "ts_interp — OpenSCAD interpreter (Trinity Site engine)\n"
        "\n"
        "Usage:\n"
        "  ts_interp <file.scad>                Render to output.stl\n"
        "  ts_interp <file.scad> -o <out.stl>   Render to specified file\n"
        "  ts_interp --help                      Show this help\n"
        "\n"
        "Supports: cube, sphere, cylinder, translate, rotate, scale, mirror,\n"
        "  union, difference, intersection, hull, minkowski,\n"
        "  linear_extrude, rotate_extrude, circle, square, polygon,\n"
        "  module/function definitions, if/else, for loops, variables,\n"
        "  all OpenSCAD math functions, $fn/$fa/$fs.\n"
    );
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        return 0;
    }

    const char *input = argv[1];
    const char *output = "output.stl";

    for (int i = 2; i < argc; i++) {
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0)
            && i + 1 < argc) {
            output = argv[++i];
        }
    }

    printf("Trinity Site OpenSCAD Interpreter\n");
    printf("Input:  %s\n", input);
    printf("Output: %s\n", output);
    printf("\n");

    int ret = ts_interpret_file(input, output);
    return ret == 0 ? 0 : 1;
}
