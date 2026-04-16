/**
 * Defect detection engine implementation.
 *
 * Per-CCD routing:
 *   CCD1 — top surface: 11 defect types (structural, epoxy, contamination, code)
 *   CCD2 — side pins:    5 defect types (pin geometry + edge staining)
 *   CCD3 — bottom:       2 defect types (gold exposal, yellow glass cement)
 *   CCD4 — inner:        3 defect types (light leakage, epoxy secondary checks)
 *
 * All detection methods target <50 ms per frame on a quad-core i7 with
 * pre-allocated buffers to minimise heap allocations in the hot path.
 *
 * Author: Rongxuan Zhou
 */

#include "DefectDetector.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace aoi {
namespace fs = std::filesystem;

DefectDetector::DefectDetector() = default;

// ---------------------------------------------------------------------------
// Template loading — expects templates/{crack,broken}/*.png
// ---------------------------------------------------------------------------
bool DefectDetector::LoadTemplates(const std::string& template_dir) {
    auto load = [](const std::string& dir, std::vector<cv::Mat>& out) {
        if (!fs::is_directory(dir)) return false;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() == ".png" ||
                entry.path().extension() == ".tiff") {
                cv::Mat tmpl = cv::imread(entry.path().string(), cv::IMREAD_GRAYSCALE);
                if (!tmpl.empty()) out.push_back(tmpl);
            }
        }
        return !out.empty();
    };

    bool ok = true;
    ok &= load((fs::path(template_dir) / "crack").string(), crack_templates_);
    ok &= load((fs::path(template_dir) / "broken").string(), broken_templates_);
    return ok;
}

// ---------------------------------------------------------------------------
// CCD routing
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::Detect(CameraID cam,
                                                  const cv::Mat& image) {
    switch (cam) {
        case CameraID::CCD1_TOP:    return DetectCCD1(image);
        case CameraID::CCD2_SIDE:   return DetectCCD2(image);
        case CameraID::CCD3_BOTTOM: return DetectCCD3(image);
        case CameraID::CCD4_INNER:  return DetectCCD4(image);
    }
    return {};
}

// ---------------------------------------------------------------------------
// CCD1 — top surface (11 defect types)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectCCD1(const cv::Mat& image) {
    std::vector<DefectResult> results;
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    auto crack   = DetectCrackBroken(gray, CameraID::CCD1_TOP);
    auto epoxy   = DetectEpoxyDefects(image, CameraID::CCD1_TOP);
    auto contam  = DetectContamination(image, CameraID::CCD1_TOP);
    auto code    = DetectCodeDefects(gray, CameraID::CCD1_TOP);

    results.insert(results.end(), crack.begin(), crack.end());
    results.insert(results.end(), epoxy.begin(), epoxy.end());
    results.insert(results.end(), contam.begin(), contam.end());
    results.insert(results.end(), code.begin(), code.end());
    return results;
}

// CCD2 — side pins (5 defect types)
std::vector<DefectResult> DefectDetector::DetectCCD2(const cv::Mat& image) {
    auto pins = DetectPinDefects(image);

    // Edge staining — high-saturation regions along package perimeter
    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);

    cv::Mat sat_mask;
    cv::threshold(channels[1], sat_mask, 100, 255, cv::THRESH_BINARY);

    // Restrict to peripheral band (outer 8% of image height)
    int band = static_cast<int>(image.rows * 0.08);
    cv::Mat edge_mask = cv::Mat::zeros(image.size(), CV_8U);
    edge_mask(cv::Rect(0, 0, image.cols, band)).setTo(255);
    edge_mask(cv::Rect(0, image.rows - band, image.cols, band)).setTo(255);
    cv::bitwise_and(sat_mask, edge_mask, sat_mask);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(sat_mask, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);
    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area > 50) {
            cv::Rect bb = cv::boundingRect(c);
            float conf = std::min(1.0f, static_cast<float>(area) / 500.0f);
            pins.push_back({DefectType::A_EDGE_STAINING,
                            ClassifySeverity(DefectType::A_EDGE_STAINING, conf,
                                             static_cast<int>(area)),
                            conf, bb, CameraID::CCD2_SIDE, "edge staining"});
        }
    }

    return pins;
}

// CCD3 — bottom surface (2 defect types)
std::vector<DefectResult> DefectDetector::DetectCCD3(const cv::Mat& image) {
    auto gold = DetectGoldExposal(image);
    auto ygc  = DetectYellowGlassCement(image);
    gold.insert(gold.end(), ygc.begin(), ygc.end());
    return gold;
}

// CCD4 — closed chamber (3 defect types)
std::vector<DefectResult> DefectDetector::DetectCCD4(const cv::Mat& image) {
    auto leakage = DetectLightLeakage(image);
    auto epoxy   = DetectEpoxyDefects(image, CameraID::CCD4_INNER);
    leakage.insert(leakage.end(), epoxy.begin(), epoxy.end());
    return leakage;
}

// ---------------------------------------------------------------------------
// Template matching — crack and broken detection via NCC
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectCrackBroken(const cv::Mat& gray,
                                                             CameraID cam) {
    std::vector<DefectResult> results;

    auto match_all = [&](const std::vector<cv::Mat>& templates,
                         double threshold, DefectType dtype) {
        for (const auto& tmpl : templates) {
            if (tmpl.cols > gray.cols || tmpl.rows > gray.rows) continue;

            cv::Mat score;
            cv::matchTemplate(gray, tmpl, score, cv::TM_CCOEFF_NORMED);

            // Multi-hit extraction with non-maximum suppression
            double max_val;
            cv::Point max_loc;
            while (true) {
                cv::minMaxLoc(score, nullptr, &max_val, nullptr, &max_loc);
                if (max_val < threshold) break;

                cv::Rect bb(max_loc.x, max_loc.y, tmpl.cols, tmpl.rows);
                float conf = static_cast<float>(max_val);
                results.push_back({dtype,
                                   ClassifySeverity(dtype, conf, bb.area()),
                                   conf, bb, cam, ""});

                // Suppress the detected region to find additional matches
                cv::rectangle(score, bb, cv::Scalar(0), cv::FILLED);
            }
        }
    };

    match_all(crack_templates_,  params_.crack_ncc_threshold,
              DefectType::F_CRACK);
    match_all(broken_templates_, params_.broken_ncc_threshold,
              DefectType::F_BROKEN);

    return results;
}

// ---------------------------------------------------------------------------
// Epoxy defect detection — exposal, insufficient, overflow
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectEpoxyDefects(const cv::Mat& image,
                                                              CameraID cam) {
    std::vector<DefectResult> results;

    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);

    // Epoxy appears as dark amber/brown — specific hue + low value range
    cv::Mat mask;
    cv::inRange(hsv, cv::Scalar(10, 50, 20), cv::Scalar(30, 255, 180), mask);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // Expected epoxy coverage region (central die area, ~40% of image)
    cv::Rect expected_zone(image.cols / 4, image.rows / 4,
                           image.cols / 2, image.rows / 2);
    double expected_area = expected_zone.area() * 0.6;

    double total_epoxy_area = 0;
    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        total_epoxy_area += area;
        cv::Rect bb = cv::boundingRect(c);

        // Overflow: epoxy outside expected zone boundary
        if ((bb.x < expected_zone.x || bb.br().x > expected_zone.br().x ||
             bb.y < expected_zone.y || bb.br().y > expected_zone.br().y) &&
            area > 100) {
            float conf = std::min(1.0f, static_cast<float>(area) / 2000.0f);
            results.push_back({DefectType::F_EPOXY_OVERFLOW,
                               Severity::CRITICAL, conf, bb, cam,
                               "epoxy outside boundary"});
        }

        // Exposal: unexpected bare area within epoxy zone
        cv::Rect intersection = bb & expected_zone;
        if (intersection.area() > 0 && area < 50 && area > 10) {
            results.push_back({DefectType::F_EPOXY_EXPOSAL,
                               Severity::CRITICAL, 0.7f, bb, cam,
                               "bare substrate visible through epoxy"});
        }
    }

    // Insufficient epoxy: total coverage below threshold
    if (total_epoxy_area < expected_area * 0.7 && total_epoxy_area > 0) {
        float ratio = static_cast<float>(total_epoxy_area / expected_area);
        results.push_back({DefectType::F_INSUFFICIENT_EPOXY,
                           Severity::CRITICAL, 1.0f - ratio,
                           expected_zone, cam,
                           "epoxy coverage " +
                               std::to_string(static_cast<int>(ratio * 100)) + "%"});
    }

    return results;
}

// ---------------------------------------------------------------------------
// Contamination detection — blob analysis with electrical/non-electrical split
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectContamination(const cv::Mat& image,
                                                               CameraID cam) {
    std::vector<DefectResult> results;

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    // Adaptive threshold isolates local intensity anomalies (particles/residue)
    cv::Mat binary;
    cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV, 31, 12);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);

    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < params_.contam_area_min_px) continue;

        double perimeter = cv::arcLength(c, true);
        double circularity = (perimeter > 0)
                                 ? 4.0 * CV_PI * area / (perimeter * perimeter)
                                 : 0.0;
        cv::Rect bb = cv::boundingRect(c);

        // Conductivity feature: mean intensity in the blob region as a proxy
        // for metallic (bright, electrically conductive) vs organic (dark) debris.
        cv::Mat roi_mask = cv::Mat::zeros(gray.size(), CV_8U);
        cv::drawContours(roi_mask, std::vector<std::vector<cv::Point>>{c},
                         0, cv::Scalar(255), cv::FILLED);
        double mean_intensity = cv::mean(gray, roi_mask)[0];
        double conductivity_feat = mean_intensity / 255.0;

        DefectType dtype;
        if (conductivity_feat > params_.conductivity_feature_split) {
            dtype = DefectType::C_DYEING_CONTAMINATION;
        } else {
            dtype = DefectType::C_NON_ELECTRICAL_CONTAMINATION;
        }

        // Staining: low-circularity diffuse region with moderate area
        if (circularity < params_.contam_circularity_thresh && area > 200) {
            dtype = DefectType::C_STAINING;
        }

        float conf = std::min(1.0f, static_cast<float>(area) / 1000.0f);
        results.push_back({dtype, ClassifySeverity(dtype, conf,
                                                    static_cast<int>(area)),
                           conf, bb, cam, ""});
    }

    return results;
}

// ---------------------------------------------------------------------------
// Pin defect detection (CCD2) — Hough lines + contour analysis
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectPinDefects(const cv::Mat& image) {
    std::vector<DefectResult> results;

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    // Edge detection tuned for pin geometry (fine metallic features)
    cv::Mat edges;
    cv::Canny(gray, edges, 50, 150, 3);

    // Hough line detection for pin straightness analysis
    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(edges, lines, 1, CV_PI / 180, 60, 40, 10);

    // Group lines by approximate y-position to identify individual pins
    struct PinCandidate {
        std::vector<cv::Vec4i> segments;
        double angle_deg;
        cv::Rect bbox;
    };
    std::vector<PinCandidate> pins;

    // Cluster line segments into pin groups by vertical proximity
    std::sort(lines.begin(), lines.end(),
              [](const cv::Vec4i& a, const cv::Vec4i& b) {
                  return (a[1] + a[3]) / 2 < (b[1] + b[3]) / 2;
              });

    constexpr int kPinGroupThreshold = 15;  // px vertical gap between pin groups
    PinCandidate current;
    for (const auto& line : lines) {
        int y_mid = (line[1] + line[3]) / 2;
        if (!current.segments.empty()) {
            int prev_y = (current.segments.back()[1] +
                          current.segments.back()[3]) / 2;
            if (std::abs(y_mid - prev_y) > kPinGroupThreshold) {
                pins.push_back(current);
                current = {};
            }
        }
        current.segments.push_back(line);
    }
    if (!current.segments.empty()) pins.push_back(current);

    for (auto& pin : pins) {
        // Compute dominant angle from longest segment
        double max_len = 0;
        double dominant_angle = 0;
        int x_min = INT_MAX, y_min = INT_MAX, x_max = 0, y_max = 0;

        for (const auto& seg : pin.segments) {
            double dx = seg[2] - seg[0];
            double dy = seg[3] - seg[1];
            double len = std::sqrt(dx * dx + dy * dy);
            if (len > max_len) {
                max_len = len;
                dominant_angle = std::atan2(dy, dx) * 180.0 / CV_PI;
            }
            x_min = std::min({x_min, seg[0], seg[2]});
            y_min = std::min({y_min, seg[1], seg[3]});
            x_max = std::max({x_max, seg[0], seg[2]});
            y_max = std::max({y_max, seg[1], seg[3]});
        }

        pin.angle_deg = dominant_angle;
        pin.bbox = cv::Rect(x_min, y_min, x_max - x_min, y_max - y_min);

        // Pin bent: deviation from horizontal beyond threshold
        double angle_deviation = std::abs(std::fmod(dominant_angle, 180.0));
        if (angle_deviation > params_.pin_bend_angle_deg_max &&
            angle_deviation < (180.0 - params_.pin_bend_angle_deg_max)) {
            float conf = std::min(1.0f,
                                  static_cast<float>(angle_deviation) / 10.0f);
            results.push_back({DefectType::F_PIN_BENT, Severity::CRITICAL,
                               conf, pin.bbox, CameraID::CCD2_SIDE,
                               "angle deviation " +
                                   std::to_string(angle_deviation) + " deg"});
        }

        // Pin mis-cut: abnormally short pin length relative to expected
        double expected_length = image.cols * 0.3;  // nominal pin extends ~30% of FOV
        if (max_len < expected_length * params_.pin_miscut_length_ratio) {
            float conf = 1.0f - static_cast<float>(max_len / expected_length);
            results.push_back({DefectType::F_PIN_MIS_CUT, Severity::CRITICAL,
                               conf, pin.bbox, CameraID::CCD2_SIDE,
                               "pin length " + std::to_string(max_len) + " px"});
        }

        // Pin bur: jagged edge contour in the pin tip region
        cv::Rect tip_roi(pin.bbox.x + pin.bbox.width * 3 / 4, pin.bbox.y,
                         pin.bbox.width / 4, pin.bbox.height);
        tip_roi &= cv::Rect(0, 0, gray.cols, gray.rows);
        if (tip_roi.area() > 0) {
            cv::Mat tip_edges = edges(tip_roi);
            int edge_count = cv::countNonZero(tip_edges);
            if (edge_count > params_.pin_bur_edge_threshold) {
                float conf = std::min(
                    1.0f, static_cast<float>(edge_count) /
                               (params_.pin_bur_edge_threshold * 3.0f));
                results.push_back({DefectType::A_PIN_BUR, Severity::MAJOR,
                                   conf, tip_roi, CameraID::CCD2_SIDE,
                                   "excessive tip edge density"});
            }
        }
    }

    // Pin oxidation: orange/brown discoloration in HSV space
    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
    cv::Mat oxide_mask;
    cv::inRange(hsv,
                cv::Scalar(params_.pin_oxidation_hue_low, 40, 40),
                cv::Scalar(params_.pin_oxidation_hue_high, 255, 200),
                oxide_mask);

    std::vector<std::vector<cv::Point>> oxide_contours;
    cv::findContours(oxide_mask, oxide_contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);
    for (const auto& c : oxide_contours) {
        double area = cv::contourArea(c);
        if (area > 80) {
            cv::Rect bb = cv::boundingRect(c);
            float conf = std::min(1.0f, static_cast<float>(area) / 600.0f);
            results.push_back({DefectType::F_PIN_OXIDIZED, Severity::CRITICAL,
                               conf, bb, CameraID::CCD2_SIDE, "oxidation"});
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// Code OCR quality — No Code / Code Blur detection
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectCodeDefects(const cv::Mat& gray,
                                                             CameraID cam) {
    std::vector<DefectResult> results;

    // Code marking region is in the upper-centre of the top surface
    int roi_x = (gray.cols - params_.code_roi_width) / 2;
    int roi_y = gray.rows / 8;
    cv::Rect code_roi(roi_x, roi_y, params_.code_roi_width,
                      params_.code_roi_height);
    code_roi &= cv::Rect(0, 0, gray.cols, gray.rows);

    cv::Mat code_region = gray(code_roi);

    // Laplacian variance as sharpness metric
    cv::Mat laplacian;
    cv::Laplacian(code_region, laplacian, CV_64F);
    cv::Scalar mu, sigma;
    cv::meanStdDev(laplacian, mu, sigma);
    double variance = sigma[0] * sigma[0];

    // Intensity check — if the region is nearly uniform, no code is present
    cv::Scalar region_mu, region_sigma;
    cv::meanStdDev(code_region, region_mu, region_sigma);

    if (region_sigma[0] < 8.0) {
        // No discernible marking at all
        results.push_back({DefectType::A_NO_CODE, Severity::MAJOR, 0.95f,
                           code_roi, cam, "no marking detected"});
    } else if (variance < params_.code_laplacian_blur_limit) {
        float conf = 1.0f - static_cast<float>(variance /
                                                params_.code_laplacian_blur_limit);
        results.push_back({DefectType::C_CODE_BLUR, Severity::MINOR, conf,
                           code_roi, cam,
                           "Laplacian var=" + std::to_string(variance)});
    }

    // Misalignment: code centroid deviation from expected position
    cv::Mat binary;
    cv::threshold(code_region, binary, 0, 255,
                  cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    cv::Moments m = cv::moments(binary);
    if (m.m00 > 0) {
        double cx = m.m10 / m.m00;
        double cy = m.m01 / m.m00;
        double dx = std::abs(cx - code_region.cols / 2.0);
        double dy = std::abs(cy - code_region.rows / 2.0);
        double displacement = std::sqrt(dx * dx + dy * dy);

        // Displacement > 15% of ROI dimension indicates misalignment
        double threshold = code_region.cols * 0.15;
        if (displacement > threshold) {
            float conf = std::min(1.0f,
                                  static_cast<float>(displacement / (threshold * 2)));
            results.push_back({DefectType::A_MISALIGNMENT, Severity::MAJOR,
                               conf, code_roi, cam,
                               "code offset " + std::to_string(displacement) + " px"});
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// Light leakage detection (CCD4 closed chamber)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectLightLeakage(const cv::Mat& image) {
    std::vector<DefectResult> results;

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    // In a properly sealed chamber, the image should be uniformly dark
    // except for the controlled inspection illumination region.
    // Leakage appears as bright patches outside the expected lit zone.

    // Expected illumination zone: central 60%
    cv::Rect lit_zone(image.cols / 5, image.rows / 5,
                      image.cols * 3 / 5, image.rows * 3 / 5);
    cv::Mat peripheral_mask = cv::Mat::ones(gray.size(), CV_8U) * 255;
    peripheral_mask(lit_zone).setTo(0);

    cv::Mat peripheral;
    gray.copyTo(peripheral, peripheral_mask);

    cv::Mat bright_mask;
    cv::threshold(peripheral, bright_mask, params_.leakage_intensity_thresh,
                  255, cv::THRESH_BINARY);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bright_mask, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);

    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < params_.leakage_area_min_px) continue;

        cv::Rect bb = cv::boundingRect(c);
        double mean_bright = cv::mean(gray(bb))[0];
        float conf = std::min(1.0f, static_cast<float>(mean_bright / 80.0));

        results.push_back({DefectType::A_LIGHT_LEAKAGE, Severity::MAJOR,
                           conf, bb, CameraID::CCD4_INNER,
                           "leakage area=" + std::to_string(static_cast<int>(area)) +
                               " intensity=" + std::to_string(mean_bright)});
    }

    return results;
}

// ---------------------------------------------------------------------------
// Gold exposal detection (CCD3 bottom surface)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectGoldExposal(const cv::Mat& image) {
    std::vector<DefectResult> results;

    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);

    // Gold colour: narrow hue band with high saturation and moderate value
    cv::Mat gold_mask;
    cv::inRange(hsv, cv::Scalar(18, 80, 100), cv::Scalar(35, 255, 255),
                gold_mask);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(gold_mask, gold_mask, cv::MORPH_OPEN, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(gold_mask, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);

    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < 40) continue;

        cv::Rect bb = cv::boundingRect(c);
        float conf = std::min(1.0f, static_cast<float>(area) / 800.0f);
        results.push_back({DefectType::A_GOLD_EXPOSAL, Severity::MAJOR,
                           conf, bb, CameraID::CCD3_BOTTOM, "gold exposal"});
    }

    return results;
}

// ---------------------------------------------------------------------------
// Yellow glass cement detection (CCD3 bottom surface)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectYellowGlassCement(
        const cv::Mat& image) {
    std::vector<DefectResult> results;

    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);

    // Yellow cement residue: yellow hue, high saturation
    cv::Mat yellow_mask;
    cv::inRange(hsv, cv::Scalar(22, 60, 80), cv::Scalar(40, 255, 255),
                yellow_mask);

    // Distinguish from gold by checking for lower value channel (cement is duller)
    cv::Mat value_mask;
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);
    cv::threshold(channels[2], value_mask, 180, 255, cv::THRESH_BINARY_INV);
    cv::bitwise_and(yellow_mask, value_mask, yellow_mask);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(yellow_mask, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);

    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < 60) continue;

        cv::Rect bb = cv::boundingRect(c);
        float conf = std::min(1.0f, static_cast<float>(area) / 1200.0f);
        results.push_back({DefectType::A_YELLOW_GLASS_CEMENT, Severity::MAJOR,
                           conf, bb, CameraID::CCD3_BOTTOM,
                           "yellow glass cement residue"});
    }

    return results;
}

// ---------------------------------------------------------------------------
// Severity classification based on defect type, confidence, and area
// ---------------------------------------------------------------------------
Severity DefectDetector::ClassifySeverity(DefectType type, float confidence,
                                          int area_px) {
    // All F_ (function) defects are critical regardless of size
    int t = static_cast<int>(type);
    if (t <= static_cast<int>(DefectType::F_PIN_MIS_CUT))
        return Severity::CRITICAL;

    // Cosmetic and assembly defects: severity scales with area and confidence
    if (confidence > 0.85 || area_px > 500)
        return Severity::MAJOR;

    return Severity::MINOR;
}

}  // namespace aoi
