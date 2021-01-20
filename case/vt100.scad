//VT102 scale model, top+bottom half + fastener locations to hold them together, plus
//PCB and LCD mounts.
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

use <threadlib/threadlib.scad>

pix_scale=13; //this many pixels in one mm
$fs=0.3;

//in pixels
screen_slot_size=10;
vent_slot_size=6;
vent_slot_off=12;
slot_depth=15;
hull_thickness_mm=2.5;
hull_thickness=hull_thickness_mm*pix_scale;
lip_height=3;
show_lcd=0;

faceplate_angle=9;

/* Set this to 1 for some things that take long to render (specifically the ventilation gratings and
the DEC logo badge) */
fast_render=0;

/* What to render. Use values from 1 to 7 to select a view. For 3d printing, you want to
render 3 (bottom half) and 4 (top half). */
render_tp=3;

if (render_tp==1 || render_tp==2) {
    pcb();
    intersection() {
        union() {
            vt100_model_top();
        }
        if (render_tp==1) {
            translate([-500, 0, -500]) cube([1000,1000,1000]);
        } else {
            translate([25, -500, -500]) cube([1000,1000,1000]);
        }
    }
} else if (render_tp==3 || render_tp==4 || render_tp==5) {
    intersection() {
        union() {
            if (render_tp==3 || render_tp==5) {
                vt100_model_bottom();
            }
            if (render_tp==4 || render_tp==5) {
                rotate([0,180,0])  vt100_model_top();
            }
        }
        //translate([-500, 0, -500]) cube([1000,1000,1000]);
    }
} else if (render_tp==6) {
    pcb();
} else if (render_tp==7) {
    switch();
}


//Cutout for power switch
module switch() {
    translate([3.25, 0, 1.4]) union() {
        translate([1.5, 0, 0]) cube([2.8+3, 6.8, 1.5], center=true);
        translate([-2, 0, 0])cube([4, 3.2, 1.2], center=true);
    }
}

//PCB. Make ext 1 to also include room where plug/card goes in.
module pcb(ext) {
    exsz=ext?10:0;
    color([0.2,0.9,0]) translate([9,28,-13.5]) {
        union() {
            rotate([90,0,0]) linear_extrude(height=1.7) polygon(points=[
                [-2,13.5],[-1.3,0],[41,0],[41,49],[0,43]
            ]);
            translate([14.3,0,13.8]) cube([31.6,1,18]);
            translate([-1.5,-2.8-1.6,2.7]) rotate([0, -3, 0])translate([-exsz,0,0]) cube([exsz+6.5, 2.8, 8]);
            translate([-1.7,-2.5-1.6,19.3]) rotate([0, 4, 0])translate([-exsz,0,0]) cube([exsz+17.7, 2.5, 14]);
        }
    }
}


module lcd_placeholder_thingies() {
    scale(1/pix_scale) union() {
        translate([590 ,0, 455]) rotate([0,5,0]) cube([80,50,30]);
        translate([590 ,-250, 455]) rotate([0,5,0]) cube([80,50,30]);
        translate([700 ,-350, -20]) cube([70,500,50]);
    }
}

module pcb_placeholder_thingies() {
    union() {
        translate([7, 24, -13.5-1]) cube([10, 5.6, 3.5]);
        translate([8.5, 24, 27]) rotate([0,-9,0]) cube([10, 5.6, 4.5]);
        translate([47.3, 24, 31]) rotate([0,-9,0]) cube([6, 5.6, 4.5]);
    }
}


module vt100_model_top() {
    difference() {
        vt100_model();
        lip_diff();
    }
}


//inside this = bottom, outside = top
module lip_diff() {
    union() {
        translate([0, -500, -1000]) cube([1000,1000,1000]);
        difference() {
            scale(1/pix_scale) translate([55+hull_thickness/2,-922/2+hull_thickness/2,0])cube([736-hull_thickness,922-hull_thickness,lip_height*pix_scale]);
            scale(1/pix_scale) translate([200,-380,0])cube([400-hull_thickness,800-hull_thickness,lip_height*pix_scale]);
        }
    }
}

module vt100_model_bottom() {
    lhs=lip_height*pix_scale;
    intersection() {
        vt100_model();
        lip_diff();
    }
}

module screwpost() {
    translate([25,0,-16]) {
        cylinder(49.5, d=5.5+4);
    }
}

module screwpost_inside() {
    translate([25,0,-50-1.5]) {
        cylinder(50, d=5.5);
        cylinder(50+1.51, d=3);
        translate([0,0,51.5]) cylinder(0.2, d=5); //compensate for build plate building artifact top
        translate([0,0,35]) cylinder(0.5, d=8); //compensate for build plate building artifact bot
        translate([0,0,50+9]) rotate([180,0,0]) tap("M3", turns=15);
    }
}

module vt100_model() {
    render(8) difference() {
        union() {
            scale(1/pix_scale) vt100_hull();
            lcd_placeholder_thingies();
            pcb_placeholder_thingies();
            translate([0,18,0]) screwpost();
            translate([0,-18,0]) screwpost();
            translate([4+hull_thickness_mm,-33,hull_thickness_mm-16]) cube([6.5,12, 2]);
        }
        translate([0,18,0]) screwpost_inside();
        translate([0,-18,0]) screwpost_inside();
        translate([1.5+hull_thickness_mm, -33+6, hull_thickness_mm-15.2]) switch();
        lcd();
        pcb(1);
        translate([0, 19, -12]) cube([5.5, 12, 12]);
    }

}

module lcd() {
   if (show_lcd == 1) {
    //sizes are from KD018QVFMN010 specsheet
    tol=0.25; //tolerance on outside dimensions
    translate([721/pix_scale, -400/pix_scale,23/pix_scale]) rotate([-90-faceplate_angle,180, -90]) union() {
        translate([-tol/2,-tol/2,-tol/2]) cube([46.7+tol, 34.7+tol, 2.50+tol]); //display itself
        %translate([3.2, 4.03, 0]) cube([35.52, 26.64, 2.50+tol+0.1]); //visible area, stick out a bit
    }
   }
}



module rounded_cube(c, s) {
    hull() {
        translate([s,s,s]) sphere(r=s);
        translate([c[0]-s,s,s]) sphere(r=s);
        translate([s,c[1]-s,s]) sphere(r=s);
        translate([c[0]-s,c[1]-s,s]) sphere(r=s);
        translate([s,s,c[2]-s]) sphere(r=s);
        translate([c[0]-s,s,c[2]-s]) sphere(r=s);
        translate([s,c[1]-s,c[2]-s]) sphere(r=s);
        translate([c[0]-s,c[1]-s,c[2]-s]) sphere(r=s);
    }
}

//black rounded cosmetical cube thing on the case the screen is located in
module vt100_screencube(m) {
    translate([376+m,-408+m,43+m])rounded_cube([500-m*2,593-m*2,500-m*2],40-m);
}


module vt100_hull() {
    difference() {
        vt100_case_w_screen();
        vt100_case_solid(hull_thickness);
    }
}


module vt100_case_w_screen() {
    difference() {
        vt100_case_outer();
        union() {
            translate([784-40,0,-15]) rotate([0,-faceplate_angle,0])rotate([90,0,90]) hull() {
                translate([-358,106,0]) cylinder(50,r1=0,r2=30);
                translate([132,106,0]) cylinder(50,r1=0,r2=30);
                translate([-358,422,0]) cylinder(50,r1=0,r2=30);
                translate([132,422,0]) cylinder(50,r1=0,r2=30);
            }
        }
    }
}


module vt100_case_outer() {
    union() {
        difference() {
            if (fast_render) {
                vt100_case_solid(0);
            } else {
                minkowski(){
                    vt100_case_solid(8);
                    sphere(d=8);
                }
            }
            union() {
                difference() {
                    vt100_screencube(0);
                    vt100_screencube(screen_slot_size);
                }
                if (!fast_render) {
                    for (i=[0:vent_slot_off:480]) { //side vents
                        translate([157+i, 215, 200]) cube([vent_slot_size, 180, 500]);
                    }
                    for (i=[0:vent_slot_off:580]) { //back vents
                        translate([242-385, -405+i, 300]) cube([300, vent_slot_size, 500]);
                    }
                }
            }
        }
        vt100_case_solid(slot_depth);
        if (!fast_render) {
            translate([740+8, 242, 100]) rotate([90-faceplate_angle,0,90]) scale(0.18) linear_extrude(height=30/0.18, convexity=10) {
                import("digital_badge.svg", convexity=10);
            }
        }
    }
}

/*

  A--------G
  /        |
 |         |
 B         |
 |    EE   |
  \      E-F
  C------D 

  X ->  Y V
*/



module vt100_case_solid(inset) {
    A=[120,150];
    B=[99,722];
    C=[56,545];
    D=[650,722];
    E=[730,628];
    EE=[657,556];
    F=[755,550];
    G=[655,112];

    mirror([0,0,1])intersection() {
        //Front/back/top/bottom of case
        rotate([90,0,0]) translate([0,-545,-500]) scale([1,1,1000]) difference() {
            union() {
                hull() {
                    translate(A) cylinder(1, d=71-inset*2);
                    translate(B) cylinder(1, d=71-inset*2);
                    translate([inset, 0, 0]) translate(C) cylinder(1, d=1);
                    translate(D) cylinder(1, d=71-inset*2);
                    translate(EE) cylinder(1, d=71-inset*2);
                }                
                hull() {
                    translate(EE) cylinder(1, d=71-inset*2);
                    translate(F) cylinder(1, d=71-inset*2);
                    translate(G) cylinder(1, d=128-inset*2);
                    translate(A) cylinder(1, d=71-inset*2);
                }
                hull() {
                    translate(EE) cylinder(1, d=0.001+inset*2);
                    translate(E) cylinder(1, d=77+inset*2);
                }
            }
            translate(E) translate([0,0,-0.5]) cylinder(2, d=82+inset*2);
        }
        union() {
            //Sides of the case
            translate([0,461-inset,0]) rotate([180-3.5,0,0]) cube([1000,461,600]);
            mirror([0,1,0]) translate([0,461-inset,0]) rotate([180-3.5,0,0]) cube([1000,461,600]);
            translate([0,-461+inset,0]) rotate([-4,0,0]) cube([1000,461,600]);
            mirror([0,1,0]) translate([0,-461+inset,0]) rotate([-4,0,0]) cube([1000,461,600]);
        }
    }
}

