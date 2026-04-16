#pragma once
/**
 * Image preprocessing pipeline for the TI CSE AOI system.
 *
 * Handles CCD-specific corrections that must run before defect detection:
 *   - Bayer demosaicing (BayerRG for GE501GC, BayerGB for GE2000C)
 *   - Flat-field correction from calibration reference images
 *   - CLAHE adaptive contrast enhancement with per-CCD clip limits
 *   - Sapphire glass chromatic aberration compensation (CCD4)
 *   - Cylindrical unwrap for 360-degree side inspection (CCD2)
 *
 * Author: Rongxuan Zhou
 */

#include <string>
#include <unordered_map>
#include <vector>

#include <opencv2/core.hpp>

#include "GigEVisionCapture.h"

namespace aoi {

struct PreprocessConfig {
    double  clahe_clip_limit  = 3.0;
    int     clahe_grid_size   = 8;
    bool    apply_flatfield   = true;
    bool    apply_chromatic_correction = false;  // CCD4 only
};

class ImagePreprocessor {
public:
    ImagePreprocessor();

    /// Load flat-field reference and chromatic LUT from calibration directory.
    bool LoadCalibration(const std::string& calib_dir);

    /// Full preprocessing pipeline: demosaic → flatfield → CLAHE → CCD-specific.
    cv::Mat Process(CameraID cam, const cv::Mat& raw);

    void SetConfig(CameraID cam, const PreprocessConfig& cfg);

    /// Extract aligned ROI with sub-pixel interpolation.
    static cv::Mat ExtractROI(const cv::Mat& image, cv::Rect2f roi,
                              float angle_deg = 0.0f);

    /// Cylindrical unwrap for CCD2 side-view 360-degree inspection.
    static cv::Mat CylindricalUnwrap(const cv::Mat& image,
                                     cv::Point2f center, float radius,
                                     int output_width, int output_height);

private:
    std::unordered_map<int, PreprocessConfig> configs_;
    std::unordered_map<int, cv::Mat>          flatfield_refs_;
    cv::Mat                                   chromatic_lut_;  // CCD4 aberration map

    cv::Mat Demosaic(CameraID cam, const cv::Mat& raw);
    cv::Mat ApplyFlatField(CameraID cam, const cv::Mat& image);
    cv::Mat ApplyCLAHE(CameraID cam, const cv::Mat& image);
    cv::Mat CompensateChromaticAberration(const cv::Mat& image);
};

}  // namespace aoi
