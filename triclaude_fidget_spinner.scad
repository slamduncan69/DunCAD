// Fidget Spinner — 608 Skateboard Bearings (22mm OD, 8mm ID, 7mm wide)
// 3-lobe design: 1 center + 2 wing bearings at 120°

$fn = 100;
$fa = 1;
$fs = 0.4;

// ── 608 Bearing dimensions ───────────────────────────────────────────────────
bearing_od      = 22;      // outer diameter
bearing_id      = 8;       // inner diameter (axle hole)
bearing_h       = 7;       // height/width
pocket_od       = 22.4;    // press-fit pocket: slight clearance
pocket_id       = 7.6;     // axle hole: slight undersize for tight fit
lip_h           = 1.0;     // retaining lip above/below bearing
total_h         = bearing_h + (lip_h * 2);  // overall spinner thickness

// ── Layout ───────────────────────────────────────────────────────────────────
arm_length      = 42;      // center-to-center distance (mm)
lobe_r          = bearing_od / 2 + 4;   // lobe hull circle radius
arm_r           = 6;                     // arm neck radius

// Wing bearing positions (120° apart, one at top)
wing_angles     = [90, 210, 330];

module bearing_positions() {
    translate([0, 0, 0]) children(0);  // center
    for (a = wing_angles)
        translate([arm_length * cos(a), arm_length * sin(a), 0]) children(0);
}

// ── Spinner body (hull of lobes) ─────────────────────────────────────────────
module spinner_body() {
    hull() {
        // center lobe
        cylinder(r = lobe_r, h = total_h);
        // wing lobes
        for (a = wing_angles)
            translate([arm_length * cos(a), arm_length * sin(a), 0])
                cylinder(r = lobe_r, h = total_h);
    }
}

// ── Bearing pocket (recessed from both sides, bearing snaps in) ───────────────
module bearing_pocket() {
    // center through-hole for axle
    translate([0, 0, -0.1])
        cylinder(d = pocket_id, h = total_h + 0.2);

    // top pocket (bearing sits flush / 0.5mm below surface)
    translate([0, 0, total_h - lip_h - bearing_h])
        cylinder(d = pocket_od, h = bearing_h + 0.1);
}

// ── Weight relief cutouts between lobes (reduce mass, look cool) ─────────────
module arm_cutout(a) {
    mid_x = arm_length/2 * cos(a);
    mid_y = arm_length/2 * sin(a);
    // teardrop-ish slot along the arm
    hull() {
        translate([mid_x + arm_r*0.6*cos(a), mid_y + arm_r*0.6*sin(a), -0.1])
            cylinder(r = arm_r, h = total_h + 0.2);
        translate([mid_x - arm_r*0.6*cos(a), mid_y - arm_r*0.6*sin(a), -0.1])
            cylinder(r = arm_r, h = total_h + 0.2);
    }
}

// ── Assembly ──────────────────────────────────────────────────────────────────
difference() {
    spinner_body();

    // bearing pockets at all 3 positions
    translate([0, 0, 0])
        bearing_pocket();
    for (a = wing_angles)
        translate([arm_length * cos(a), arm_length * sin(a), 0])
            bearing_pocket();

    // weight relief slots in each arm
    for (a = wing_angles)
        arm_cutout(a);
}
