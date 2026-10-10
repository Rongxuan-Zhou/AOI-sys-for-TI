#pragma once
/**
 * Defect detection engine for the TI CSE AOI system (spec V1.4).
 *
 * Routes one unit's preprocessed frame from a CCD station to the detectors
 * that can see the defect on that view (spec slide 9 and the sample images
 * on slides 30-45):
 *
 *   CCD1 (top):    crack, broken, misalignment, pin missing, insufficient
 *                  epoxy, epoxy overflow, epoxy exposal, dyeing / electrical /
 *                  non-electrical contamination, no code, code blur
 *   CCD2 (side):   pin bent / oxidized / bur / mis-cut, gold exposal,
 *                  epoxy higher than ceramic
 *   CCD3 (bottom): crack, broken, misalignment, dyeing / electrical /
 *                  non-electrical contamination
 *   CCD4 (inner):  lighting check, staining on edge, yellow glass cement,
 *                  misalignment against the nest ring
 *
 * Every threshold is a DetectionParams field. Header defaults are tuned
 * for the synthetic test frames (640-800 px); the per-station YAML carries
 * the production values for 2448x2048 / 5472x3648 frames.
 *
 * Author: Rongxuan Zhou
 */

#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "CameraID.h"
#include "CseLocator.h"
#include "DefectTypes.h"
#include "LightingCheck.h"

namespace aoi {

struct DetectionParams {
    LocatorParams  locator;
    LightingParams lighting;

    // geometry
    double      mm_per_px_ccd123 = 0.0115;
    cv::Point2f fixture_center_ccd1{-1.f, -1.f};   // (-1,-1) = image centre
    cv::Point2f fixture_center_ccd3{-1.f, -1.f};
    double      misalign_max_offset_mm = 0.15;
    int         expected_pin_count = 4;

    // crack / broken
    int    crack_blackhat_kernel_px = 15;
    int    crack_blackhat_thresh = 25;
    double crack_aspect_min = 4.0;
    int    crack_min_length_px = 30;
    float  broken_depth_px = 6.f;

    // top pins and epoxy (CCD1)
    int    pin_head_dark_thresh = 110;
    float  pin_head_radius_px = 18.f;
    float  epoxy_ring_px = 10.f;
    int    pin_core_val_min = 170;
    int    pin_core_area_min_px = 20;
    int    epoxy_sat_min = 50;
    int    epoxy_val_max = 160;
    double epoxy_ring_min_fill = 0.6;
    double epoxy_overflow_ratio = 2.5;      // x nominal ring area
    int    epoxy_hue_low = 8;
    int    epoxy_hue_high = 30;
    int    exposal_sat_max = 40;
    double exposal_texture_min = 18.0;
    int    exposal_area_min_px = 300;

    // contamination (CCD1, CCD3)
    int    contam_area_min_px = 40;
    int    dyeing_sat_min = 70;
    int    electrical_dark_max = 120;
    double electrical_texture_min = 20.0;

    // marking code (CCD1)
    int    code_roi_w = 260;
    int    code_roi_h = 90;
    int    code_offset_px = 30;             // along the notch direction from the centre
    int    code_ink_contrast = 30;          // ink = grey < ROI mean - contrast
    double code_min_ink_ratio = 0.03;
    double code_laplacian_blur_limit = 80.0;

    // side view (CCD2)
    bool   pins_up = true;                  // machine orientation (slide 9); false for bench images
    int    side_fg_thresh = 40;
    int    pin_w_max = 14;
    double pin_bend_angle_deg_max = 2.0;
    int    pin_length_px = 120;
    int    pin_length_tol_px = 15;
    int    pin_bur_width_px = 4;
    int    pin_oxide_val_max = 120;
    int    gold_area_min_px = 60;
    int    epoxy_bump_px = 5;
    int    epoxy_bump_width_px = 8;
};

class DefectDetector {
public:
    DefectDetector();
    explicit DefectDetector(const DetectionParams& params);

    /// Run the detection suite for one unit's frame from the given station.
    /// Never throws. A frame without a locatable disc yields exactly one
    /// result: PrimaryTypeOf(cam) with description "disc not located".
    std::vector<DefectResult> Detect(CameraID cam, const cv::Mat& image);

    void SetParams(const DetectionParams& params);
    const DetectionParams& GetParams() const { return params_; }

    /// Defect reported when the disc cannot be located on this station.
    static DefectType   PrimaryTypeOf(CameraID cam);
    static DefectResult NotLocated(CameraID cam);

private:
    DetectionParams params_;
    CseLocator      locator_;
    LightingCheck   lighting_;

    // per-station routers
    std::vector<DefectResult> DetectCCD1(const cv::Mat& bgr);
    std::vector<DefectResult> DetectCCD2(const cv::Mat& bgr);
    std::vector<DefectResult> DetectCCD3(const cv::Mat& bgr);
    std::vector<DefectResult> DetectCCD4(const cv::Mat& bgr);

    // ceramic (CCD1 / CCD3)
    std::vector<DefectResult> DetectCrack(const cv::Mat& gray, const CseGeometry& g,
                                          const cv::Mat& exclude_mask, CameraID cam);
    std::vector<DefectResult> DetectBroken(const CseGeometry& g, CameraID cam);
    std::vector<DefectResult> DetectMisalignment(const CseGeometry& g, cv::Size frame, CameraID cam);
    std::vector<DefectResult> DetectContamination(const cv::Mat& bgr, const CseGeometry& g,
                                                  const cv::Mat& exclude_mask, CameraID cam);
    // top side (CCD1)
    std::vector<DefectResult> DetectPinsTop(const cv::Mat& bgr, const CseGeometry& g,
                                            cv::Mat& pin_mask_out);
    std::vector<DefectResult> DetectEpoxyExposal(const cv::Mat& bgr, const CseGeometry& g,
                                                 const cv::Mat& exclude_mask);
    std::vector<DefectResult> DetectCode(const cv::Mat& bgr, const CseGeometry& g,
                                         cv::Mat& code_mask_out);
    // side view (CCD2)
    std::vector<DefectResult> DetectPinsSide(const cv::Mat& bgr, const cv::Mat& fg,
                                             const cv::Rect& body);
    std::vector<DefectResult> DetectGoldExposal(const cv::Mat& bgr, const cv::Rect& body);
    std::vector<DefectResult> DetectEpoxyBump(const cv::Mat& fg, const cv::Rect& body);

    static DefectResult Make(DefectType t, float conf, cv::Rect bb, CameraID cam,
                             const std::string& desc);
};

}  // namespace aoi
