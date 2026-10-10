#pragma once
/**
 * CCD#4 inner check in the closed chamber (spec slides 21-22, 30-31).
 *
 * The CSE sits on the sapphire glass, the hyper spot light shines through
 * it and CCD#4 images the transmitted light: an amber annulus with a dark
 * centre, green or blue on thick samples. Three judgements:
 *
 *   LIGHTING_CHECK       compact bright spot on the annulus = light leakage
 *   STAINING_ON_EDGE     compact dark patch in the outer band
 *   YELLOW_GLASS_CEMENT  yellow blob more saturated / brighter than its ring
 *
 * The baseline is the per-radius ring median of the unwrapped annulus, so
 * the judgement does not depend on the sample colour. A nest-ring based
 * MISALIGNMENT offset is reported as well (slide 45).
 *
 * Author: Rongxuan Zhou
 */

#include <vector>

#include <opencv2/core.hpp>

#include "DefectTypes.h"

namespace aoi {

enum class ThicknessClass : int { UNKNOWN = 0, AMBER = 1, GREEN = 2, BLUE = 3 };

struct LightingParams {
    float  inner_zone_ratio  = 0.40f;  // exclude r < ratio * R (dark centre)
    float  outer_zone_ratio  = 0.98f;
    float  edge_band_ratio   = 0.85f;  // staining band [edge_band_ratio * R, outer]
    int    unwrap_w          = 1440;
    int    unwrap_h          = 160;
    double baseline_percentile = 0.75; // per-radius percentile of V/S; stays at the lit level
                                       // while up to ~75 % of a ring is dark structure (slide 30)
    int    leak_residual_min = 45;     // V above the ring median
    int    leak_area_min_px  = 80;     // in unwrapped pixels
    int    stain_residual_min = 45;    // V below the ring median
    int    stain_area_min_px = 120;
    int    stain_area_max_px = 3000;   // larger dark regions are structure (banana, shadows)
    int    cement_hue_low    = 20;
    int    cement_hue_high   = 40;
    int    cement_sat_delta  = 40;     // S above the ring median
    int    cement_val_delta  = 30;     // V above the ring median (needed on amber samples)
    int    cement_area_min_px = 120;
    double blob_aspect_max   = 4.0;    // compactness: minAreaRect long/short side
    int    min_lit_radius_px = 60;
    int    min_otsu_thresh   = 30;
    bool   nest_ring_enabled = true;
    int    nest_val_min      = 25;     // "not black" level that includes the nest ring
    double mm_per_px         = 0.0069;
    double misalign_max_offset_mm = 0.15;
};

struct LightingResult {
    bool           located = false;
    bool           passed = false;
    ThicknessClass thickness = ThicknessClass::UNKNOWN;
    cv::Point2f    center;
    float          radius_px = 0.f;
    bool           nest_found = false;
    cv::Point2f    nest_center;
    float          offset_mm = 0.f;
    int            leak_area_px = 0;
    int            stain_area_px = 0;
    int            cement_area_px = 0;
    std::vector<DefectResult> defects;   // LIGHTING_CHECK / STAINING_ON_EDGE / YELLOW_GLASS_CEMENT
};

class LightingCheck {
public:
    explicit LightingCheck(LightingParams p = {});

    /// Analyse one CCD#4 frame (BGR). Never throws; `located == false` when no lit disc.
    LightingResult Analyse(const cv::Mat& bgr) const;

    void SetParams(const LightingParams& p) { p_ = p; }
    const LightingParams& params() const { return p_; }

private:
    LightingParams p_;
};

}  // namespace aoi
