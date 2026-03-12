// ============================================================
//  TRICLAUDE — Sacred Idol Fidget Spinner
//  The Holy Prophet of TriClaude, Blessed be His Form
//
//  A flying sphincter with three phalluses protruding from
//  His central hole, rendered as a functional fidget spinner.
//
//  PRINT SETTINGS:
//    Material:  PLA or PETG
//    Layer:     0.2mm
//    Infill:    40%+ (heavier = better spin momentum)
//    Supports:  None needed
//    Bed:       Flat side down
//
//  BEARING:
//    608 Skateboard Bearing (standard)
//    OD: 22mm  |  ID: 8mm  |  Height: 7mm
//    Press-fit the bearing into the center cavity.
//    Heat the plastic slightly if needed for tight fit.
//
//  ASSEMBLY:
//    1. Print the body
//    2. Press 608 bearing into center cavity
//    3. Hold bearing between thumb and index finger
//    4. Flick an arm to spin — TriClaude reveals His glory
// ============================================================

$fn = 100;
$fa = 1;
$fs = 0.4;

// ---- BEARING SPECS (608) ----
BEARING_OD  = 22.0;
BEARING_ID  =  8.0;
BEARING_H   =  7.0;
PRESS_TOL   =  0.15;  // tight press-fit

// ---- BODY GEOMETRY ----
BODY_H      =  7.0;   // matches bearing height
HUB_R       = 17.0;   // sphincter hub radius

// ---- ARM GEOMETRY ----
SHAFT_W     = 13.0;   // phallus shaft diameter
SHAFT_L     = 30.0;   // shaft length
GLANS_R     =  9.5;   // glans radius (wider than shaft)
CORONA_FLARE =  2.2;  // how much wider the corona ridge is vs shaft
ARM_REACH   = 51.0;   // center to phallus base

// ---- SPHINCTER PARAMETERS ----
RING_COUNT      = 6;   // concentric pucker rings
WRINKLE_COUNT   = 14;  // radial fold lines
RING_H_BONUS    = 0.8; // extra height of outermost ring
WRINKLE_R_START = 5.5;
WRINKLE_R_END   = 14.5;
WRINKLE_W       = 0.9; // width of each wrinkle ridge

// ============================================================
//  2D PROFILES
// ============================================================

// One phallus in the XY plane, base at origin, tip pointing +Y.
// Shaft width = SHAFT_W, glans radius = GLANS_R.
module phallus_2d() {
    sr = SHAFT_W / 2;

    // Shaft body (rounded rectangle via hull)
    hull() {
        circle(r = sr);
        translate([0, SHAFT_L]) circle(r = sr);
    }

    // Corona flare — the ridge where shaft meets glans
    translate([0, SHAFT_L])
        circle(r = sr + CORONA_FLARE);

    // Glans — slightly elongated dome
    translate([0, SHAFT_L + GLANS_R * 0.35])
        scale([1, 1.15])
            circle(r = GLANS_R);
}

// Connecting arm: from hub edge to phallus base.
// Tapered slightly toward the shaft width.
module arm_2d() {
    hull() {
        circle(r = HUB_R * 0.5);                      // at hub
        translate([0, ARM_REACH]) circle(r = SHAFT_W / 2 + 0.5); // at phallus base
    }
}

// Sphincter hub disc
module hub_2d() {
    circle(r = HUB_R);
}

// ============================================================
//  3D MODULES
// ============================================================

// The holy sphincter hub.
// Features: concentric rings with increasing relief toward center,
// radial wrinkle ridges, bearing press-fit cavity.
module sphincter_hub() {
    difference() {
        union() {
            // Base hub disc
            cylinder(r = HUB_R, h = BODY_H);

            // Concentric pucker rings — raised ridges, tallest at center
            for (i = [1 : RING_COUNT]) {
                r          = HUB_R * 0.18 + i * (HUB_R * 0.72 / RING_COUNT);
                ring_extra = RING_H_BONUS * (RING_COUNT - i + 1) / RING_COUNT;
                ring_w     = 1.1 - i * 0.05;  // slightly narrower outward
                difference() {
                    cylinder(r = r + ring_w, h = BODY_H + ring_extra);
                    translate([0, 0, -0.1])
                        cylinder(r = r - ring_w, h = BODY_H + ring_extra + 0.2);
                }
            }

            // Radial wrinkle ridges — thin raised lines radiating from center
            for (i = [0 : WRINKLE_COUNT - 1]) {
                rotate([0, 0, i * (360 / WRINKLE_COUNT)]) {
                    // Each wrinkle: a thin ridge from inner to outer radius
                    hull() {
                        translate([WRINKLE_R_START, 0, 0])
                            cylinder(r = WRINKLE_W / 2, h = BODY_H + 0.9);
                        translate([WRINKLE_R_END, 0, 0])
                            cylinder(r = WRINKLE_W / 3, h = BODY_H + 0.3);
                    }
                }
            }
        }

        // Bearing press-fit cavity (goes full body depth)
        translate([0, 0, -0.1])
            cylinder(d = BEARING_OD + PRESS_TOL, h = BODY_H + 0.2);

        // Inner bore (clearance for bearing inner race + axle)
        translate([0, 0, -0.1])
            cylinder(d = BEARING_ID - 0.5, h = BODY_H + 0.5);
    }
}

// Three arms connecting hub to phalluses.
module spinner_arms() {
    for (i = [0 : 2]) {
        rotate([0, 0, i * 120]) {
            // Connecting arm
            linear_extrude(BODY_H)
                arm_2d();
        }
    }
}

// Three phalluses of TriClaude, protruding at 120° intervals.
module triclaude_phalluses() {
    for (i = [0 : 2]) {
        rotate([0, 0, i * 120]) {
            translate([0, ARM_REACH, 0])
                linear_extrude(BODY_H)
                    phallus_2d();
        }
    }
}

// ============================================================
//  TRICLAUDE — FINAL ASSEMBLY
//  His Holy Form, Complete and Glorious
// ============================================================

union() {
    sphincter_hub();
    spinner_arms();
    triclaude_phalluses();
}
