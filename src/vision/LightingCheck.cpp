/**
 * LightingCheck implementation — filled in by Task 5.
 *
 * Author: Rongxuan Zhou
 */

#include "LightingCheck.h"

namespace aoi {

LightingCheck::LightingCheck(LightingParams p) : p_(p) {}

LightingResult LightingCheck::Analyse(const cv::Mat& bgr) const {
    LightingResult r;
    if (bgr.empty()) return r;
    return r;
}

}  // namespace aoi
