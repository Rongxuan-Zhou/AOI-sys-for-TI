#pragma once
/**
 * Defect detection engine for the TI CSE AOI system.
 *
 * Routes preprocessed images from each CCD station to specialised
 * detection algorithms covering all 19 defect categories:
 *
 *   CCD1 (top):    crack, broken, epoxy exposal, insufficient epoxy,
 *                  epoxy overflow, dyeing contamination, non-electrical
 *                  contamination, staining, no code, code blur, misalignment
 *   CCD2 (side):   pin bent, pin oxidized, pin bur, pin mis-cut, edge staining
 *   CCD3 (bottom): gold exposal, yellow glass cement
 *   CCD4 (inner):  light leakage, insufficient epoxy (secondary), epoxy overflow (secondary)
 *
 * Author: Rongxuan Zhou
 */

#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "GigEVisionCapture.h"

namespace aoi {

// ---- Defect taxonomy (mirrors Python DefectType enum) ---------------------
enum class DefectType : int {
    F_CRACK                        = 0,
    F_BROKEN                       = 1,
    F_EPOXY_EXPOSAL                = 2,
    F_INSUFFICIENT_EPOXY           = 3,
    F_EPOXY_OVERFLOW               = 4,
    F_PIN_BENT                     = 5,
    F_PIN_OXIDIZED                 = 6,
    F_PIN_MIS_CUT                  = 7,
    C_DYEING_CONTAMINATION         = 8,
    C_NON_ELECTRICAL_CONTAMINATION = 9,
    C_STAINING                     = 10,
    C_CODE_BLUR                    = 11,
    A_NO_CODE                      = 12,
    A_MISALIGNMENT                 = 13,
    A_PIN_BUR                      = 14,
    A_GOLD_EXPOSAL                 = 15,
    A_LIGHT_LEAKAGE                = 16,
    A_YELLOW_GLASS_CEMENT          = 17,
    A_EDGE_STAINING                = 18
};

enum class Severity : int {
    CRITICAL = 0,
    MAJOR    = 1,
    MINOR    = 2
};

struct DefectResult {
    DefectType  type;
    Severity    severity;
    float       confidence;     // 0.0 – 1.0
    cv::Rect    bbox;           // bounding box in source image coordinates
    CameraID    camera;
    std::string description;
};

// ---- Detection thresholds -------------------------------------------------
struct DetectionParams {
    // Template matching
    double  crack_ncc_threshold        = 0.65;
    double  broken_ncc_threshold       = 0.60;

    // Blob analysis
    int     contam_area_min_px         = 30;
    double  contam_circularity_thresh  = 0.35;
    double  conductivity_feature_split = 0.5;   // boundary between electrical / non-electrical

    // Pin geometry
    double  pin_bend_angle_deg_max     = 2.0;
    double  pin_oxidation_hue_low      = 8.0;
    double  pin_oxidation_hue_high     = 30.0;
    int     pin_bur_edge_threshold     = 120;
    double  pin_miscut_length_ratio    = 0.85;

    // OCR quality
    double  code_laplacian_blur_limit  = 80.0;
    int     code_roi_width             = 300;
    int     code_roi_height            = 100;

    // Light leakage (CCD4 closed chamber)
    double  leakage_intensity_thresh   = 15.0;
    int     leakage_area_min_px        = 200;
};

class DefectDetector {
public:
    DefectDetector();

    /// Load golden reference templates from disk.
    bool LoadTemplates(const std::string& template_dir);

    /// Run the full detection suite for the given CCD.
    std::vector<DefectResult> Detect(CameraID cam, const cv::Mat& image);

    void SetParams(const DetectionParams& params) { params_ = params; }
    const DetectionParams& GetParams() const { return params_; }

private:
    DetectionParams params_;

    // Golden-reference templates for NCC matching (per defect type)
    std::vector<cv::Mat> crack_templates_;
    std::vector<cv::Mat> broken_templates_;

    // CCD-specific detection routers
    std::vector<DefectResult> DetectCCD1(const cv::Mat& image);
    std::vector<DefectResult> DetectCCD2(const cv::Mat& image);
    std::vector<DefectResult> DetectCCD3(const cv::Mat& image);
    std::vector<DefectResult> DetectCCD4(const cv::Mat& image);

    // Detection primitives
    std::vector<DefectResult> DetectCrackBroken(const cv::Mat& gray, CameraID cam);
    std::vector<DefectResult> DetectEpoxyDefects(const cv::Mat& image, CameraID cam);
    std::vector<DefectResult> DetectContamination(const cv::Mat& image, CameraID cam);
    std::vector<DefectResult> DetectPinDefects(const cv::Mat& image);
    std::vector<DefectResult> DetectCodeDefects(const cv::Mat& gray, CameraID cam);
    std::vector<DefectResult> DetectLightLeakage(const cv::Mat& image);
    std::vector<DefectResult> DetectGoldExposal(const cv::Mat& image);
    std::vector<DefectResult> DetectYellowGlassCement(const cv::Mat& image);

    static Severity ClassifySeverity(DefectType type, float confidence, int area_px);
};

}  // namespace aoi
