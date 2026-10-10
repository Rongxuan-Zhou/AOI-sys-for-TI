#pragma once
/**
 * CSE disc geometry shared by every CCD station.
 *
 * The CSE is a round white ceramic disc with one semicircular key notch on
 * the rim (spec slides 30-45). Every detector needs the same three facts:
 * where the disc is, how big it is, and where the key notch points. This
 * unit answers them once:
 *
 *   - disc centre and radius from a two-pass least-squares circle fit of the
 *     outer contour (inlier filtering keeps a chip or the notch from pulling
 *     the fit),
 *   - key-notch angle, depth and width from the rim profile,
 *   - helper masks and a polar unwrap used by the rim and annulus checks.
 *
 * Author: Rongxuan Zhou
 */

#include <cmath>
#include <vector>

#include <opencv2/core.hpp>

namespace aoi {

struct CseGeometry {
    bool        found = false;
    cv::Point2f center;                   // px, image coordinates
    float       radius_px = 0.f;
    float       notch_angle_deg = NAN;    // 0 = +x, 90 = +y (down); NAN if no notch found
    float       notch_depth_px = 0.f;
    float       notch_width_deg = 0.f;
    cv::Mat     disc_mask;                // CV_8U, 255 inside the fitted circle
    std::vector<cv::Point> contour;       // outer contour of the bright blob
    cv::Mat     fg_mask;                  // foreground (Otsu) mask used for the rim profile
};

struct LocatorParams {
    int   min_disc_radius_px = 60;    // smaller bright blobs are not a disc
    int   min_otsu_thresh    = 30;    // Otsu level below this = no real foreground (dark frame)
    int   disc_fg_thresh     = 40;    // V above this is disc (epoxy and pin holes included)
    float notch_width_deg    = 7.f;   // nominal angular width of the key notch
    float notch_depth_px     = 10.f;  // nominal radial depth of the key notch
    float fit_inlier_ratio   = 0.03f; // |r - R| / R accepted for the second fit pass
    int   interior_margin_px = 20;    // rim band excluded from on-disc detectors
};

class CseLocator {
public:
    explicit CseLocator(LocatorParams p = {});

    /// Locate the disc in a BGR or grey frame. Never throws; `found == false` on failure.
    CseGeometry Locate(const cv::Mat& bgr_or_gray) const;

    /// Outermost foreground radius (px) along each of n_angles rays from the centre.
    std::vector<float> RimProfile(const CseGeometry& g, int n_angles = 720) const;

    /// disc_mask eroded by interior_margin_px, minus a circle of 1.5 * notch_depth_px
    /// at the notch position. Empty Mat when `g.found` is false.
    cv::Mat InteriorMask(const CseGeometry& g) const;

    /// Unwrap the annulus [r_in, r_out] around c into (rows = radius, cols = angle).
    static cv::Mat PolarUnwrap(const cv::Mat& img, cv::Point2f c, float r_in, float r_out,
                               int out_w, int out_h);

    /// Algebraic (Kasa) least-squares circle fit. Returns false for degenerate input.
    static bool FitCircleLSQ(const std::vector<cv::Point>& pts, cv::Point2f& c, float& r);

    const LocatorParams& params() const { return p_; }

private:
    LocatorParams p_;

    /// Foreground = V > disc_fg_thresh (Otsu level only guards against dark frames).
    bool Foreground(const cv::Mat& img, cv::Mat& fg) const;
};

}  // namespace aoi
