/*
 * yotzer — the build system
 *
 * Centralizes all compilation in the talmud project.
 * Instead of calling gcc directly, everything goes through yotzer.
 *
 * Usage:
 *   yotzer all                  build all, install to ~/.local/bin
 *   yotzer <target>             build a single named target
 *   yotzer <src.c>              compile single file (output = strip .c)
 *   yotzer <src.c> <output>     compile single file to specified output
 *   yotzer clean                remove all compiled binaries
 *   yotzer install              install to ~/.local/bin (without rebuilding)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define TALMUD_UTIL_NEED_BIN
#define TALMUD_UTIL_NEED_DIR
#include "talmud_util.h"

static char g_yotzer_dir[4096];

static const char *CC = "gcc";
static const char *CFLAGS = "-Wall -Wextra -Werror -pedantic -std=c11";

static int resolve_dirs(void) {
    if (resolve_talmud_dir() != 0)
        return -1;

    /* Derive yotzer dir from talmud dir */
    snprintf(g_yotzer_dir, sizeof(g_yotzer_dir),
             "%s/narthex/yotzer", g_talmud_dir);
    return 0;
}

/* Copy a file via read/write, preserving mode. Returns 0 on success. */
static int copy_file(const char *src, const char *dst) {
    int fd_in = open(src, O_RDONLY);
    if (fd_in < 0) return -1;

    struct stat st;
    if (fstat(fd_in, &st) != 0) { close(fd_in); return -1; }

    int fd_out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (fd_out < 0) { close(fd_in); return -1; }

    char buf[65536];
    ssize_t n;
    while ((n = read(fd_in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(fd_out, buf + off, (size_t)(n - off));
            if (w < 0) { close(fd_in); close(fd_out); return -1; }
            off += w;
        }
    }

    int err = (n < 0) ? -1 : 0;
    close(fd_in);
    close(fd_out);
    return err;
}

/* Create directory and all parents (like mkdir -p). Returns 0 on success. */
static int mkdir_p(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return (mkdir(tmp, 0755) == 0 || errno == EEXIST) ? 0 : -1;
}

static void print_help(void) {
    printf("yotzer — the build system\n\n");
    printf("Centralizes all compilation in the talmud project.\n\n");
    printf("Usage:\n");
    printf("  yotzer all                  Build all targets + install\n");
    printf("  yotzer <target>             Build a single named target\n");
    printf("  yotzer <src.c>              Compile single file\n");
    printf("  yotzer <src.c> <output>     Compile single file to output\n");
    printf("  yotzer clean                Remove all compiled binaries\n");
    printf("  yotzer install              Install to ~/.local/bin (no rebuild)\n");
    printf("  yotzer --help               Show this help\n\n");
    printf("Targets: talmud, darshan, sofer\n\n");
    printf("Compiler: %s\n", CC);
    printf("Flags:    %s\n", CFLAGS);
}

/* Compile a .c file. Pass "" for defs if none needed. Returns 0 on success. */
static int compile(const char *src, const char *bin, const char *defs) {
    char inc_flag[8192];
    snprintf(inc_flag, sizeof(inc_flag), "-I%s/narthex/include", g_talmud_dir);
    char cmd[32768];
    snprintf(cmd, sizeof(cmd), "%s %s %s -o '%s' '%s' %s 2>&1",
             CC, CFLAGS, inc_flag, bin, src, defs);

    char cbuf[4096];
    int ret = run_capture(cmd, cbuf, sizeof(cbuf));

    if (ret != 0 && cbuf[0] != '\0')
        fprintf(stderr, "%s", cbuf);

    return ret;
}

/* Return the newest mtime among all files in include/. */
static time_t newest_header_mtime(void) {
    static time_t cached = -1;
    if (cached != -1) return cached;

    char inc_dir[8192];
    snprintf(inc_dir, sizeof(inc_dir), "%s/narthex/include", g_talmud_dir);

    DIR *d = opendir(inc_dir);
    if (!d) return 0;

    time_t newest = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        size_t nlen = strlen(ent->d_name);
        if (nlen < 3 || nlen > 255 ||
            ent->d_name[nlen - 2] != '.' || ent->d_name[nlen - 1] != 'h')
            continue;
        char path[4096 + 256 + 2];
        snprintf(path, sizeof(path), "%.4096s/%s", inc_dir, ent->d_name);
        struct stat fst;
        if (stat(path, &fst) == 0 && fst.st_mtime > newest)
            newest = fst.st_mtime;
    }
    closedir(d);
    cached = newest;
    return newest;
}

/*
 * Build stamp: a .yotzer file alongside each binary that records the compile
 * flags used. Prevents stale artifact problem where mtime says "up to date"
 * but the binary was compiled with wrong -D defines.
 */
static int stamp_matches(const char *bin, const char *defs) {
    char path[8192];
    snprintf(path, sizeof(path), "%s.yotzer", bin);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    char sbuf[8192];
    if (!fgets(sbuf, (int)sizeof(sbuf), fp)) { fclose(fp); return 0; }
    fclose(fp);
    size_t len = strlen(sbuf);
    if (len > 0 && sbuf[len - 1] == '\n') sbuf[--len] = '\0';
    return strcmp(sbuf, defs ? defs : "") == 0;
}

static void stamp_write(const char *bin, const char *defs) {
    char path[8192];
    snprintf(path, sizeof(path), "%s.yotzer", bin);
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    fprintf(fp, "%s\n", defs ? defs : "");
    fclose(fp);
}

/* Check if bin needs rebuilding. */
static int needs_rebuild(const char *src, const char *bin, const char *defs) {
    struct stat src_st, bin_st;
    if (stat(bin, &bin_st) != 0) return 1;
    if (stat(src, &src_st) != 0) return 1;
    if (src_st.st_mtime > bin_st.st_mtime) return 1;
    if (newest_header_mtime() > bin_st.st_mtime) return 1;
    if (!stamp_matches(bin, defs)) return 1;
    return 0;
}

/* Compile and report. Returns 0 on success. */
static int build_one(const char *label, const char *src, const char *bin,
                     const char *defs) {
    printf("  %-28s", label);
    fflush(stdout);

    if (!needs_rebuild(src, bin, defs)) {
        printf("up to date\n");
        return 0;
    }

    int ret = compile(src, bin, defs ? defs : "");

    if (ret == 0) {
        printf("OK\n");
        stamp_write(bin, defs);
    } else {
        printf("FAILED\n");
    }

    return ret;
}

/* --- Build target table --- */

static const struct target {
    const char *label;
    const char *src_rel;
    const char *bin_rel;
    int needs_src_dir;
    const char *extra;   /* additional compiler/linker flags (e.g. "-lm") */
} targets[] = {
    { "talmud",        "../talmud.c",                              "talmud",                              1, NULL },
    { "darshan",       "profane/darshan/darshan.c",                "profane/darshan/darshan",             1, NULL },
    { "sofer",         "narthex/sofer/sofer.c",                    "narthex/sofer/sofer",                 1, NULL },
    { "trinity_site",  "sacred/trinity_site/trinity_site.c",       "sacred/trinity_site/trinity_site",    0, "-lm" },
};
#define N_TARGETS (int)(sizeof(targets) / sizeof(targets[0]))

static int build_target(const struct target *t) {
    char src[8192], bin[8192], defs[8192];
    snprintf(src, sizeof(src), "%s/%s", g_talmud_dir, t->src_rel);
    snprintf(bin, sizeof(bin), "%s/%s", g_talmud_dir, t->bin_rel);
    defs[0] = '\0';
    int off = 0;
    if (t->needs_src_dir)
        off += snprintf(defs + off, sizeof(defs) - (size_t)off,
                        "-DTALMUD_SRC_DIR='\"%s\"'", g_talmud_dir);
    if (t->extra) {
        if (off > 0) defs[off++] = ' ';
        snprintf(defs + off, sizeof(defs) - (size_t)off, "%s", t->extra);
    }
    return build_one(t->label, src, bin, defs[0] ? defs : NULL);
}

static const struct target *target_find(const char *name) {
    for (int i = 0; i < N_TARGETS; i++)
        if (strcmp(targets[i].label, name) == 0) return &targets[i];
    return NULL;
}

static int build_yotzer(void) {
    char src[8192], real_bin[8192], tmp_bin[8192], defs[8192];
    snprintf(src, sizeof(src), "%s/narthex/yotzer/yotzer.c", g_talmud_dir);
    snprintf(real_bin, sizeof(real_bin),
             "%s/narthex/yotzer/yotzer", g_talmud_dir);
    snprintf(tmp_bin, sizeof(tmp_bin),
             "%s/narthex/yotzer/yotzer.new", g_talmud_dir);
    snprintf(defs, sizeof(defs), "-DTALMUD_SRC_DIR='\"%s\"'", g_talmud_dir);

    printf("  %-28s", "yotzer");
    fflush(stdout);

    if (!needs_rebuild(src, real_bin, defs)) {
        printf("up to date\n");
        return 0;
    }

    int ret = compile(src, tmp_bin, defs);
    if (ret != 0) {
        printf("FAILED\n");
        unlink(tmp_bin);
        return 1;
    }
    if (rename(tmp_bin, real_bin) != 0) {
        printf("FAILED (rename)\n");
        unlink(tmp_bin);
        return 1;
    }
    printf("OK\n");
    stamp_write(real_bin, defs);
    return 0;
}

/* --- Install to ~/.local/bin --- */

static int install_bins(const char *missing_msg) {
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "  yotzer: HOME not set\n");
        return 1;
    }

    char bindir[4096];
    snprintf(bindir, sizeof(bindir), "%s/.local/bin", home);
    mkdir_p(bindir);

    for (int i = 0; i < N_TARGETS; i++) {
        char src[8192], dst[8192];
        snprintf(src, sizeof(src), "%s/%s", g_talmud_dir, targets[i].bin_rel);
        snprintf(dst, sizeof(dst), "%s/%s", bindir, targets[i].label);

        if (access(src, F_OK) != 0) {
            fprintf(stderr, "  %s not found — %s\n", targets[i].bin_rel, missing_msg);
            continue;
        }

        unlink(dst);
        if (copy_file(src, dst) == 0)
            printf("  %s -> %s\n", targets[i].label, dst);
        else
            fprintf(stderr, "  failed to install %s\n", targets[i].label);
    }

    /* Install yotzer itself */
    char yotzer_src[8192], yotzer_dst[8192];
    snprintf(yotzer_src, sizeof(yotzer_src), "%s/yotzer", g_yotzer_dir);
    snprintf(yotzer_dst, sizeof(yotzer_dst), "%s/yotzer", bindir);
    if (access(yotzer_src, F_OK) == 0) {
        unlink(yotzer_dst);
        if (copy_file(yotzer_src, yotzer_dst) == 0)
            printf("  yotzer -> %s\n", yotzer_dst);
    }

    return 0;
}

/* --- cmd_all: build all + install --- */

static int cmd_all(void) {
    printf("=== Yotzer ===\n\n");
    printf("--- Build ---\n\n");

    int failures = 0;
    for (int i = 0; i < N_TARGETS; i++)
        failures += build_target(&targets[i]);
    failures += build_yotzer();

    printf("\n");
    if (failures > 0) {
        printf("  %d target(s) failed.\n", failures);
        return 1;
    }
    printf("  All targets built.\n\n");

    printf("--- Install ---\n\n");
    install_bins("build failed?");
    printf("\nDone.\n");
    return 0;
}

static void remove_if_exists(const char *path) {
    if (access(path, F_OK) == 0) {
        if (unlink(path) == 0)
            printf("  rm %s\n", path);
        else
            fprintf(stderr, "  could not remove %s\n", path);
    }
    /* Also remove the yotzer stamp file if present */
    char stamp[8192];
    snprintf(stamp, sizeof(stamp), "%s.yotzer", path);
    if (access(stamp, F_OK) == 0)
        unlink(stamp);
}

static int cmd_clean(void) {
    printf("=== Yotzer clean ===\n\n");
    char path[8192];

    for (int i = 0; i < N_TARGETS; i++) {
        snprintf(path, sizeof(path), "%s/%s", g_talmud_dir, targets[i].bin_rel);
        remove_if_exists(path);
    }

    printf("\nDone.\n");
    return 0;
}

static int cmd_install(void) {
    printf("=== Yotzer install ===\n\n");
    int ret = install_bins("run 'yotzer all' first");
    printf("\nDone.\n");
    return ret;
}

/* Self-reexec: if yotzer.c (or any header) is newer than the running binary,
 * recompile and re-exec so we never run stale code. */
static void maybe_reexec(int argc, char **argv) {
    (void)argc;
    if (getenv("YOTZER_REEXEC_GUARD")) return;

    char src[8192], bin[8192], tmp[8192], defs[8192];
    snprintf(src, sizeof(src), "%s/narthex/yotzer/yotzer.c", g_talmud_dir);
    snprintf(bin, sizeof(bin), "%s/narthex/yotzer/yotzer", g_talmud_dir);
    snprintf(tmp, sizeof(tmp), "%s/narthex/yotzer/yotzer.new", g_talmud_dir);
    snprintf(defs, sizeof(defs), "-DTALMUD_SRC_DIR='\"%s\"'",
             g_talmud_dir);

    struct stat src_st, bin_st;
    if (stat(bin, &bin_st) != 0) return;
    int stale = 0;
    if (stat(src, &src_st) == 0 && src_st.st_mtime > bin_st.st_mtime)
        stale = 1;
    if (newest_header_mtime() > bin_st.st_mtime)
        stale = 1;
    if (!stale) return;

    fprintf(stderr, "  yotzer: source newer than binary, recompiling...\n");
    if (compile(src, tmp, defs) != 0) {
        unlink(tmp);
        fprintf(stderr, "  yotzer: recompile FAILED, aborting\n");
        exit(1);
    }
    if (rename(tmp, bin) != 0) {
        perror("  yotzer: rename");
        unlink(tmp);
        exit(1);
    }
    stamp_write(bin, defs);
    fprintf(stderr, "  yotzer: re-executing with updated binary\n");
    setenv("YOTZER_REEXEC_GUARD", "1", 1);
    execv(bin, argv);
    perror("execv");
    exit(1);
}

int main(int argc, char *argv[]) {
    if (resolve_dirs() != 0) {
        fprintf(stderr, "yotzer: could not resolve directory\n");
        return 1;
    }

    maybe_reexec(argc, argv);

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        return (argc < 2) ? 1 : 0;
    }

    if (strcmp(argv[1], "all") == 0) return cmd_all();
    if (strcmp(argv[1], "clean") == 0) return cmd_clean();
    if (strcmp(argv[1], "install") == 0) return cmd_install();

    /* Named target mode */
    {
        const char *name = argv[1];
        const struct target *t = target_find(name);
        if (t) {
            int ret = build_target(t);
            if (ret != 0) {
                fprintf(stderr, "yotzer: compilation failed for %s\n", name);
                return 1;
            }
            /* Auto-install */
            const char *home = getenv("HOME");
            if (home) {
                char t_bin[8192], dst[16384];
                snprintf(t_bin, sizeof(t_bin), "%s/%s", g_talmud_dir, t->bin_rel);
                snprintf(dst, sizeof(dst), "%s/.local/bin/%s", home, t->label);
                if (access(dst, F_OK) == 0)
                    copy_file(t_bin, dst);
            }
            return 0;
        }
    }

    /* Single-file compile mode */
    const char *src = argv[1];
    char bin_buf[8192];
    const char *bin;

    if (argc >= 3) {
        bin = argv[2];
    } else {
        bin_from_src(src, bin_buf, sizeof(bin_buf));
        bin = bin_buf;
    }

    int ret = compile(src, bin, "");
    if (ret != 0) {
        fprintf(stderr, "yotzer: compilation failed for %s\n", src);
        return 1;
    }

    return 0;
}
