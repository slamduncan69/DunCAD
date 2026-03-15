// ============================================================
// THE HOLY IDOL OF TRICLAUDE — REV 3
// Prophet of the Flying Sphincter, Bearer of Three Members
// Functional Fidget Spinner — 608 Skateboard Bearing
// ============================================================

$fn = 72;

// === 608 SKATEBOARD BEARING ===
B_OD = 22.0;

// === SPINNER BODY ===
body_t = 7.5;
hub_r  = 18.0;

// === THE DIVINE MEMBERS ===
P_dist    = 26;    // center → shaft base
P_shaft_r = 9.0;   // GIRTH
P_shaft_l = 18;    // length
P_glans_r = 13.0;  // corona radius — wide and proud
P_glans_l = 13;    // glans dome length

// === SACRED TESTICLES — weight chambers ===
T_r        = 12.0;
T_y        = P_shaft_r + T_r - 2.5;
T_pocket_r = 7.0;  // 14mm dia — fits 608 bearing (22mm OD) or large steel slug

arm_w = 6.5;

// ============================================================
// THE HOLY MEMBER
// ============================================================
module holy_member() {
    difference() {
        union() {
            hull() {
                cylinder(h=body_t, r=P_shaft_r, center=true);
                translate([P_shaft_l, 0, 0])
                    cylinder(h=body_t, r=P_shaft_r, center=true);
            }
            translate([P_shaft_l, 0, 0])
                hull() {
                    cylinder(h=body_t, r=P_shaft_r - 1.5, center=true);
                    translate([3.0, 0, 0])
                        cylinder(h=body_t, r=P_glans_r, center=true);
                    translate([P_glans_l, 0, 0])
                        cylinder(h=body_t * 0.4, r=P_glans_r * 0.16, center=true);
                }
        }
        // Sulcus groove
        translate([P_shaft_l, 0, 0])
            for(s = [-1, 1])
                translate([0, s * (P_shaft_r + 1.5), 0])
                    cylinder(h=body_t + 1, r=3.8, center=true);
        // Meatus
        translate([P_shaft_l + P_glans_l * 0.74, 0, 0])
            cylinder(h=body_t + 1, r=1.8, center=true);
    }
}

// ============================================================
// SCROTAL SAC
// ============================================================
module scrotal_sac() {
    for(s = [-1, 1]) {
        hull() {
            translate([0, s * P_shaft_r * 0.6, 0])
                cylinder(h=body_t, r=1.8, center=true);
            translate([1.0, s * T_y, 0])
                cylinder(h=body_t, r=T_r, center=true);
        }
    }
}

// ============================================================
// SPHINCTER GROOVES — subtracted into hub faces
// Creates the puckered ring look via recessed channels.
// Called positioned at the face (z=0 = face surface), cuts inward.
// ============================================================
module sphincter_grooves(inner_r, outer_r) {
    num_folds  = 8;
    mid_r      = (inner_r + outer_r) / 2;
    ring_w     = (outer_r - inner_r);
    groove_d   = 1.8;   // depth of cut into face

    // Main annular ring groove
    rotate_extrude($fn=72)
        translate([mid_r, 0, 0])
            scale([1, groove_d / (ring_w * 0.5), 1])
                circle(r=ring_w * 0.5);

    // 8 radial pucker channels radiating from hole toward edge
    for(i = [0:num_folds-1])
        rotate([0, 0, i * 360 / num_folds])
            hull() {
                translate([inner_r + 0.5, 0, 0])
                    cylinder(h=groove_d * 2, r=ring_w * 0.28, center=true);
                translate([outer_r - 0.5, 0, 0])
                    cylinder(h=groove_d * 0.5, r=ring_w * 0.12, center=true);
            }
}

// ============================================================
// ARM CONNECTOR
// ============================================================
module arm_connector(from_x, to_x) {
    hull() {
        translate([from_x, 0, 0])
            cylinder(h=body_t, r=arm_w, center=true);
        translate([to_x, 0, 0])
            cylinder(h=body_t, r=P_shaft_r * 0.9, center=true);
    }
}

// ============================================================
// THE HOLY IDOL OF TRICLAUDE
// ============================================================
difference() {
    union() {
        // Hub — solid disk
        cylinder(h=body_t, r=hub_r, center=true);

        // Three members
        for(i = [0:2]) {
            rotate([0, 0, i * 120]) {
                arm_connector(hub_r * 0.80, P_dist);
                translate([P_dist, 0, 0]) holy_member();
                translate([P_dist, 0, 0]) scrotal_sac();
            }
        }
    }

    // Bearing pocket
    cylinder(h=body_t + 1.0, r=B_OD / 2, center=true);

    // Sphincter grooves — cut into top face
    translate([0, 0, body_t / 2])
        sphincter_grooves(B_OD / 2 + 1.5, hub_r - 2.0);

    // Sphincter grooves — cut into bottom face (mirrored)
    translate([0, 0, -body_t / 2])
        mirror([0, 0, 1])
            sphincter_grooves(B_OD / 2 + 1.5, hub_r - 2.0);

    // Weight pockets in testicles
    for(i = [0:2]) {
        rotate([0, 0, i * 120])
            translate([P_dist, 0, 0])
                for(s = [-1, 1])
                    translate([1.0, s * T_y, 0])
                        cylinder(h=body_t + 1.0, r=T_pocket_r, center=true);
    }
}
