#pragma once
/**
 * Defect taxonomy for the TI CSE AOI system — spec V1.4, slide 8.
 *
 * 19 defect items in the spec's order and grouping, plus the two
 * "Inner Staining Residue" sub-checks of the lighting check (slide 8,
 * right-hand table). Critical items (purple on slide 8): Lighting Check,
 * Crack, Broken, Misalignment.
 *
 * Author: Rongxuan Zhou
 */

#include <string>

#include <opencv2/core.hpp>

#include "CameraID.h"

namespace aoi {

enum class DefectGroup : int {
    FUNCTION      = 0,
    COSMETIC      = 1,
    ASSEMBLY      = 2,
    INNER_RESIDUE = 3
};

enum class DefectType : int {
    // ---- Function (slide 8, items 1-9) ----
    LIGHTING_CHECK               = 0,
    CRACK                        = 1,
    BROKEN                       = 2,
    EPOXY_EXPOSAL                = 3,
    PIN_MISSING                  = 4,
    ELECTRICAL_CONTAMINATION     = 5,
    GOLD_EXPOSAL                 = 6,
    INSUFFICIENT_EPOXY           = 7,
    EPOXY_OVERFLOW               = 8,
    // ---- Cosmetic (items 10-13) ----
    DYEING_CONTAMINATION         = 9,
    NON_ELECTRICAL_CONTAMINATION = 10,
    NO_CODE                      = 11,
    CODE_BLUR                    = 12,
    // ---- Assembly (items 14-19) ----
    PIN_BENT                     = 13,
    PIN_OXIDIZED                 = 14,
    PIN_BUR                      = 15,
    PIN_MIS_CUT                  = 16,
    EPOXY_HIGHER_THAN_CERAMIC    = 17,
    MISALIGNMENT                 = 18,
    // ---- Inner staining residue (lighting-check sub-checks) ----
    STAINING_ON_EDGE             = 19,
    YELLOW_GLASS_CEMENT          = 20,

    COUNT                        = 21
};

enum class Severity : int {
    CRITICAL = 0,
    MAJOR    = 1,
    MINOR    = 2
};

DefectGroup GroupOf(DefectType t);
Severity    SeverityOf(DefectType t);
/// snake_case name used by the YAML configs and the PLC DEFECT_CODE table.
const char* ToString(DefectType t);

struct DefectResult {
    DefectType  type;
    Severity    severity;
    float       confidence;     // 0.0 - 1.0
    cv::Rect    bbox;           // bounding box in source image coordinates
    CameraID    camera;
    std::string description;
};

}  // namespace aoi
