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

    cv::Mat u8 = CseLocator::To8U(image);
    cv::Mat bgr;
    if (u8.channels() == 1) {
        cv::cvtColor(u8, bgr, cv::COLOR_GRAY2BGR);
    } else if (u8.channels() == 3) {
        bgr = u8;
    } else {
        return {NotLocated(cam)};
    }

    // Never let an OpenCV exception leave the detector: the IPC process must
    // keep running and the unit is NG when the frame cannot be judged.
    try {
        switch (cam) {
            case CameraID::CCD1_TOP:    return DetectCCD1(bgr);
            case CameraID::CCD2_SIDE:   return DetectCCD2(bgr);
            case CameraID::CCD3_BOTTOM: return DetectCCD3(bgr);
            case CameraID::CCD4_INNER:  return DetectCCD4(bgr);
            case CameraID::NG_CHECK:    return DetectCCD1(bgr);   // NG reconfirm looks at the top side
            default:                    return {NotLocated(cam)};
        }
    } catch (const cv::Exception&) {
        return {NotLocated(cam)};
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
// ---------------------------------------------------------------------------
// Contamination — blobs that differ from the ceramic, classified by colour and
// texture (slides 36, 39, 40):
//   dyeing          saturated colour stain
//   electrical      dark metallic textured streak
//   non-electrical  everything else (light smudge)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectContamination(const cv::Mat& bgr, const CseGeometry& g,
                                                              const cv::Mat& exclude_mask, CameraID cam) {
    std::vector<DefectResult> out;
    cv::Mat roi = locator_.InteriorMask(g);
    if (roi.empty()) return out;
    if (!exclude_mask.empty()) {
        cv::Mat not_excl;
        cv::bitwise_not(exclude_mask, not_excl);
        cv::bitwise_and(roi, not_excl, roi);
    }

    cv::Mat gray, hsv, S;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    cv::extractChannel(hsv, S, 1);

    cv::Mat local_dark, saturated, cand;
    cv::adaptiveThreshold(gray, local_dark, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV, 31, 12);
    cv::threshold(S, saturated, params_.dyeing_sat_min, 255, cv::THRESH_BINARY);
    cv::bitwise_or(local_dark, saturated, cand);
    cv::bitwise_and(cand, roi, cand);
    cv::Mat k5 = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(cand, cand, cv::MORPH_CLOSE, k5);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(cand, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < params_.contam_area_min_px) continue;

        cv::Mat blob = cv::Mat::zeros(bgr.size(), CV_8U);
        cv::drawContours(blob, std::vector<std::vector<cv::Point>>{c}, 0, cv::Scalar(255), cv::FILLED);
        cv::Scalar mu_g, sd_g;
        cv::meanStdDev(gray, mu_g, sd_g, blob);
        double mean_s = cv::mean(S, blob)[0];

        DefectType type;
        if (mean_s >= params_.dyeing_sat_min) {
            type = DefectType::DYEING_CONTAMINATION;
        } else if (mu_g[0] <= params_.electrical_dark_max &&
                   sd_g[0] >= params_.electrical_texture_min) {
            type = DefectType::ELECTRICAL_CONTAMINATION;
        } else {
            type = DefectType::NON_ELECTRICAL_CONTAMINATION;
        }
        float conf = static_cast<float>(area / (10.0 * params_.contam_area_min_px));
        out.push_back(Make(type, conf, cv::boundingRect(c), cam,
                           "area " + std::to_string(static_cast<int>(area)) + " px, mean grey " +
                               std::to_string(static_cast<int>(mu_g[0])) + ", sat " +
                               std::to_string(static_cast<int>(mean_s))));
    }
    return out;
}
// ---------------------------------------------------------------------------
// Top-side pins and their epoxy (slides 35, 38, 44)
//   hole  = dark, low-saturation circular blob of the expected radius
//   pin   = bright metallic core inside the hole
//   epoxy = saturated amber ring around the hole
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectPinsTop(const cv::Mat& bgr, const CseGeometry& g,
                                                        cv::Mat& pin_mask_out) {
    std::vector<DefectResult> out;
    const CameraID cam = CameraID::CCD1_TOP;
    cv::Mat interior = locator_.InteriorMask(g);
    pin_mask_out = cv::Mat::zeros(bgr.size(), CV_8U);
    if (interior.empty()) return out;

    cv::Mat gray, hsv;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> ch(3);
    cv::split(hsv, ch);
    const cv::Mat& S = ch[1];
    const cv::Mat& V = ch[2];

    // hole candidates: dark and unsaturated (the amber ring and the ink are saturated)
    cv::Mat dark, unsat, holes;
    cv::threshold(gray, dark, params_.pin_head_dark_thresh, 255, cv::THRESH_BINARY_INV);
    cv::threshold(S, unsat, params_.epoxy_sat_min, 255, cv::THRESH_BINARY_INV);
    cv::bitwise_and(dark, unsat, holes);
    cv::bitwise_and(holes, interior, holes);
    cv::Mat k5 = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(holes, holes, cv::MORPH_CLOSE, k5);   // fill the core cavity

    // epoxy mask: amber hue, saturated, not too bright. The dark hole and the
    // metallic core are unsaturated and must not count as epoxy, otherwise a
    // nominal ring already measures ~1.8x its own area.
    cv::Mat epoxy;
    cv::inRange(hsv, cv::Scalar(params_.epoxy_hue_low, params_.epoxy_sat_min, 0),
                cv::Scalar(params_.epoxy_hue_high, 255, params_.epoxy_val_max), epoxy);
    cv::bitwise_and(epoxy, g.disc_mask, epoxy);
    cv::Mat epoxy_labels;
    cv::connectedComponents(epoxy, epoxy_labels, 8, CV_32S);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(holes, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const float r_nom = params_.pin_head_radius_px;
    const float ring = params_.epoxy_ring_px;
    int hole_count = 0;
    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < 10) continue;
        double per = cv::arcLength(c, true);
        double circ = per > 0 ? 4.0 * CV_PI * area / (per * per) : 0.0;
        cv::Point2f hc;
        float hr = 0.f;
        cv::minEnclosingCircle(c, hc, hr);
        if (circ < 0.6 || hr < 0.6f * r_nom || hr > 1.4f * r_nom) continue;
        ++hole_count;

        // pin core present?
        cv::Mat core_mask = cv::Mat::zeros(bgr.size(), CV_8U);
        cv::circle(core_mask, hc, static_cast<int>(0.6f * hr), cv::Scalar(255), cv::FILLED);
        cv::Mat bright;
        cv::threshold(V, bright, params_.pin_core_val_min, 255, cv::THRESH_BINARY);
        cv::bitwise_and(bright, core_mask, bright);
        int core_px = cv::countNonZero(bright);
        cv::Rect hole_bb = cv::boundingRect(c);
        if (core_px < params_.pin_core_area_min_px) {
            out.push_back(Make(DefectType::PIN_MISSING, 1.f, hole_bb, cam,
                               "hole without pin core"));
        }

        // epoxy ring fill
        cv::Mat ring_mask = cv::Mat::zeros(bgr.size(), CV_8U);
        cv::circle(ring_mask, hc, static_cast<int>(hr + ring), cv::Scalar(255), cv::FILLED);
        cv::circle(ring_mask, hc, static_cast<int>(hr), cv::Scalar(0), cv::FILLED);
        int ring_px = cv::countNonZero(ring_mask);
        cv::Mat ring_epoxy;
        cv::bitwise_and(epoxy, ring_mask, ring_epoxy);
        double fill = ring_px > 0 ? static_cast<double>(cv::countNonZero(ring_epoxy)) / ring_px : 0.0;
        cv::Rect ring_bb(static_cast<int>(hc.x - hr - ring), static_cast<int>(hc.y - hr - ring),
                         static_cast<int>(2 * (hr + ring)), static_cast<int>(2 * (hr + ring)));
        ring_bb &= cv::Rect(0, 0, bgr.cols, bgr.rows);
        if (fill < params_.epoxy_ring_min_fill) {
            out.push_back(Make(DefectType::INSUFFICIENT_EPOXY, static_cast<float>(1.0 - fill), ring_bb,
                               cam, "epoxy ring fill " + std::to_string(static_cast<int>(fill * 100)) + "%"));
        }

        // epoxy overflow: the connected epoxy component touching the ring is too large
        double nominal = CV_PI * ((hr + ring) * (hr + ring) - hr * hr);
        int label = 0;
        for (int y = ring_bb.y; y < ring_bb.y + ring_bb.height && label == 0; ++y)
            for (int x = ring_bb.x; x < ring_bb.x + ring_bb.width; ++x)
                if (ring_epoxy.at<uchar>(y, x)) { label = epoxy_labels.at<int>(y, x); break; }
        if (label > 0) {
            cv::Mat comp = (epoxy_labels == label);
            int comp_px = cv::countNonZero(comp);
            if (comp_px > params_.epoxy_overflow_ratio * nominal) {
                cv::Rect comp_bb = cv::boundingRect(comp);
                out.push_back(Make(DefectType::EPOXY_OVERFLOW,
                                   static_cast<float>(comp_px / (2.0 * params_.epoxy_overflow_ratio * nominal)),
                                   comp_bb, cam,
                                   "epoxy area " + std::to_string(comp_px) + " px around pin"));
                cv::Mat comp_d;
                cv::dilate(comp, comp_d, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(13, 13)));
                cv::bitwise_or(pin_mask_out, comp_d, pin_mask_out);
            }
        }

        cv::circle(pin_mask_out, hc, static_cast<int>(hr + ring + 6), cv::Scalar(255), cv::FILLED);
    }

    if (hole_count < params_.expected_pin_count) {
        int r = static_cast<int>(g.radius_px);
        cv::Rect bb(static_cast<int>(g.center.x) - r, static_cast<int>(g.center.y) - r, 2 * r, 2 * r);
        out.push_back(Make(DefectType::PIN_MISSING, 1.f, bb & cv::Rect(0, 0, bgr.cols, bgr.rows), cam,
                           std::to_string(hole_count) + " of " +
                               std::to_string(params_.expected_pin_count) + " pins found"));
    }
    return out;
}

// ---------------------------------------------------------------------------
// EPOXY_EXPOSAL — grey, granular (high local texture) patch on the ceramic (slide 34)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectEpoxyExposal(const cv::Mat& bgr, const CseGeometry& g,
                                                             const cv::Mat& exclude_mask) {
    std::vector<DefectResult> out;
    cv::Mat roi = locator_.InteriorMask(g);
    if (roi.empty()) return out;
    if (!exclude_mask.empty()) {
        cv::Mat not_excl;
        cv::bitwise_not(exclude_mask, not_excl);
        cv::bitwise_and(roi, not_excl, roi);
    }

    cv::Mat gray, hsv;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    cv::Mat S;
    cv::extractChannel(hsv, S, 1);

    // local std-dev over 7x7
    cv::Mat f, mean, mean_sq, var;
    gray.convertTo(f, CV_32F);
    cv::boxFilter(f, mean, CV_32F, cv::Size(7, 7));
    cv::boxFilter(f.mul(f), mean_sq, CV_32F, cv::Size(7, 7));
    var = mean_sq - mean.mul(mean);
    cv::Mat stddev;
    cv::sqrt(cv::max(var, 0.f), stddev);

    cv::Mat textured = stddev > params_.exposal_texture_min;
    cv::Mat unsat;
    cv::threshold(S, unsat, params_.exposal_sat_max, 255, cv::THRESH_BINARY_INV);
    cv::Mat cand;
    cv::bitwise_and(textured, unsat, cand);
    cv::bitwise_and(cand, roi, cand);
    cv::Mat k5 = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(cand, cand, cv::MORPH_CLOSE, k5);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(cand, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < params_.exposal_area_min_px) continue;
        out.push_back(Make(DefectType::EPOXY_EXPOSAL, static_cast<float>(area / (4.0 * params_.exposal_area_min_px)),
                           cv::boundingRect(c), CameraID::CCD1_TOP,
                           "granular patch " + std::to_string(static_cast<int>(area)) + " px"));
    }
    return out;
}

// ---------------------------------------------------------------------------
// NO_CODE / CODE_BLUR — the marking ROI follows the key notch (slides 41, 42)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectCode(const cv::Mat& bgr, const CseGeometry& g,
                                                     cv::Mat& code_mask_out) {
    std::vector<DefectResult> out;
    const CameraID cam = CameraID::CCD1_TOP;
    code_mask_out = cv::Mat::zeros(bgr.size(), CV_8U);

    // ROI centre = disc centre + code_offset_px along the notch direction;
    // without a notch the code is assumed below the centre (notch at +y).
    float notch_deg = std::isnan(g.notch_angle_deg) ? 90.f : g.notch_angle_deg;
    float a = notch_deg * static_cast<float>(CV_PI) / 180.f;
    cv::Point2f centre(g.center.x + params_.code_offset_px * std::cos(a),
                       g.center.y + params_.code_offset_px * std::sin(a));
    float rot_deg = notch_deg - 90.f;

    cv::Size roi_size(params_.code_roi_w, params_.code_roi_h);
    cv::Mat rot = cv::getRotationMatrix2D(centre, rot_deg, 1.0);
    rot.at<double>(0, 2) += roi_size.width / 2.0 - centre.x;
    rot.at<double>(1, 2) += roi_size.height / 2.0 - centre.y;
    cv::Mat gray, code;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::warpAffine(gray, code, rot, roi_size, cv::INTER_LINEAR, cv::BORDER_REPLICATE);

    // mask of the ROI polygon in source coordinates (for the other detectors)
    // RotatedRect's angle and getRotationMatrix2D's angle share the same sign
    // convention in image coordinates; the mask must cover the warped ROI itself.
    cv::RotatedRect rr(centre, cv::Size2f(static_cast<float>(roi_size.width + 12),
                                          static_cast<float>(roi_size.height + 12)), rot_deg);
    cv::Point2f v[4];
    rr.points(v);
    std::vector<cv::Point> poly = {v[0], v[1], v[2], v[3]};
    cv::fillConvexPoly(code_mask_out, poly, cv::Scalar(255));
    cv::Rect roi_bb = cv::boundingRect(poly) & cv::Rect(0, 0, bgr.cols, bgr.rows);

    double mean = cv::mean(code)[0];
    cv::Mat ink;
    cv::threshold(code, ink, mean - params_.code_ink_contrast, 255, cv::THRESH_BINARY_INV);
    double ink_ratio = static_cast<double>(cv::countNonZero(ink)) / code.total();

    if (ink_ratio < params_.code_min_ink_ratio) {
        out.push_back(Make(DefectType::NO_CODE, 1.f, roi_bb, cam,
                           "ink ratio " + std::to_string(ink_ratio)));
        return out;
    }

    cv::Mat lap;
    cv::Laplacian(code, lap, CV_64F);
    cv::Scalar mu, sigma;
    cv::meanStdDev(lap, mu, sigma);
    double variance = sigma[0] * sigma[0];
    if (variance < params_.code_laplacian_blur_limit) {
        out.push_back(Make(DefectType::CODE_BLUR,
                           static_cast<float>(1.0 - variance / params_.code_laplacian_blur_limit), roi_bb,
                           cam, "Laplacian variance " + std::to_string(variance)));
    }
    return out;
}
// ---------------------------------------------------------------------------
// Side-view pins (slide 43). Internal orientation: pins hang below the body.
//   bent     line fit angle from vertical
//   mis-cut  length outside the nominal band
//   bur      tip rows wider than the pin body
//   oxidized pin is dull/dark instead of bright gold
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectPinsSide(const cv::Mat& bgr, const cv::Mat& fg,
                                                         const cv::Rect& body) {
    std::vector<DefectResult> out;
    const CameraID cam = CameraID::CCD2_SIDE;
    const int y_b = body.y + body.height;
    if (y_b + 2 >= fg.rows) return out;

    cv::Mat below = cv::Mat::zeros(fg.size(), CV_8U);
    fg.rowRange(y_b + 2, fg.rows).copyTo(below.rowRange(y_b + 2, fg.rows));

    cv::Mat labels, stats, centroids;
    int n = cv::connectedComponentsWithStats(below, labels, stats, centroids, 8, CV_32S);

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> ch(3);
    cv::split(hsv, ch);

    for (int i = 1; i < n; ++i) {
        int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
        int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        int x0 = stats.at<int>(i, cv::CC_STAT_LEFT);
        int y0 = stats.at<int>(i, cv::CC_STAT_TOP);
        if (area < 20) continue;
        cv::Rect bb(x0, y0, w, h);
        cv::Mat comp = (labels(bb) == i);
        std::vector<cv::Point> pts;
        cv::findNonZero(comp, pts);
        // pin-likeness must not depend on the tilt: use the minimum-area rectangle
        cv::RotatedRect mar = cv::minAreaRect(pts);
        float long_side = std::max(mar.size.width, mar.size.height);
        float short_side = std::max(1.f, std::min(mar.size.width, mar.size.height));
        if (y0 > y_b + 2 + params_.pin_w_max) continue;   // does not start at the body edge
        if (long_side < 0.3 * params_.pin_length_px || long_side / short_side < 3.f) {
            // Not pin-shaped. A substantial blob hanging from the body is still a
            // pin problem (pins touching, folded pin) and must not pass silently.
            if (area >= 0.5 * params_.pin_length_px * params_.pin_w_max) {
                out.push_back(Make(DefectType::PIN_BENT, 1.f, bb, cam,
                                   "abnormal pin cluster " + std::to_string(area) + " px"));
            }
            continue;
        }

        // --- bent: angle of the fitted line from the vertical
        cv::Vec4f line;
        cv::fitLine(pts, line, cv::DIST_L2, 0, 0.01, 0.01);
        double angle = std::atan2(std::fabs(line[0]), std::fabs(line[1])) * 180.0 / CV_PI;
        if (angle > params_.pin_bend_angle_deg_max) {
            out.push_back(Make(DefectType::PIN_BENT, static_cast<float>(angle / (5.0 * params_.pin_bend_angle_deg_max)),
                               bb, cam, "pin angle " + std::to_string(angle) + " deg"));
        }

        // --- mis-cut: length from the body edge to the tip
        int length = (y0 + h) - y_b;
        if (std::abs(length - params_.pin_length_px) > params_.pin_length_tol_px) {
            out.push_back(Make(DefectType::PIN_MIS_CUT,
                               static_cast<float>(std::abs(length - params_.pin_length_px) / (3.0 * params_.pin_length_tol_px)),
                               bb, cam, "pin length " + std::to_string(length) + " px"));
        }

        // --- bur: tip rows wider than the pin body
        std::vector<int> widths(h);
        for (int r = 0; r < h; ++r) widths[r] = cv::countNonZero(comp.row(r));
        std::vector<int> sorted(widths);
        std::sort(sorted.begin(), sorted.end());
        int median_w = sorted[h / 2];
        int tip_max = 0;
        for (int r = std::max(0, h - 6); r < h; ++r) tip_max = std::max(tip_max, widths[r]);
        if (tip_max - median_w > params_.pin_bur_width_px) {
            cv::Rect tip(bb.x, bb.y + std::max(0, h - 6), bb.width, std::min(6, h));
            out.push_back(Make(DefectType::PIN_BUR, static_cast<float>((tip_max - median_w) / (3.0 * params_.pin_bur_width_px)),
                               tip, cam, "tip width " + std::to_string(tip_max) + " vs " + std::to_string(median_w)));
        }

        // --- oxidized: dull / dark pin instead of bright gold
        cv::Mat full = cv::Mat::zeros(fg.size(), CV_8U);
        comp.copyTo(full(bb));
        cv::Mat sat_mask;
        cv::threshold(ch[1], sat_mask, 60, 255, cv::THRESH_BINARY);
        cv::bitwise_and(sat_mask, full, sat_mask);
        cv::Mat vmask = cv::countNonZero(sat_mask) > 0 ? sat_mask : full;
        double mean_v = cv::mean(ch[2], vmask)[0];
        if (mean_v < params_.pin_oxide_val_max) {
            out.push_back(Make(DefectType::PIN_OXIDIZED, static_cast<float>(1.0 - mean_v / params_.pin_oxide_val_max),
                               bb, cam, "pin brightness " + std::to_string(static_cast<int>(mean_v))));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// GOLD_EXPOSAL — brown/gold patch on the white ceramic side wall (slide 37)
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectGoldExposal(const cv::Mat& bgr, const cv::Rect& body) {
    std::vector<DefectResult> out;
    cv::Rect roi = body & cv::Rect(0, 0, bgr.cols, bgr.rows);
    if (roi.area() <= 0) return out;
    cv::Mat hsv;
    cv::cvtColor(bgr(roi), hsv, cv::COLOR_BGR2HSV);
    cv::Mat gold;
    cv::inRange(hsv, cv::Scalar(10, 60, 60), cv::Scalar(35, 255, 220), gold);
    cv::Mat k3 = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(gold, gold, cv::MORPH_OPEN, k3);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(gold, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < params_.gold_area_min_px) continue;
        cv::Rect bb = cv::boundingRect(c);
        bb.x += roi.x;
        bb.y += roi.y;
        out.push_back(Make(DefectType::GOLD_EXPOSAL, static_cast<float>(area / (5.0 * params_.gold_area_min_px)), bb,
                           CameraID::CCD2_SIDE, "gold patch " + std::to_string(static_cast<int>(area)) + " px"));
    }
    return out;
}

// ---------------------------------------------------------------------------
// EPOXY_HIGHER_THAN_CERAMIC — bump in the body's top-edge profile
// ---------------------------------------------------------------------------
std::vector<DefectResult> DefectDetector::DetectEpoxyBump(const cv::Mat& fg, const cv::Rect& body) {
    std::vector<DefectResult> out;
    if (body.width <= 0) return out;
    const int search_top = std::max(0, body.y - 4 * params_.epoxy_bump_px);
    const int search_bot = std::min(fg.rows - 1, body.y + body.height / 2);

    std::vector<int> top(body.width, -1);
    for (int i = 0; i < body.width; ++i) {
        int x = body.x + i;
        if (x < 0 || x >= fg.cols) continue;
        for (int y = search_top; y <= search_bot; ++y) {
            if (fg.at<uchar>(y, x)) { top[i] = y; break; }
        }
    }
    std::vector<int> valid;
    for (int t : top) if (t >= 0) valid.push_back(t);
    if (valid.empty()) return out;
    std::sort(valid.begin(), valid.end());
    int median_top = valid[valid.size() / 2];

    int run_start = -1;
    for (int i = 0; i <= body.width; ++i) {
        bool bump = i < body.width && top[i] >= 0 && (median_top - top[i]) > params_.epoxy_bump_px;
        if (bump && run_start < 0) run_start = i;
        if (!bump && run_start >= 0) {
            int len = i - run_start;
            if (len >= params_.epoxy_bump_width_px) {
                int rise = 0;
                for (int k = run_start; k < i; ++k) rise = std::max(rise, median_top - top[k]);
                cv::Rect bb(body.x + run_start, median_top - rise, len, rise + 2);
                out.push_back(Make(DefectType::EPOXY_HIGHER_THAN_CERAMIC,
                                   static_cast<float>(rise / (3.0 * params_.epoxy_bump_px)), bb,
                                   CameraID::CCD2_SIDE,
                                   "bump " + std::to_string(rise) + " px over " + std::to_string(len) + " columns"));
            }
            run_start = -1;
        }
    }
    return out;
}

}  // namespace aoi
