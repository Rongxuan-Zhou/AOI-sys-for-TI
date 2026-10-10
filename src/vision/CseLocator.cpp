/**
 * CseLocator implementation.
 *
 * Author: Rongxuan Zhou
 */

#include "CseLocator.h"

#include <algorithm>

#include <opencv2/imgproc.hpp>

namespace aoi {

CseLocator::CseLocator(LocatorParams p) : p_(p) {}

// ---------------------------------------------------------------------------
// Foreground: Otsu on the V channel. A dark or noise-only frame has an Otsu
// level below min_otsu_thresh and is rejected instead of being split into a
// fake disc.
// ---------------------------------------------------------------------------
bool CseLocator::Foreground(const cv::Mat& img, cv::Mat& fg) const {
    cv::Mat gray;
    if (img.channels() == 3) {
        cv::Mat hsv;
        cv::cvtColor(img, hsv, cv::COLOR_BGR2HSV);
        cv::extractChannel(hsv, gray, 2);
    } else if (img.channels() == 1) {
        gray = img;
    } else {
        return false;
    }

    double level = cv::threshold(gray, fg, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    if (level < p_.min_otsu_thresh) return false;

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(fg, fg, cv::MORPH_OPEN, kernel);
    return true;
}

// ---------------------------------------------------------------------------
// Kasa fit: minimise sum((x^2 + y^2) + a x + b y + c)^2
// ---------------------------------------------------------------------------
bool CseLocator::FitCircleLSQ(const std::vector<cv::Point>& pts, cv::Point2f& c, float& r) {
    if (pts.size() < 3) return false;
    cv::Mat A(static_cast<int>(pts.size()), 3, CV_64F);
    cv::Mat b(static_cast<int>(pts.size()), 1, CV_64F);
    for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
        double x = pts[i].x, y = pts[i].y;
        A.at<double>(i, 0) = x;
        A.at<double>(i, 1) = y;
        A.at<double>(i, 2) = 1.0;
        b.at<double>(i) = -(x * x + y * y);
    }
    cv::Mat sol;
    if (!cv::solve(A, b, sol, cv::DECOMP_SVD)) return false;
    double a = sol.at<double>(0), bb = sol.at<double>(1), cc = sol.at<double>(2);
    double r2 = a * a / 4.0 + bb * bb / 4.0 - cc;
    if (r2 <= 0) return false;
    c = cv::Point2f(static_cast<float>(-a / 2.0), static_cast<float>(-bb / 2.0));
    r = static_cast<float>(std::sqrt(r2));
    return true;
}

// ---------------------------------------------------------------------------
// Locate
// ---------------------------------------------------------------------------
CseGeometry CseLocator::Locate(const cv::Mat& img) const {
    CseGeometry g;
    if (img.empty()) return g;

    cv::Mat fg;
    if (!Foreground(img, fg)) return g;

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(fg, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) return g;

    auto largest = std::max_element(contours.begin(), contours.end(),
                                    [](const auto& a, const auto& b) {
                                        return cv::contourArea(a) < cv::contourArea(b);
                                    });
    cv::Point2f enc_c;
    float enc_r = 0.f;
    cv::minEnclosingCircle(*largest, enc_c, enc_r);
    if (enc_r < p_.min_disc_radius_px) return g;

    // Pass 1: all contour points. Pass 2: radial inliers only, so the notch
    // or a chipped rim does not pull the fit.
    cv::Point2f c = enc_c;
    float r = enc_r;
    if (!FitCircleLSQ(*largest, c, r)) return g;
    std::vector<cv::Point> inliers;
    inliers.reserve(largest->size());
    for (const auto& p : *largest) {
        float d = std::hypot(p.x - c.x, p.y - c.y);
        if (std::abs(d - r) <= p_.fit_inlier_ratio * r) inliers.push_back(p);
    }
    if (inliers.size() >= 3) FitCircleLSQ(inliers, c, r);
    if (r < p_.min_disc_radius_px) return g;

    g.found = true;
    g.center = c;
    g.radius_px = r;
    g.contour = *largest;
    g.fg_mask = fg;
    g.disc_mask = cv::Mat::zeros(img.size(), CV_8U);
    cv::circle(g.disc_mask, c, static_cast<int>(std::lround(r)), cv::Scalar(255), cv::FILLED);

    // Key notch: contiguous runs of rim residual above 0.4 * nominal depth,
    // wrapped around 360 deg. The run with a plausible width and the largest
    // depth is the notch.
    const int n = 720;
    std::vector<float> prof = RimProfile(g, n);
    std::vector<char> flag(n);
    const float d_thr = 0.4f * p_.notch_depth_px;
    for (int i = 0; i < n; ++i) flag[i] = (r - prof[i]) > d_thr;

    float best_depth = 0.f;
    for (int i = 0; i < n; ++i) {
        if (!flag[i] || flag[(i + n - 1) % n]) continue;  // run start only
        int len = 0;
        float depth = 0.f;
        int j = i;
        while (flag[j % n] && len < n) {
            depth = std::max(depth, r - prof[j % n]);
            ++len;
            ++j;
        }
        float width_deg = len * 360.f / n;
        if (width_deg < 0.4f * p_.notch_width_deg || width_deg > 2.0f * p_.notch_width_deg)
            continue;
        if (depth > best_depth) {
            best_depth = depth;
            float centre_idx = i + (len - 1) / 2.f;
            g.notch_angle_deg = std::fmod(centre_idx * 360.f / n, 360.f);
            g.notch_depth_px = depth;
            g.notch_width_deg = width_deg;
        }
    }
    return g;
}

// ---------------------------------------------------------------------------
// Rim profile: march inward from 1.08 R until the foreground mask is hit.
// ---------------------------------------------------------------------------
std::vector<float> CseLocator::RimProfile(const CseGeometry& g, int n_angles) const {
    std::vector<float> prof(static_cast<size_t>(std::max(n_angles, 1)), 0.f);
    if (!g.found || g.fg_mask.empty()) return prof;
    const float r_start = 1.08f * g.radius_px;
    const float r_stop = 0.6f * g.radius_px;
    for (int i = 0; i < n_angles; ++i) {
        float th = i * 2.f * static_cast<float>(CV_PI) / n_angles;
        float cs = std::cos(th), sn = std::sin(th);
        prof[i] = r_stop;
        for (float rr = r_start; rr >= r_stop; rr -= 0.5f) {
            int x = cvRound(g.center.x + rr * cs);
            int y = cvRound(g.center.y + rr * sn);
            if (x < 0 || y < 0 || x >= g.fg_mask.cols || y >= g.fg_mask.rows) continue;
            if (g.fg_mask.at<uchar>(y, x)) {
                prof[i] = rr;
                break;
            }
        }
    }
    return prof;
}

// ---------------------------------------------------------------------------
// Interior mask
// ---------------------------------------------------------------------------
cv::Mat CseLocator::InteriorMask(const CseGeometry& g) const {
    if (!g.found) return {};
    cv::Mat m;
    int k = 2 * p_.interior_margin_px + 1;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
    cv::erode(g.disc_mask, m, kernel);
    if (!std::isnan(g.notch_angle_deg)) {
        float a = g.notch_angle_deg * static_cast<float>(CV_PI) / 180.f;
        cv::Point2f n(g.center.x + g.radius_px * std::cos(a),
                      g.center.y + g.radius_px * std::sin(a));
        int rr = static_cast<int>(std::lround(1.5f * p_.notch_depth_px + p_.interior_margin_px));
        cv::circle(m, n, rr, cv::Scalar(0), cv::FILLED);
    }
    return m;
}

// ---------------------------------------------------------------------------
// Polar unwrap
// ---------------------------------------------------------------------------
cv::Mat CseLocator::PolarUnwrap(const cv::Mat& img, cv::Point2f c, float r_in, float r_out,
                                int out_w, int out_h) {
    cv::Mat map_x(out_h, out_w, CV_32F), map_y(out_h, out_w, CV_32F);
    const float a_step = 2.f * static_cast<float>(CV_PI) / out_w;
    const float r_step = (r_out - r_in) / out_h;
    for (int row = 0; row < out_h; ++row) {
        float r = r_in + row * r_step;
        for (int col = 0; col < out_w; ++col) {
            float th = col * a_step;
            map_x.at<float>(row, col) = c.x + r * std::cos(th);
            map_y.at<float>(row, col) = c.y + r * std::sin(th);
        }
    }
    cv::Mat out;
    cv::remap(img, out, map_x, map_y, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    return out;
}

}  // namespace aoi
