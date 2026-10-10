/**
 * Defect detection engine implementation.
 *
 * Author: Rongxuan Zhou
 */

#include "DefectDetector.h"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace aoi {

// ---------------------------------------------------------------------------
// Taxonomy tables
// ---------------------------------------------------------------------------
DefectGroup GroupOf(DefectType t) {
    int i = static_cast<int>(t);
    if (i <= static_cast<int>(DefectType::EPOXY_OVERFLOW))    return DefectGroup::FUNCTION;
    if (i <= static_cast<int>(DefectType::CODE_BLUR))         return DefectGroup::COSMETIC;
    if (i <= static_cast<int>(DefectType::MISALIGNMENT))      return DefectGroup::ASSEMBLY;
    return DefectGroup::INNER_RESIDUE;
}

Severity SeverityOf(DefectType t) {
    switch (t) {
        case DefectType::LIGHTING_CHECK:
        case DefectType::CRACK:
        case DefectType::BROKEN:
        case DefectType::MISALIGNMENT:
            return Severity::CRITICAL;
        default:
            break;
    }
    DefectGroup g = GroupOf(t);
    if (g == DefectGroup::FUNCTION || g == DefectGroup::ASSEMBLY) return Severity::MAJOR;
    return Severity::MINOR;
}

const char* ToString(DefectType t) {
    static const char* const kNames[] = {
        "lighting_check", "crack", "broken", "epoxy_exposal", "pin_missing",
        "electrical_contamination", "gold_exposal", "insufficient_epoxy", "epoxy_overflow",
        "dyeing_contamination", "non_electrical_contamination", "no_code", "code_blur",
        "pin_bent", "pin_oxidized", "pin_bur", "pin_mis_cut", "epoxy_higher_than_ceramic",
        "misalignment", "staining_on_edge", "yellow_glass_cement"};
    int i = static_cast<int>(t);
    if (i < 0 || i >= static_cast<int>(DefectType::COUNT)) return "unknown";
    return kNames[i];
}

// ---------------------------------------------------------------------------
// Construction / parameters
// ---------------------------------------------------------------------------
DefectDetector::DefectDetector() : DefectDetector(DetectionParams{}) {}

DefectDetector::DefectDetector(const DetectionParams& params)
    : params_(params), locator_(params.locator), lighting_(params.lighting) {}

void DefectDetector::SetParams(const DetectionParams& params) {
    params_ = params;
    locator_ = CseLocator(params.locator);
    lighting_.SetParams(params.lighting);
}

DefectType DefectDetector::PrimaryTypeOf(CameraID cam) {
    switch (cam) {
        case CameraID::CCD2_SIDE:   return DefectType::PIN_BENT;
        case CameraID::CCD3_BOTTOM: return DefectType::BROKEN;
        case CameraID::CCD4_INNER:  return DefectType::LIGHTING_CHECK;
        default:                    return DefectType::MISALIGNMENT;
    }
}

DefectResult DefectDetector::NotLocated(CameraID cam) {
    DefectType t = PrimaryTypeOf(cam);
    return {t, SeverityOf(t), 1.f, cv::Rect(), cam, "disc not located"};
}

DefectResult DefectDetector::Make(DefectType t, float conf, cv::Rect bb, CameraID cam,
                                  const std::string& desc) {
    return {t, SeverityOf(t), std::clamp(conf, 0.f, 1.f), bb, cam, desc};
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::Detect(CameraID cam, const cv::Mat& image) {
    if (image.empty()) return {NotLocated(cam)};

    cv::Mat bgr;
    if (image.channels() == 1) {
        cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
    } else if (image.channels() == 3) {
        bgr = image;
    } else {
        return {NotLocated(cam)};
    }

    switch (cam) {
        case CameraID::CCD1_TOP:    return DetectCCD1(bgr);
        case CameraID::CCD2_SIDE:   return DetectCCD2(bgr);
        case CameraID::CCD3_BOTTOM: return DetectCCD3(bgr);
        case CameraID::CCD4_INNER:  return DetectCCD4(bgr);
        case CameraID::NG_CHECK:    return DetectCCD1(bgr);   // NG reconfirm looks at the top side
        default:                    return {NotLocated(cam)};
    }
}

// CCD1 — top side
std::vector<DefectResult> DefectDetector::DetectCCD1(const cv::Mat& bgr) {
    CseGeometry g = locator_.Locate(bgr);
    if (!g.found) return {NotLocated(CameraID::CCD1_TOP)};

    std::vector<DefectResult> out;
    auto append = [&out](std::vector<DefectResult> v) {
        out.insert(out.end(), v.begin(), v.end());
    };

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    append(DetectMisalignment(g, bgr.size(), CameraID::CCD1_TOP));
    append(DetectBroken(g, CameraID::CCD1_TOP));

    cv::Mat pin_mask, code_mask;
    append(DetectPinsTop(bgr, g, pin_mask));
    append(DetectCode(bgr, g, code_mask));

    cv::Mat exclude = cv::Mat::zeros(bgr.size(), CV_8U);
    if (!pin_mask.empty()) cv::bitwise_or(exclude, pin_mask, exclude);
    if (!code_mask.empty()) cv::bitwise_or(exclude, code_mask, exclude);

    append(DetectCrack(gray, g, exclude, CameraID::CCD1_TOP));
    append(DetectEpoxyExposal(bgr, g, exclude));
    append(DetectContamination(bgr, g, exclude, CameraID::CCD1_TOP));
    return out;
}

// CCD3 — bottom side
std::vector<DefectResult> DefectDetector::DetectCCD3(const cv::Mat& bgr) {
    CseGeometry g = locator_.Locate(bgr);
    if (!g.found) return {NotLocated(CameraID::CCD3_BOTTOM)};

    std::vector<DefectResult> out;
    auto append = [&out](std::vector<DefectResult> v) {
        out.insert(out.end(), v.begin(), v.end());
    };

    cv::Mat gray, hsv;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);

    // The amber epoxy region is structure on the bottom side, not a defect.
    cv::Mat epoxy;
    cv::inRange(hsv, cv::Scalar(params_.epoxy_hue_low, params_.epoxy_sat_min, 0),
                cv::Scalar(params_.epoxy_hue_high, 255, 255), epoxy);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(21, 21));
    cv::dilate(epoxy, epoxy, kernel);

    append(DetectMisalignment(g, bgr.size(), CameraID::CCD3_BOTTOM));
    append(DetectBroken(g, CameraID::CCD3_BOTTOM));
    append(DetectCrack(gray, g, epoxy, CameraID::CCD3_BOTTOM));
    append(DetectContamination(bgr, g, epoxy, CameraID::CCD3_BOTTOM));
    return out;
}

// CCD2 — side view
std::vector<DefectResult> DefectDetector::DetectCCD2(const cv::Mat& bgr_in) {
    cv::Mat bgr = bgr_in;
    if (params_.pins_up) cv::flip(bgr_in, bgr, 0);   // internal orientation: pins below the body

    cv::Mat gray, fg;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, fg, params_.side_fg_thresh, 255, cv::THRESH_BINARY);

    // Body = largest component after a horizontal opening wider than a pin,
    // so pins connected to the body do not become part of it.
    cv::Mat opened;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2 * params_.pin_w_max + 1, 1));
    cv::morphologyEx(fg, opened, cv::MORPH_OPEN, kernel);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(opened, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return {NotLocated(CameraID::CCD2_SIDE)};
    auto largest = std::max_element(contours.begin(), contours.end(),
                                    [](const auto& a, const auto& b) {
                                        return cv::contourArea(a) < cv::contourArea(b);
                                    });
    cv::Rect body = cv::boundingRect(*largest);
    if (body.area() < 2000) return {NotLocated(CameraID::CCD2_SIDE)};

    std::vector<DefectResult> out;
    auto append = [&out](std::vector<DefectResult> v) {
        out.insert(out.end(), v.begin(), v.end());
    };
    append(DetectPinsSide(bgr, fg, body));
    append(DetectGoldExposal(bgr, body));
    append(DetectEpoxyBump(fg, body));

    if (params_.pins_up) {
        for (auto& r : out) {
            if (r.bbox.area() > 0) r.bbox.y = bgr.rows - r.bbox.y - r.bbox.height;
        }
    }
    return out;
}

// CCD4 — inner check
std::vector<DefectResult> DefectDetector::DetectCCD4(const cv::Mat& bgr) {
    LightingResult lr = lighting_.Analyse(bgr);
    if (!lr.located) return {NotLocated(CameraID::CCD4_INNER)};
    std::vector<DefectResult> out = lr.defects;
    if (lr.nest_found && lr.offset_mm > params_.lighting.misalign_max_offset_mm) {
        int r = static_cast<int>(lr.radius_px);
        cv::Rect bb(static_cast<int>(lr.center.x) - r, static_cast<int>(lr.center.y) - r, 2 * r, 2 * r);
        out.push_back(Make(DefectType::MISALIGNMENT,
                           static_cast<float>(lr.offset_mm / (2.0 * params_.lighting.misalign_max_offset_mm)),
                           bb & cv::Rect(0, 0, bgr.cols, bgr.rows), CameraID::CCD4_INNER,
                           "nest offset " + std::to_string(lr.offset_mm) + " mm"));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Primitives — filled in by Tasks 4, 6, 7, 8
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// CRACK — thin dark elongated structure inside the disc (slide 32)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectCrack(const cv::Mat& gray, const CseGeometry& g,
                                                      const cv::Mat& exclude_mask, CameraID cam) {
    std::vector<DefectResult> out;
    cv::Mat roi_mask = locator_.InteriorMask(g);
    if (roi_mask.empty()) return out;
    if (!exclude_mask.empty()) {
        cv::Mat not_excl;
        cv::bitwise_not(exclude_mask, not_excl);
        cv::bitwise_and(roi_mask, not_excl, roi_mask);
    }

    int k = params_.crack_blackhat_kernel_px | 1;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
    cv::Mat blackhat;
    cv::morphologyEx(gray, blackhat, cv::MORPH_BLACKHAT, kernel);

    cv::Mat cand;
    cv::threshold(blackhat, cand, params_.crack_blackhat_thresh, 255, cv::THRESH_BINARY);
    cv::bitwise_and(cand, roi_mask, cand);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(cand, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto& c : contours) {
        if (c.size() < 5) continue;
        cv::RotatedRect rr = cv::minAreaRect(c);
        float long_side = std::max(rr.size.width, rr.size.height);
        float short_side = std::max(1.f, std::min(rr.size.width, rr.size.height));
        if (long_side < params_.crack_min_length_px) continue;
        if (long_side / short_side < params_.crack_aspect_min) continue;
        float conf = long_side / (4.f * params_.crack_min_length_px);
        out.push_back(Make(DefectType::CRACK, conf, cv::boundingRect(c), cam,
                           "crack length " + std::to_string(static_cast<int>(long_side)) + " px"));
    }
    return out;
}

// ---------------------------------------------------------------------------
// BROKEN — rim residual runs; only the key notch (by angle) is exempt (slide 33)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectBroken(const CseGeometry& g, CameraID cam) {
    std::vector<DefectResult> out;
    const int n = 720;
    std::vector<float> prof = locator_.RimProfile(g, n);
    const float R = g.radius_px;
    const LocatorParams& lp = params_.locator;

    std::vector<char> flag(n);
    for (int i = 0; i < n; ++i) flag[i] = (R - prof[i]) > params_.broken_depth_px;

    for (int i = 0; i < n; ++i) {
        if (!flag[i] || flag[(i + n - 1) % n]) continue;  // run start only
        int len = 0;
        float depth = 0.f;
        int j = i;
        while (flag[j % n] && len < n) {
            depth = std::max(depth, R - prof[j % n]);
            ++len;
            ++j;
        }
        float width_deg = len * 360.f / n;
        float centre_deg = std::fmod((i + (len - 1) / 2.f) * 360.f / n, 360.f);

        bool is_notch = false;
        if (!std::isnan(g.notch_angle_deg)) {
            float da = std::fabs(centre_deg - g.notch_angle_deg);
            da = std::min(da, 360.f - da);
            is_notch = da <= lp.notch_width_deg && depth <= 1.5f * lp.notch_depth_px;
        }
        if (is_notch) continue;

        float a0 = (centre_deg - width_deg / 2.f) * static_cast<float>(CV_PI) / 180.f;
        float a1 = (centre_deg + width_deg / 2.f) * static_cast<float>(CV_PI) / 180.f;
        std::vector<cv::Point> pts = {
            cv::Point(cvRound(g.center.x + R * std::cos(a0)), cvRound(g.center.y + R * std::sin(a0))),
            cv::Point(cvRound(g.center.x + R * std::cos(a1)), cvRound(g.center.y + R * std::sin(a1))),
            cv::Point(cvRound(g.center.x + (R - depth) * std::cos((a0 + a1) / 2)),
                      cvRound(g.center.y + (R - depth) * std::sin((a0 + a1) / 2)))};
        cv::Rect bb = cv::boundingRect(pts);
        bb.x -= 4; bb.y -= 4; bb.width += 8; bb.height += 8;
        float conf = depth / (3.f * params_.broken_depth_px);
        out.push_back(Make(DefectType::BROKEN, conf, bb, cam,
                           "rim chip depth " + std::to_string(static_cast<int>(depth)) +
                               " px at " + std::to_string(static_cast<int>(centre_deg)) + " deg"));
    }
    return out;
}

// ---------------------------------------------------------------------------
// MISALIGNMENT — disc centre vs the station's fixture centre (slide 45)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectMisalignment(const CseGeometry& g, cv::Size frame,
                                                             CameraID cam) {
    std::vector<DefectResult> out;
    cv::Point2f ref = (cam == CameraID::CCD3_BOTTOM) ? params_.fixture_center_ccd3
                                                     : params_.fixture_center_ccd1;
    if (ref.x < 0 || ref.y < 0) ref = cv::Point2f(frame.width / 2.f, frame.height / 2.f);

    double offset_mm = std::hypot(g.center.x - ref.x, g.center.y - ref.y) * params_.mm_per_px_ccd123;
    if (offset_mm > params_.misalign_max_offset_mm) {
        int r = static_cast<int>(g.radius_px);
        cv::Rect bb(static_cast<int>(g.center.x) - r, static_cast<int>(g.center.y) - r, 2 * r, 2 * r);
        bb &= cv::Rect(0, 0, frame.width, frame.height);
        float conf = static_cast<float>(offset_mm / (2.0 * params_.misalign_max_offset_mm));
        out.push_back(Make(DefectType::MISALIGNMENT, conf, bb, cam,
                           "centre offset " + std::to_string(offset_mm) + " mm"));
    }
    return out;
}
std::vector<DefectResult> DefectDetector::DetectContamination(const cv::Mat&, const CseGeometry&,
                                                              const cv::Mat&, CameraID) { return {}; }
std::vector<DefectResult> DefectDetector::DetectPinsTop(const cv::Mat&, const CseGeometry&,
                                                        cv::Mat&) { return {}; }
std::vector<DefectResult> DefectDetector::DetectEpoxyExposal(const cv::Mat&, const CseGeometry&,
                                                             const cv::Mat&) { return {}; }
std::vector<DefectResult> DefectDetector::DetectCode(const cv::Mat&, const CseGeometry&,
                                                     cv::Mat&) { return {}; }
std::vector<DefectResult> DefectDetector::DetectPinsSide(const cv::Mat&, const cv::Mat&,
                                                         const cv::Rect&) { return {}; }
std::vector<DefectResult> DefectDetector::DetectGoldExposal(const cv::Mat&, const cv::Rect&) { return {}; }
std::vector<DefectResult> DefectDetector::DetectEpoxyBump(const cv::Mat&, const cv::Rect&) { return {}; }

}  // namespace aoi
