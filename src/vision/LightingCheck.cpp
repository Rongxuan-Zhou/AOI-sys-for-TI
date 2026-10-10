/**
 * LightingCheck implementation.
 *
 * Pipeline per frame:
 *   1. lit disc = largest Otsu-foreground blob on V (reject dark frames)
 *   2. nest ring = largest "not black" blob (V > nest_val_min); its centre
 *      gives the alignment reference (slide 45)
 *   3. polar unwrap of H, S, V over the annulus [inner, outer] * R
 *   4. per-radius ring percentile (default 75th) of V and S = colour-independent
 *      baseline that survives large dark structure (banana, hole shadows)
 *   5. residual masks → leakage (bright, compact), staining (dark, compact,
 *      outer band), cement (yellow hue, more saturated / brighter than ring)
 *   6. thickness class from the dominant annulus hue (reported only)
 *
 * Author: Rongxuan Zhou
 */

#include "LightingCheck.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <opencv2/imgproc.hpp>

#include "CseLocator.h"

namespace aoi {

LightingCheck::LightingCheck(LightingParams p) : p_(p) {}

namespace {

/// Per-row percentile of a CV_8U matrix (0.5 = median).
cv::Mat RowPercentile(const cv::Mat& m, double p) {
    cv::Mat sorted;
    cv::sort(m, sorted, cv::SORT_EVERY_ROW | cv::SORT_ASCENDING);
    int col = std::clamp(static_cast<int>(p * (m.cols - 1)), 0, m.cols - 1);
    cv::Mat out(m.rows, 1, CV_8U);
    for (int r = 0; r < m.rows; ++r) out.at<uchar>(r) = sorted.at<uchar>(r, col);
    return out;
}

/// Broadcast a column vector (rows x 1) across cols → CV_16S matrix.
cv::Mat Broadcast16S(const cv::Mat& col, int cols) {
    cv::Mat out(col.rows, cols, CV_16S);
    for (int r = 0; r < col.rows; ++r) out.row(r).setTo(static_cast<short>(col.at<uchar>(r)));
    return out;
}

bool LargestContour(const cv::Mat& mask, std::vector<cv::Point>& out) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) return false;
    auto it = std::max_element(contours.begin(), contours.end(),
                               [](const auto& a, const auto& b) {
                                   return cv::contourArea(a) < cv::contourArea(b);
                               });
    out = *it;
    return true;
}

}  // namespace

LightingResult LightingCheck::Analyse(const cv::Mat& bgr_in) const {
    LightingResult res;
    if (bgr_in.empty() || bgr_in.channels() != 3) return res;
    cv::Mat bgr = CseLocator::To8U(bgr_in);

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> ch(3);
    cv::split(hsv, ch);
    const cv::Mat& H = ch[0];
    const cv::Mat& S = ch[1];
    const cv::Mat& V = ch[2];

    // --- 1. lit disc -------------------------------------------------------
    cv::Mat fg;
    double level = cv::threshold(V, fg, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    if (level < p_.min_otsu_thresh) return res;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(fg, fg, cv::MORPH_OPEN, kernel);
    // close the dark centre so the disc is one blob
    std::vector<cv::Point> disc_contour;
    if (!LargestContour(fg, disc_contour)) return res;
    cv::Point2f c;
    float R = 0.f;
    cv::minEnclosingCircle(disc_contour, c, R);
    if (R < p_.min_lit_radius_px) return res;
    // refine with the inlier LSQ fit (dark centre does not affect the outer contour)
    cv::Point2f c2 = c;
    float R2 = R;
    if (CseLocator::FitCircleLSQ(disc_contour, c2, R2)) {
        std::vector<cv::Point> in;
        for (const auto& p : disc_contour)
            if (std::abs(std::hypot(p.x - c2.x, p.y - c2.y) - R2) <= 0.03f * R2) in.push_back(p);
        if (in.size() >= 3 && CseLocator::FitCircleLSQ(in, c2, R2) && R2 >= p_.min_lit_radius_px) {
            c = c2;
            R = R2;
        }
    }
    res.located = true;
    res.center = c;
    res.radius_px = R;

    // --- 2. nest ring (alignment reference) --------------------------------
    if (p_.nest_ring_enabled) {
        cv::Mat not_black;
        cv::threshold(V, not_black, p_.nest_val_min, 255, cv::THRESH_BINARY);
        cv::morphologyEx(not_black, not_black, cv::MORPH_CLOSE, kernel);
        std::vector<cv::Point> ring_contour;
        if (LargestContour(not_black, ring_contour)) {
            cv::Point2f rc;
            float rr = 0.f;
            cv::minEnclosingCircle(ring_contour, rc, rr);
            if (rr >= 1.1f * R) {
                res.nest_found = true;
                res.nest_center = rc;
                res.offset_mm = static_cast<float>(std::hypot(rc.x - c.x, rc.y - c.y) * p_.mm_per_px);
            }
        }
    }

    // --- 3. polar unwrap ---------------------------------------------------
    const float r_in = p_.inner_zone_ratio * R;
    const float r_out = p_.outer_zone_ratio * R;
    const int W = p_.unwrap_w, Hh = p_.unwrap_h;
    cv::Mat uH = CseLocator::PolarUnwrap(H, c, r_in, r_out, W, Hh);
    cv::Mat uS = CseLocator::PolarUnwrap(S, c, r_in, r_out, W, Hh);
    cv::Mat uV = CseLocator::PolarUnwrap(V, c, r_in, r_out, W, Hh);

    // --- 4. ring-median baselines -----------------------------------------
    cv::Mat baseV = Broadcast16S(RowPercentile(uV, p_.baseline_percentile), W);
    cv::Mat baseS = Broadcast16S(RowPercentile(uS, p_.baseline_percentile), W);
    cv::Mat v16, s16;
    uV.convertTo(v16, CV_16S);
    uS.convertTo(s16, CV_16S);
    cv::Mat resV = v16 - baseV;
    cv::Mat resS = s16 - baseS;

    // --- 6. thickness class (dominant hue over the annulus) ---------------
    {
        int hist[30] = {0};
        for (int r = 0; r < Hh; ++r)
            for (int col = 0; col < W; ++col)
                if (uV.at<uchar>(r, col) > 40) ++hist[std::min(29, uH.at<uchar>(r, col) / 6)];
        int best = static_cast<int>(std::max_element(hist, hist + 30) - hist);
        int hue = best * 6 + 3;
        if (hue >= 10 && hue <= 35)       res.thickness = ThicknessClass::AMBER;
        else if (hue >= 40 && hue <= 85)  res.thickness = ThicknessClass::GREEN;
        else if (hue >= 90 && hue <= 130) res.thickness = ThicknessClass::BLUE;
        else                              res.thickness = ThicknessClass::UNKNOWN;
    }

    // --- 5. blob extraction helpers ---------------------------------------
    auto to_source_bbox = [&](const cv::Rect& pb) {
        // map the 4 corners of a polar bbox back to image space
        std::vector<cv::Point> pts;
        const float a_step = 2.f * static_cast<float>(CV_PI) / W;
        const float r_step = (r_out - r_in) / Hh;
        for (int col : {pb.x, pb.x + pb.width}) {
            for (int row : {pb.y, pb.y + pb.height}) {
                float th = col * a_step;
                float r = r_in + row * r_step;
                pts.emplace_back(cvRound(c.x + r * std::cos(th)), cvRound(c.y + r * std::sin(th)));
            }
        }
        // also the arc midpoint so wide arcs are covered
        {
            float th = (pb.x + pb.width / 2.f) * a_step;
            float r = r_in + (pb.y + pb.height / 2.f) * r_step;
            pts.emplace_back(cvRound(c.x + r * std::cos(th)), cvRound(c.y + r * std::sin(th)));
        }
        cv::Rect bb = cv::boundingRect(pts);
        return bb & cv::Rect(0, 0, bgr.cols, bgr.rows);
    };

    auto compact = [&](const std::vector<cv::Point>& contour) {
        if (contour.size() < 5) return true;
        cv::RotatedRect rr = cv::minAreaRect(contour);
        float long_side = std::max(rr.size.width, rr.size.height);
        float short_side = std::max(1.f, std::min(rr.size.width, rr.size.height));
        return long_side / short_side <= p_.blob_aspect_max;
    };

    // The unwrap has a seam at angle 0. Pad each mask on both sides with the
    // wrapped columns so a blob straddling the seam is one contour; every blob
    // then appears twice and the copy whose centre lies in [pad, W + pad) is kept.
    const int pad = std::max(8, W / 36);
    auto extract = [&](const cv::Mat& mask_in, int area_min, int area_max, DefectType type,
                       int& area_out, const char* label, float conf_scale) {
        cv::Mat parts[3] = {mask_in.colRange(W - pad, W), mask_in, mask_in.colRange(0, pad)};
        cv::Mat mask;
        cv::hconcat(parts, 3, mask);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        for (const auto& ct : contours) {
            cv::Rect pb = cv::boundingRect(ct);
            float cx = pb.x + pb.width / 2.f;
            if (cx < pad || cx >= W + pad) continue;      // the duplicate copy
            pb.x -= pad;                                  // back to unpadded columns (may be < 0: wraps)
            double area = cv::contourArea(ct);
            if (area < area_min) continue;
            if (area_max > 0 && area > area_max) continue;
            if (!compact(ct)) continue;
            area_out += static_cast<int>(area);
            cv::Rect bb = to_source_bbox(pb);
            float conf = static_cast<float>(area) / (conf_scale * area_min);
            res.defects.push_back({type, SeverityOf(type), std::clamp(conf, 0.f, 1.f), bb,
                                   CameraID::CCD4_INNER,
                                   std::string(label) + " area " + std::to_string(static_cast<int>(area)) + " px"});
        }
    };

    // leakage: bright compact residual anywhere on the annulus
    cv::Mat leak = resV > p_.leak_residual_min;
    extract(leak, p_.leak_area_min_px, 0, DefectType::LIGHTING_CHECK, res.leak_area_px,
            "light leakage", 3.f);

    // staining on edge: dark compact residual in the outer band
    cv::Mat stain = resV < -p_.stain_residual_min;
    int band_row = static_cast<int>((p_.edge_band_ratio - p_.inner_zone_ratio) /
                                    (p_.outer_zone_ratio - p_.inner_zone_ratio) * Hh);
    band_row = std::clamp(band_row, 0, Hh - 1);
    stain.rowRange(0, band_row).setTo(0);
    extract(stain, p_.stain_area_min_px, p_.stain_area_max_px, DefectType::STAINING_ON_EDGE,
            res.stain_area_px, "edge staining", 3.f);

    // yellow glass cement: yellow hue AND (more saturated OR brighter than the ring)
    cv::Mat hue_ok;
    cv::inRange(uH, cv::Scalar(p_.cement_hue_low), cv::Scalar(p_.cement_hue_high), hue_ok);
    cv::Mat sat_up = resS > p_.cement_sat_delta;
    cv::Mat val_up = resV > p_.cement_val_delta;
    cv::Mat cement;
    cv::bitwise_or(sat_up, val_up, cement);
    cv::bitwise_and(cement, hue_ok, cement);
    extract(cement, p_.cement_area_min_px, 0, DefectType::YELLOW_GLASS_CEMENT, res.cement_area_px,
            "yellow glass cement", 3.f);

    res.passed = res.defects.empty();
    return res;
}

}  // namespace aoi
