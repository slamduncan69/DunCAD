/*
 * ts_text.h — Text rendering via embedded Hershey Simplex font
 *
 * Generates 2D polygon outlines from text strings using the public-domain
 * Hershey vector font (futural/simplex). Each glyph is a set of polyline
 * strokes. Strokes are thickened into rectangles and unioned into a mesh.
 *
 * Usage: ts_gen_text("Hello", 10.0, "left", "baseline", 1.0, &mesh)
 * Output is 2D (z=0) — use linear_extrude for 3D.
 */
#ifndef TS_TEXT_H
#define TS_TEXT_H

#include "ts_mesh.h"
#include <string.h>
#include <math.h>

/* ================================================================
 * HERSHEY SIMPLEX ROMAN FONT DATA
 *
 * Public domain. 96 glyphs: ASCII 32 (space) through 127 (DEL).
 * Format: vertex_count, left_bound, right_bound, coordinate_pairs
 * Coordinates relative to 'R' (ASCII 82). ' R' = pen up.
 * ================================================================ */
static const char *ts_hershey_glyphs[96] = {
    /* 32 SP */ "1JZ",
    /* 33 !  */ "9MWRFRT RRYQZR[SZRY",
    /* 34 "  */ "6JZNFNM RVFVM",
    /* 35 #  */ "12H]SBLb RYBRb RLOZO RKUYU",
    /* 36 $  */ "27H\\PBP_ RTBT_ RYIWGTFPFMGKIKKLMMNOOUQWRXSYUYXWZT[P[MZKX",
    /* 37 %  */ "32F^[FI[ RNFPHPJOLMMKMIKIIJGLFNFPGSHVHYG[F RWTUUTWTYV[X[ZZ[X[VYTWT",
    /* 38 &  */ "35E_\\O\\N[MZMYNXPVUTXRZP[L[JZIYHWHUISJRQNRMSKSIRGPFNGMIMKNNPQUXWZY[[\\[\\Z\\Y",
    /* 39 '  */ "8MWRHQGRFSGSIRKQL",
    /* 40 (  */ "11KYVBTDRGPKOPOTPYR]T`Vb",
    /* 41 )  */ "11KYNBPDRGTKUPUTTYR]P`Nb",
    /* 42 *  */ "9JZRLRX RMOWU RWOMU",
    /* 43 +  */ "6E_RIR[ RIR[R",
    /* 44 ,  */ "8NVSWRXQWRVSWSYQ[",
    /* 45 -  */ "3E_IR[R",
    /* 46 .  */ "6NVRVQWRXSWRV",
    /* 47 /  */ "3G][BIb",
    /* 48 0  */ "18H\\QFNGLJKOKRLWNZQ[S[VZXWYRYOXJVGSFQF",
    /* 49 1  */ "5H\\NJPISFS[",
    /* 50 2  */ "15H\\LKLJMHNGPFTFVGWHXJXLWNUQK[Y[",
    /* 51 3  */ "16H\\MFXFRNUNWOXPYSYUXXVZS[P[MZLYKW",
    /* 52 4  */ "7H\\UFKTZT RUFU[",
    /* 53 5  */ "18H\\WFMFLOMNPMSMVNXPYSYUXXVZS[P[MZLYKW",
    /* 54 6  */ "24H\\XIWGTFRFOGMJLOLTMXOZR[S[VZXXYUYTXQVOSNRNOOMQLT",
    /* 55 7  */ "6H\\YFO[ RKFYF",
    /* 56 8  */ "30H\\PFMGLILKMMONSOVPXRYTYWXYWZT[P[MZLYKWKTLRNPQOUNWMXKXIWGTFPF",
    /* 57 9  */ "24H\\XMWPURRSQSNRLPKMKLLINGQFRFUGWIXMXRWWUZR[P[MZLX",
    /* 58 :  */ "12NVROQPRQSPRO RRVQWRXSWRV",
    /* 59 ;  */ "14NVROQPRQSPRO RSWRXQWRVSWSYQ[",
    /* 60 <  */ "4F^ZIJRZ[",
    /* 61 =  */ "6E_IO[O RIU[U",
    /* 62 >  */ "4F^JIZRJ[",
    /* 63 ?  */ "21I[LKLJMHNGPFTFVGWHXJXLWNVORQRT RRYQZR[SZRY",
    /* 64 @  */ "56E`WNVLTKQKOLNMMPMSNUPVSVUUVS RQKOMNPNSOUPV RWKVSVUXVZV\\T]Q]O\\L[JYHWGTFQFNGLHJJILHOHRIUJWLYNZQ[T[WZYYZX RXKWSWUXV",
    /* 65 A  */ "9I[RFJ[ RRFZ[ RMTWT",
    /* 66 B  */ "24G\\KFK[ RKFTFWGXHYJYLXNWOTP RKPTPWQXRYTYWXYWZT[K[",
    /* 67 C  */ "19H]ZKYIWGUFQFOGMILKKNKSLVMXOZQ[U[WZYXZV",
    /* 68 D  */ "16G\\KFK[ RKFRFUGWIXKYNYSXVWXUZR[K[",
    /* 69 E  */ "12H[LFL[ RLFYF RLPTP RL[Y[",
    /* 70 F  */ "9HZLFL[ RLFYF RLPTP",
    /* 71 G  */ "23H]ZKYIWGUFQFOGMILKKNKSLVMXOZQ[U[WZYXZVZS RUSZS",
    /* 72 H  */ "9G]KFK[ RYFY[ RKPYP",
    /* 73 I  */ "3NVRFR[",
    /* 74 J  */ "11JZVFVVUYTZR[P[NZMYLVLT",
    /* 75 K  */ "9G\\KFK[ RYFKT RPOY[",
    /* 76 L  */ "6HYLFL[ RL[X[",
    /* 77 M  */ "12F^JFJ[ RJFR[ RZFR[ RZFZ[",
    /* 78 N  */ "9G]KFK[ RKFY[ RYFY[",
    /* 79 O  */ "22G]PFNGLIKKJNJSKVLXNZP[T[VZXXYVZSZNYKXIVGTFPF",
    /* 80 P  */ "14G\\KFK[ RKFTFWGXHYJYMXOWPTQKQ",
    /* 81 Q  */ "25G]PFNGLIKKJNJSKVLXNZP[T[VZXXYVZSZNYKXIVGTFPF RSWY]",
    /* 82 R  */ "17G\\KFK[ RKFTFWGXHYJYLXNWOTPKP RRPY[",
    /* 83 S  */ "21H\\YIWGTFPFMGKIKKLMMNOOUQWRXSYUYXWZT[P[MZKX",
    /* 84 T  */ "6JZRFR[ RKFYF",
    /* 85 U  */ "11G]KFKULXNZQ[S[VZXXYUYF",
    /* 86 V  */ "6I[JFR[ RZFR[",
    /* 87 W  */ "12F^HFM[ RRFM[ RRFW[ R\\FW[",
    /* 88 X  */ "6H\\KFY[ RYFK[",
    /* 89 Y  */ "7I[JFRPR[ RZFRP",
    /* 90 Z  */ "9H\\YFK[ RKFYF RK[Y[",
    /* 91 [  */ "12KYOBOb RPBPb ROBVB RObVb",
    /* 92 \  */ "3KYKFY^",
    /* 93 ]  */ "12KYTBTb RUBUb RNBUB RNbUb",
    /* 94 ^  */ "6JZRDJR RRDZR",
    /* 95 _  */ "3I[Ib[b",
    /* 96 `  */ "8NVSKQMQORPSORNQO",
    /* 97 a  */ "18I\\XMX[ RXPVNTMQMONMPLSLUMXOZQ[T[VZXX",
    /* 98 b  */ "18H[LFL[ RLPNNPMSMUNWPXSXUWXUZS[P[NZLX",
    /* 99 c  */ "15I[XPVNTMQMONMPLSLUMXOZQ[T[VZXX",
    /* 100 d */ "18I\\XFX[ RXPVNTMQMONMPLSLUMXOZQ[T[VZXX",
    /* 101 e */ "18I[LSXSXQWOVNTMQMONMPLSLUMXOZQ[T[VZXX",
    /* 102 f */ "9MYWFUFSGRJR[ ROMVM",
    /* 103 g */ "23I\\XMX]W`VaTbQbOa RXPVNTMQMONMPLSLUMXOZQ[T[VZXX",
    /* 104 h */ "11I\\MFM[ RMQPNRMUMWNXQX[",
    /* 105 i */ "9NVQFRGSFREQF RRMR[",
    /* 106 j */ "12MWRFSGTFSERF RSMS^RaPbNb",
    /* 107 k */ "9IZMFM[ RWMMW RQSX[",
    /* 108 l */ "3NVRFR[",
    /* 109 m */ "19CaGMG[ RGQJNLMOMQNRQR[ RRQUNWMZM\\N]Q][",
    /* 110 n */ "11I\\MMM[ RMQPNRMUMWNXQX[",
    /* 111 o */ "18I\\QMONMPLSLUMXOZQ[T[VZXXYUYSXPVNTMQM",
    /* 112 p */ "18H[LMLb RLPNNPMSMUNWPXSXUWXUZS[P[NZLX",
    /* 113 q */ "18I\\XMXb RXPVNTMQMONMPLSLUMXOZQ[T[VZXX",
    /* 114 r */ "9KXOMO[ ROSPPRNTMWM",
    /* 115 s */ "18J[XPWNTMQMNNMPNRPSUTWUXWXXWZT[Q[NZMX",
    /* 116 t */ "9MYRFRWSZU[W[ ROMVM",
    /* 117 u */ "11I\\MMMWNZP[S[UZXW RXMX[",
    /* 118 v */ "6JZLMR[ RXMR[",
    /* 119 w */ "12G]JMN[ RRMN[ RRMV[ RZMV[",
    /* 120 x */ "6J[MMX[ RXMM[",
    /* 121 y */ "10JZLMR[ RXMR[P_NaLbKb",
    /* 122 z */ "9J[XMM[ RMMXM RM[X[",
    /* 123 { */ "40KYTBRCQDPFPHQJRKSMSOQQ RRCQEQGRISJTLTNSPORSTTVTXSZR[Q]Q_Ra RQSSUSWRYQZP\\P^Q`RaTb",
    /* 124 | */ "3NVRBRb",
    /* 125 } */ "40KYPBRCSDTFTHSJRKQMQOSQ RRCSESGRIQJPLPNQPURQTPVPXQZR[S]S_Ra RSSQUQWRYSZT\\T^S`RaPb",
    /* 126 ~ */ "24F^IUISJPLONOPPTSVTXTZS[Q RISJQLPNPPQTTVUXUZT[Q[O",
    /* 127 DEL */ "35JZJFJ[K[KFLFL[M[MFNFN[O[OFPFP[Q[QFRFR[S[SFTFT[U[UFVFV[W[WFXFX[Y[YFZFZ[",
};

/* ================================================================
 * GLYPH DECODING
 * ================================================================ */

/* Decoded stroke: array of (x,y) points. pen_up marks start new polyline. */
typedef struct {
    double x, y;
    int    pen_up;  /* 1 = move-to (start new stroke), 0 = line-to */
} ts_stroke_pt;

/* Decode a Hershey glyph string into stroke points.
 * Returns number of points. left/right set to glyph bounds. */
static inline int ts_hershey_decode(const char *g, ts_stroke_pt *out,
                                     int max_pts, double *left, double *right) {
    if (!g || !*g) return 0;

    /* Parse vertex count */
    const char *p = g;
    while (*p == ' ') p++;
    int nverts = 0;
    while (*p >= '0' && *p <= '9') { nverts = nverts * 10 + (*p - '0'); p++; }

    /* Left and right bounds */
    if (!*p) return 0;
    *left = (double)(*p++ - 'R');
    if (!*p) return 0;
    *right = (double)(*p++ - 'R');

    /* Decode coordinate pairs */
    int count = 0;
    int pen_up = 1; /* first point is always move-to */
    while (*p && count < max_pts) {
        if (*p == ' ' && *(p+1) == 'R') {
            /* Pen up */
            pen_up = 1;
            p += 2;
            continue;
        }
        if (*p == ' ') { p++; continue; }

        char cx = *p++;
        if (!*p) break;
        char cy = *p++;

        out[count].x = (double)(cx - 'R');
        out[count].y = -(double)(cy - 'R'); /* Y inverted in Hershey */
        out[count].pen_up = pen_up;
        pen_up = 0;
        count++;
    }
    return count;
}

/* ================================================================
 * TEXT MESH GENERATION
 *
 * For each stroke segment, generate a thin rectangle (thick line).
 * Stroke width = size / 20 (proportional to text size).
 * Output is 2D geometry at z=0 (triangulated rectangles).
 * ================================================================ */

/* Add a thick line segment as 2 triangles at z=0 */
static inline void ts_text_add_segment(ts_mesh *m,
                                        double x0, double y0,
                                        double x1, double y1,
                                        double half_w) {
    double dx = x1 - x0, dy = y1 - y0;
    double len = sqrt(dx*dx + dy*dy);
    if (len < 1e-12) return;

    /* Perpendicular direction */
    double px = -dy / len * half_w;
    double py =  dx / len * half_w;

    /* 4 corners of the rectangle */
    int v0 = ts_mesh_add_vertex(m, x0+px, y0+py, 0, 0, 0, 1);
    int v1 = ts_mesh_add_vertex(m, x0-px, y0-py, 0, 0, 0, 1);
    int v2 = ts_mesh_add_vertex(m, x1-px, y1-py, 0, 0, 0, 1);
    int v3 = ts_mesh_add_vertex(m, x1+px, y1+py, 0, 0, 0, 1);
    ts_mesh_add_triangle(m, v0, v1, v2);
    ts_mesh_add_triangle(m, v0, v2, v3);
}

/* Add a small circle (n-gon) at a point for rounded joints/caps */
static inline void ts_text_add_dot(ts_mesh *m, double cx, double cy,
                                    double r, int segs) {
    int center_v = ts_mesh_add_vertex(m, cx, cy, 0, 0, 0, 1);
    int first_v = -1, prev_v = -1;
    for (int i = 0; i <= segs; i++) {
        double a = 2.0 * M_PI * (double)i / (double)segs;
        double x = cx + r * cos(a);
        double y = cy + r * sin(a);
        int v = ts_mesh_add_vertex(m, x, y, 0, 0, 0, 1);
        if (i == 0) first_v = v;
        if (i > 0) ts_mesh_add_triangle(m, center_v, prev_v, v);
        prev_v = v;
    }
    (void)first_v;
}

/* Generate 2D text mesh from string.
 * size = approximate cap height.
 * halign: "left", "center", "right"
 * valign: "baseline", "top", "center", "bottom"
 * spacing: character spacing multiplier (1.0 = normal) */
static inline void ts_gen_text(const char *text, double size,
                                const char *halign, const char *valign,
                                double spacing, ts_mesh *m) {
    if (!text || !*text || size <= 0) return;

    /* Hershey reference height: roughly 21 units (F at y=-12 to baseline y=9) */
    double scale = size / 21.0;
    double stroke_w = size / 18.0; /* stroke thickness */
    double half_w = stroke_w * 0.5;
    int dot_segs = 6; /* segments for round caps */

    /* First pass: compute total advance width for alignment */
    double total_advance = 0;
    for (const char *cp = text; *cp; cp++) {
        int idx = (unsigned char)*cp - 32;
        if (idx < 0 || idx >= 96) idx = 0; /* space for unknown */
        ts_stroke_pt pts[512];
        double left, right;
        ts_hershey_decode(ts_hershey_glyphs[idx], pts, 512, &left, &right);
        total_advance += (right - left) * scale * spacing;
    }

    /* Compute origin offset for alignment */
    double ox = 0, oy = 0;
    if (halign && strcmp(halign, "center") == 0) ox = -total_advance * 0.5;
    else if (halign && strcmp(halign, "right") == 0) ox = -total_advance;

    /* valign: baseline=0, top shifts down by cap height, etc. */
    double cap_h = 12.0 * scale; /* approximate cap height in Hershey units */
    if (valign && strcmp(valign, "top") == 0) oy = -cap_h;
    else if (valign && strcmp(valign, "center") == 0) oy = -cap_h * 0.5;
    else if (valign && strcmp(valign, "bottom") == 0) oy = 9.0 * scale; /* descender */

    /* Second pass: generate geometry */
    double cursor_x = ox;
    for (const char *cp = text; *cp; cp++) {
        int idx = (unsigned char)*cp - 32;
        if (idx < 0 || idx >= 96) idx = 0;

        ts_stroke_pt pts[512];
        double left, right;
        int npts = ts_hershey_decode(ts_hershey_glyphs[idx], pts, 512,
                                      &left, &right);

        /* Render strokes */
        double px = 0, py = 0;
        int has_prev = 0;
        for (int i = 0; i < npts; i++) {
            double x = cursor_x + (pts[i].x - left) * scale;
            double y = oy + pts[i].y * scale;

            if (pts[i].pen_up) {
                /* Start new stroke — add dot at start point */
                ts_text_add_dot(m, x, y, half_w, dot_segs);
                has_prev = 1;
                px = x; py = y;
                continue;
            }

            if (has_prev) {
                ts_text_add_segment(m, px, py, x, y, half_w);
                /* Add dot at joint for rounded appearance */
                ts_text_add_dot(m, x, y, half_w, dot_segs);
            }
            px = x; py = y;
            has_prev = 1;
        }

        cursor_x += (right - left) * scale * spacing;
    }
}

#endif /* TS_TEXT_H */
