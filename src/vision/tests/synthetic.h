#pragma once
/**
 * Synthetic image generators shared by the vision unit tests.
 * They imitate the CSE views described in the V1.4 spec at a small scale
 * (640-800 px frames) so the tests run without hardware or sample images.
 *
 * Author: Rongxuan Zhou
 */

#include <cmath>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace aoi::test {

/// White ceramic disc on black. Notch angle in image convention (0 = +x, 90 = +y/down).
inline cv::Mat MakeDisc(int w, int h, cv::Point2f c, float r, bool notch = true,
                        float notch_angle_deg = 270.f, float notch_r = 0.f) {
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(8, 8, 8));
    cv::circle(img, c, static_cast<int>(r), cv::Scalar(235, 238, 240), cv::FILLED, cv::LINE_AA);
    if (notch) {
        if (notch_r <= 0) notch_r = r * 0.06f;
        float a = notch_angle_deg * static_cast<float>(CV_PI) / 180.f;
        cv::Point2f n(c.x + r * std::cos(a), c.y + r * std::sin(a));
        cv::circle(img, n, static_cast<int>(notch_r), cv::Scalar(8, 8, 8), cv::FILLED, cv::LINE_AA);
    }
    return img;
}

/// Pin hole with epoxy ring. has_pin draws the bright gold core; ring_fill in [0,1];
/// overflow_px grows the epoxy blob beyond the nominal ring.
inline void DrawPinHead(cv::Mat& img, cv::Point2f p, float r_head, float ring_px,
                        float ring_fill = 1.f, float overflow_px = 0.f, bool has_pin = true) {
    const cv::Scalar epoxy(40, 90, 120);
    if (overflow_px > 0)
        cv::circle(img, p, static_cast<int>(r_head + ring_px + overflow_px), epoxy, cv::FILLED, cv::LINE_AA);
    int ring_r = static_cast<int>(r_head + ring_px / 2);
    cv::ellipse(img, p, cv::Size(ring_r, ring_r), 0, 0, 360.0 * ring_fill, epoxy,
                static_cast<int>(ring_px), cv::LINE_AA);
    cv::circle(img, p, static_cast<int>(r_head), cv::Scalar(70, 80, 70), cv::FILLED, cv::LINE_AA);  // dark hole
    if (has_pin)
        cv::circle(img, p, static_cast<int>(r_head * 0.4f), cv::Scalar(120, 200, 240), cv::FILLED,
                   cv::LINE_AA);  // gold core
}

/// Two-line laser code; blur=true applies a mild Gaussian blur to the code area only.
inline void DrawCode(cv::Mat& img, cv::Point2f center, bool blur = false) {
    cv::putText(img, "27501-76HC", center + cv::Point2f(-70, -8), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                cv::Scalar(30, 30, 40), 2);
    cv::putText(img, "52149001-8", center + cv::Point2f(-70, 18), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                cv::Scalar(30, 30, 40), 2);
    if (blur) {
        cv::Rect roi(static_cast<int>(center.x) - 90, static_cast<int>(center.y) - 30, 180, 60);
        roi &= cv::Rect(0, 0, img.cols, img.rows);
        cv::GaussianBlur(img(roi), img(roi), cv::Size(7, 7), 2);
    }
}

/// Grey (single-channel) noise patch converted to BGR so saturation stays ~0.
inline void DrawGreyNoise(cv::Mat& img, cv::Rect r, int lo, int hi) {
    cv::Mat g(r.size(), CV_8UC1);
    cv::randu(g, lo, hi);
    cv::Mat bgr;
    cv::cvtColor(g, bgr, cv::COLOR_GRAY2BGR);
    bgr.copyTo(img(r));
}

/// Side view in the detector's internal orientation (pins below the body).
/// pin_colour defaults to bright gold like a healthy pin.
inline cv::Mat MakeSideView(int w, int h, cv::Rect body, const std::vector<int>& pin_x,
                            const std::vector<float>& pin_angles_deg,
                            const std::vector<int>& pin_len_px, int pin_w = 6,
                            cv::Scalar pin_colour = cv::Scalar(40, 170, 220)) {
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(8, 8, 8));
    cv::rectangle(img, body, cv::Scalar(230, 232, 235), cv::FILLED);
    for (size_t i = 0; i < pin_x.size(); ++i) {
        float a = pin_angles_deg[i] * static_cast<float>(CV_PI) / 180.f;
        cv::Point2f top(static_cast<float>(pin_x[i]), static_cast<float>(body.y + body.height));
        cv::Point2f tip(top.x + pin_len_px[i] * std::sin(a), top.y + pin_len_px[i] * std::cos(a));
        cv::line(img, top, tip, pin_colour, pin_w, cv::LINE_AA);
    }
    return img;
}

/// CCD4 transmitted-light frame: dark background, tinted annulus [0.35R, R] with radial
/// falloff, dark centre. With nest_ring=true a dark-grey nest ring of radius 1.25R is drawn
/// around (centre + ring_offset).
inline cv::Mat MakeLitAnnulus(int w, int h, cv::Point2f c, float R, cv::Scalar bgr_tint,
                              bool nest_ring = false, cv::Point2f ring_offset = {0, 0}) {
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(5, 5, 5));
    if (nest_ring)
        cv::circle(img, c + ring_offset, static_cast<int>(R * 1.25f), cv::Scalar(60, 60, 60),
                   static_cast<int>(R * 0.12f), cv::LINE_AA);
    cv::Mat disc(h, w, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::circle(disc, c, static_cast<int>(R), bgr_tint, cv::FILLED, cv::LINE_AA);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float d = std::hypot(x - c.x, y - c.y) / R;
            if (d <= 1.f) {
                cv::Vec3b& v = disc.at<cv::Vec3b>(y, x);
                for (int k = 0; k < 3; ++k) v[k] = static_cast<uchar>(v[k] * (1.f - 0.25f * d));
                img.at<cv::Vec3b>(y, x) = v;
            }
        }
    }
    cv::circle(img, c, static_cast<int>(R * 0.35f), cv::Scalar(10, 10, 10), cv::FILLED, cv::LINE_AA);
    return img;
}

}  // namespace aoi::test
