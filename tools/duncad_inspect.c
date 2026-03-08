#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCK_PATH "/tmp/duncad.sock"
#define BUF_SIZE  65536

static void
usage(void)
{
    fprintf(stderr,
        "duncad-inspect — inspect and control a running DunCAD instance\n"
        "\n"
        "Usage: duncad-inspect <command> [args...]\n"
        "\n"
        "BEZIER EDITOR:\n"
        "  state                        Dump bezier editor state (JSON)\n"
        "  render [path]                Render bezier canvas to PNG\n"
        "  select <i>                   Select point (-1 to deselect)\n"
        "  set_point <i> <x> <y>        Move point\n"
        "  add_point <x> <y>            Add point at world coords\n"
        "  delete                       Delete selected point\n"
        "  zoom <level>                 Set zoom (px/mm)\n"
        "  pan <x> <y>                  Set pan center (world coords)\n"
        "  chain <0|1>                  Set chain mode\n"
        "  juncture <i> <0|1>           Set point juncture flag\n"
        "  export <path>                Export to .scad file\n"
        "  insert_scad                  Insert bezier as inline SCAD\n"
        "\n"
        "CODE EDITOR:\n"
        "  get_code                     Get file info (path, length)\n"
        "  get_code_text                Get full source text\n"
        "  set_code <text>              Replace source text\n"
        "  open_file <path>             Open file in editor\n"
        "  save_file [path]             Save (optionally to new path)\n"
        "  select_lines <start> <end>   Highlight line range (1-based)\n"
        "  insert_text <text>           Insert at cursor position\n"
        "\n"
        "SCAD / OPENSCAD:\n"
        "  render_scad <scad> [png]     Render SCAD file to PNG\n"
        "  open_scad <path>             Open file in OpenSCAD GUI\n"
        "  preview_render               Trigger F5 render in DunCAD\n"
        "\n"
        "GL VIEWPORT:\n"
        "  gl_state                     Get camera, display, object state\n"
        "  gl_camera <cx> <cy> <cz> <dist> <theta> <phi>\n"
        "                               Set camera position\n"
        "  gl_reset                     Reset camera to fit mesh\n"
        "  gl_ortho                     Toggle perspective/orthographic\n"
        "  gl_grid                      Toggle grid visibility\n"
        "  gl_axes                      Toggle axis indicator\n"
        "  gl_select <index>            Select object (-1 to deselect)\n"
        "  gl_load <stl_path>           Load STL into viewport\n"
        "  gl_clear                     Clear all objects\n"
        "\n"
        "TRANSFORM PANEL:\n"
        "  transform_show <stmt> <line_start> <line_end>\n"
        "                               Show panel for SCAD statement\n"
        "  transform_hide               Hide transform panel\n"
        "\n"
        "WINDOW:\n"
        "  window_title <title>         Set window title\n"
        "  window_status <text>         Set status bar text\n"
        "  window_size                  Get window dimensions\n"
        "\n"
        "META:\n"
        "  help                         List all commands (JSON)\n"
        "\n"
        "Connects via Unix socket at %s\n",
        SOCK_PATH);
}

int
main(int argc, char **argv)
{
    /* Build command string from argv */
    char cmd[4096];
    size_t pos = 0;

    if (argc < 2) {
        /* Default: state */
        memcpy(cmd, "state\n", 7);
        pos = 6;
    } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage();
        return 0;
    } else {
        for (int i = 1; i < argc && pos < sizeof(cmd) - 2; i++) {
            if (i > 1) cmd[pos++] = ' ';
            size_t len = strlen(argv[i]);
            if (pos + len >= sizeof(cmd) - 2) break;
            memcpy(cmd + pos, argv[i], len);
            pos += len;
        }
        cmd[pos++] = '\n';
        cmd[pos] = '\0';
    }

    /* Connect to DunCAD */
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("duncad-inspect: socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr,
            "duncad-inspect: cannot connect to %s\n"
            "Is DunCAD running?\n", SOCK_PATH);
        close(sock);
        return 1;
    }

    /* Send command */
    ssize_t written = write(sock, cmd, pos);
    if (written < 0) {
        perror("duncad-inspect: write");
        close(sock);
        return 1;
    }

    /* Shutdown write side so server sees EOF */
    shutdown(sock, SHUT_WR);

    /* Read response */
    char buf[BUF_SIZE];
    ssize_t total = 0;
    ssize_t n;
    while ((n = read(sock, buf + total,
                     (size_t)(BUF_SIZE - 1 - total))) > 0) {
        total += n;
        if (total >= BUF_SIZE - 1) break;
    }
    buf[total] = '\0';

    printf("%s", buf);

    close(sock);
    return 0;
}
