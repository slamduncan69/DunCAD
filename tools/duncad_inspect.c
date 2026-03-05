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
        "Usage:\n"
        "  duncad-inspect                 Dump current state (JSON)\n"
        "  duncad-inspect state           Same as above\n"
        "  duncad-inspect render [path]   Render canvas to PNG\n"
        "  duncad-inspect select <i>      Select point by index (-1 to deselect)\n"
        "  duncad-inspect set_point <i> <x> <y>   Move point\n"
        "  duncad-inspect add_point <x> <y>        Add point at world coords\n"
        "  duncad-inspect delete           Delete selected point\n"
        "  duncad-inspect zoom <level>     Set zoom (px/mm)\n"
        "  duncad-inspect pan <x> <y>      Set pan center (world coords)\n"
        "  duncad-inspect chain <0|1>      Set chain mode\n"
        "  duncad-inspect juncture <i> <0|1>  Set point juncture flag\n"
        "  duncad-inspect export <path>    Export to .scad file\n"
        "  duncad-inspect help             List commands (JSON)\n"
        "\n"
        "Connects to DunCAD via Unix socket at %s\n",
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
