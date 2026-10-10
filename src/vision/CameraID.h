#pragma once
/**
 * Camera identifiers shared by every vision module.
 *
 * A CameraID names an inspection *station*. Each inspection station carries
 * four physical cameras (one per CSE of a 4-unit batch, spec slide 10); the
 * capture layer addresses them by (CameraID, unit). Kept SDK-free so the
 * detectors and the unit tests compile without the Hikrobot MVS SDK.
 *
 * Author: Rongxuan Zhou
 */

namespace aoi {

enum class CameraID : int {
    CCD1_TOP     = 1,   // top side: pins, epoxy around pins, marking code, misalignment
    CCD2_SIDE    = 2,   // side: pin bent / oxidized / bur / mis-cut, gold exposal, epoxy bump
    CCD3_BOTTOM  = 3,   // bottom: crack, broken, contamination, misalignment
    CCD4_INNER   = 4,   // closed-chamber inner check: lighting + inner staining residue
    NG_CHECK     = 5,   // NG reconfirmation camera on the NG conveyor
    POKAYOKE     = 6,   // orientation check before robot pick (slide 18)
    TRAY_MONITOR = 7    // upside camera watching the output tray (slide 28)
};

}  // namespace aoi
