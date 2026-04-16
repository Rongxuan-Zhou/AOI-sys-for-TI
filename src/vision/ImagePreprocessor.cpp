/**
 * Image preprocessing pipeline implementation.
 *
 * Processing order per frame:
 *   1. Bayer demosaicing (camera-specific pattern)
 *   2. Flat-field correction (division by reference, normalised)
 *   3. CLAHE contrast enhancement (per-CCD clip limit)
 *   4. CCD-specific transforms:
 *        CCD2 — cylindrical unwrap for side inspection
 *        CCD4 — sapphire glass chromatic aberration correction
 *
 * Author: Rongxuan Zhou
 */

#include "ImagePreprocessor.h"

#include <cmath>
#include <filesystem>
#include <fstream>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace aoi {
namespace fs = std::filesystem;

// CCD-specific Bayer patterns matching sensor orientation on the AOI fixture
static constexpr int kBayerCodeGE501GC = cv::COLOR_BayerRG2BGR;  // MV-GE501GC
static constexpr int kBayerCodeGE2000C = cv::COLOR_BayerGB2BGR;  // MV-GE2000C

// Camera pixel pitch — used for sub-pixel alignment calculations
static constexpr double kPixelPitch_5MP  = 0.0115;  // mm/px for CCD1-3
static constexpr double kPixelPitch_20MP = 0.0069;  // mm/px for CCD4

ImagePreprocessor::ImagePreprocessor() {
    // Default per-CCD CLAHE clip limits (empirically tuned)
    configs_[static_cast<int>(CameraID::CCD1_TOP)]    = {2.5, 8, true, false};
    configs_[static_cast<int>(CameraID::CCD2_SIDE)]   = {3.0, 8, true, false};
    configs_[static_cast<int>(CameraID::CCD3_BOTTOM)] = {2.5, 8, true, false};
    configs_[static_cast<int>(CameraID::CCD4_INNER)]  = {4.0, 16, true, true};
}

// ---------------------------------------------------------------------------
// Calibration loading
// ---------------------------------------------------------------------------
bool ImagePreprocessor::LoadCalibration(const std::string& calib_dir) {
    for (auto cam : {CameraID::CCD1_TOP, CameraID::CCD2_SIDE,
                     CameraID::CCD3_BOTTOM, CameraID::CCD4_INNER}) {
        std::string path = (fs::path(calib_dir) /
                            ("flatfield_ccd" + std::to_string(static_cast<int>(cam)) + ".tiff"))
                               .string();
        if (fs::exists(path)) {
            cv::Mat ref = cv::imread(path, cv::IMREAD_UNCHANGED);
            if (ref.empty()) return false;
            ref.convertTo(flatfield_refs_[static_cast<int>(cam)], CV_32F);
            // Normalise so mean intensity = 1.0
            flatfield_refs_[static_cast<int>(cam)] /=
                cv::mean(flatfield_refs_[static_cast<int>(cam)])[0];
        }
    }

    // Chromatic aberration LUT for CCD4 sapphire glass compensation.
    // The LUT stores per-pixel (dx_r, dy_r, dx_b, dy_b) displacement
    // vectors measured during factory calibration.
    std::string lut_path = (fs::path(calib_dir) / "chromatic_lut_ccd4.bin").string();
    if (fs::exists(lut_path)) {
        // Binary format: [height, width, 4 channels] float32
        std::ifstream ifs(lut_path, std::ios::binary);
        int32_t h, w;
        ifs.read(reinterpret_cast<char*>(&h), sizeof(h));
        ifs.read(reinterpret_cast<char*>(&w), sizeof(w));
        chromatic_lut_ = cv::Mat(h, w, CV_32FC4);
        ifs.read(reinterpret_cast<char*>(chromatic_lut_.data),
                 h * w * 4 * sizeof(float));
    }

    return true;
}

void ImagePreprocessor::SetConfig(CameraID cam, const PreprocessConfig& cfg) {
    configs_[static_cast<int>(cam)] = cfg;
}

// ---------------------------------------------------------------------------
// Main pipeline
// ---------------------------------------------------------------------------
cv::Mat ImagePreprocessor::Process(CameraID cam, const cv::Mat& raw) {
    cv::Mat color = Demosaic(cam, raw);
    cv::Mat corrected = ApplyFlatField(cam, color);
    cv::Mat enhanced = ApplyCLAHE(cam, corrected);

    // CCD-specific post-processing
    if (cam == CameraID::CCD4_INNER) {
        enhanced = CompensateChromaticAberration(enhanced);
    }

    return enhanced;
}

// ---------------------------------------------------------------------------
// Bayer demosaicing
// ---------------------------------------------------------------------------
cv::Mat ImagePreprocessor::Demosaic(CameraID cam, const cv::Mat& raw) {
    if (raw.channels() > 1) return raw;  // already colour

    int code = (cam == CameraID::CCD4_INNER) ? kBayerCodeGE2000C
                                              : kBayerCodeGE501GC;
    cv::Mat bgr;
    cv::cvtColor(raw, bgr, code);
    return bgr;
}

// ---------------------------------------------------------------------------
// Flat-field correction — compensates vignetting and non-uniform illumination
// ---------------------------------------------------------------------------
cv::Mat ImagePreprocessor::ApplyFlatField(CameraID cam, const cv::Mat& image) {
    int key = static_cast<int>(cam);
    auto it = flatfield_refs_.find(key);
    if (it == flatfield_refs_.end() || !configs_[key].apply_flatfield)
        return image;

    cv::Mat float_img;
    image.convertTo(float_img, CV_32F);

    // Per-channel division by normalised reference
    std::vector<cv::Mat> channels(image.channels());
    cv::split(float_img, channels);

    std::vector<cv::Mat> ref_channels;
    cv::split(it->second, ref_channels);

    for (int c = 0; c < image.channels(); ++c) {
        // Avoid divide-by-zero in regions outside the reference FOV
        cv::Mat safe_ref;
        cv::max(ref_channels[std::min(c, static_cast<int>(ref_channels.size()) - 1)],
                0.01f, safe_ref);
        cv::divide(channels[c], safe_ref, channels[c]);
    }

    cv::Mat result;
    cv::merge(channels, result);
    result.convertTo(result, CV_8U, 255.0);
    return result;
}

// ---------------------------------------------------------------------------
// CLAHE — adaptive histogram equalisation in LAB colour space
// ---------------------------------------------------------------------------
cv::Mat ImagePreprocessor::ApplyCLAHE(CameraID cam, const cv::Mat& image) {
    auto& cfg = configs_[static_cast<int>(cam)];

    cv::Mat lab;
    cv::cvtColor(image, lab, cv::COLOR_BGR2Lab);

    std::vector<cv::Mat> channels;
    cv::split(lab, channels);

    auto clahe = cv::createCLAHE(cfg.clahe_clip_limit,
                                  cv::Size(cfg.clahe_grid_size, cfg.clahe_grid_size));
    clahe->apply(channels[0], channels[0]);

    cv::merge(channels, lab);
    cv::Mat result;
    cv::cvtColor(lab, result, cv::COLOR_Lab2BGR);
    return result;
}

// ---------------------------------------------------------------------------
// CCD4 sapphire glass chromatic aberration compensation
// ---------------------------------------------------------------------------
cv::Mat ImagePreprocessor::CompensateChromaticAberration(const cv::Mat& image) {
    if (chromatic_lut_.empty()) return image;

    std::vector<cv::Mat> bgr;
    cv::split(image, bgr);

    // Extract per-channel displacement maps from the 4-channel LUT
    std::vector<cv::Mat> lut_channels;
    cv::split(chromatic_lut_, lut_channels);  // [dx_r, dy_r, dx_b, dy_b]

    // Build absolute remap coordinates for red and blue channels.
    // Green channel is the reference — it passes through unchanged.
    cv::Mat map_x(image.size(), CV_32F), map_y(image.size(), CV_32F);

    // Remap red channel (pointer arithmetic for performance)
    {
        const float* lut_dx = lut_channels[0].ptr<float>();
        const float* lut_dy = lut_channels[1].ptr<float>();
        float* mx = map_x.ptr<float>();
        float* my = map_y.ptr<float>();
        const int total = image.rows * image.cols;
        for (int i = 0; i < total; ++i) {
            mx[i] = static_cast<float>(i % image.cols) + lut_dx[i];
            my[i] = static_cast<float>(i / image.cols) + lut_dy[i];
        }
    }
    cv::remap(bgr[2], bgr[2], map_x, map_y, cv::INTER_LINEAR,
              cv::BORDER_REFLECT_101);

    // Remap blue channel (pointer arithmetic for performance)
    {
        const float* lut_dx = lut_channels[2].ptr<float>();
        const float* lut_dy = lut_channels[3].ptr<float>();
        float* mx = map_x.ptr<float>();
        float* my = map_y.ptr<float>();
        const int total = image.rows * image.cols;
        for (int i = 0; i < total; ++i) {
            mx[i] = static_cast<float>(i % image.cols) + lut_dx[i];
            my[i] = static_cast<float>(i / image.cols) + lut_dy[i];
        }
    }
    cv::remap(bgr[0], bgr[0], map_x, map_y, cv::INTER_LINEAR,
              cv::BORDER_REFLECT_101);

    cv::Mat result;
    cv::merge(bgr, result);
    return result;
}

// ---------------------------------------------------------------------------
// Sub-pixel aligned ROI extraction with optional rotation
// ---------------------------------------------------------------------------
cv::Mat ImagePreprocessor::ExtractROI(const cv::Mat& image, cv::Rect2f roi,
                                       float angle_deg) {
    if (std::abs(angle_deg) < 0.01f) {
        // Axis-aligned: use sub-pixel crop via getRectSubPix
        cv::Mat patch;
        cv::getRectSubPix(image, cv::Size(static_cast<int>(roi.width),
                                          static_cast<int>(roi.height)),
                          cv::Point2f(roi.x + roi.width / 2,
                                      roi.y + roi.height / 2),
                          patch);
        return patch;
    }

    // Rotated ROI — build affine transform
    cv::Point2f center(roi.x + roi.width / 2, roi.y + roi.height / 2);
    cv::Mat rot = cv::getRotationMatrix2D(center, angle_deg, 1.0);

    // Adjust translation so output is tightly cropped
    rot.at<double>(0, 2) += roi.width / 2 - center.x;
    rot.at<double>(1, 2) += roi.height / 2 - center.y;

    cv::Mat warped;
    cv::warpAffine(image, warped, rot,
                   cv::Size(static_cast<int>(roi.width),
                            static_cast<int>(roi.height)),
                   cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
    return warped;
}

// ---------------------------------------------------------------------------
// Cylindrical unwrap for CCD2 360-degree side inspection
// ---------------------------------------------------------------------------
cv::Mat ImagePreprocessor::CylindricalUnwrap(const cv::Mat& image,
                                              cv::Point2f center, float radius,
                                              int output_width,
                                              int output_height) {
    // Maps the cylindrical surface of the package side view into a flat
    // rectangular strip. Each column of the output corresponds to an
    // angular slice; each row maps radially outward from the centre.
    cv::Mat map_x(output_height, output_width, CV_32F);
    cv::Mat map_y(output_height, output_width, CV_32F);

    float angle_step = 2.0f * static_cast<float>(CV_PI) / output_width;
    float r_inner = radius * 0.92f;  // slightly inside the physical edge
    float r_outer = radius * 1.08f;
    float r_step  = (r_outer - r_inner) / output_height;

    for (int row = 0; row < output_height; ++row) {
        float r = r_inner + row * r_step;
        for (int col = 0; col < output_width; ++col) {
            float theta = col * angle_step;
            map_x.at<float>(row, col) = center.x + r * std::cos(theta);
            map_y.at<float>(row, col) = center.y + r * std::sin(theta);
        }
    }

    cv::Mat unwrapped;
    cv::remap(image, unwrapped, map_x, map_y, cv::INTER_LINEAR,
              cv::BORDER_CONSTANT, cv::Scalar(0));
    return unwrapped;
}

}  // namespace aoi
