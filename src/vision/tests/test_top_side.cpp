#include <gtest/gtest.h>

#include "DefectDetector.h"
#include "synthetic.h"

using namespace aoi;

static bool Has(const std::vector<DefectResult>& v, DefectType t) {
    for (const auto& r : v) if (r.type == t) return true;
    return false;
}

// Top-side synthetic part: disc R=220 at (400,300), notch at 90 deg, 4 pin holes >= 90 px apart,
// two-line code 30 px below the centre.
static cv::Mat TopPart(int holes = 4, float fill = 1.f, float overflow = 0.f, bool code = true,
                       bool blur = false, int empty_hole = -1) {
    cv::Mat img = test::MakeDisc(800, 600, {400, 300}, 220.f, true, 90.f, 12.f);
    const cv::Point2f P[4] = {{265, 200}, {355, 160}, {445, 160}, {535, 200}};
    for (int i = 0; i < holes; ++i)
        test::DrawPinHead(img, P[i], 18.f, 10.f, fill, overflow, i != empty_hole);
    if (code) test::DrawCode(img, {400, 330}, blur);
    return img;
}

TEST(TopSide, GoodPartPasses) {
    DefectDetector d(test::SyntheticParams());
    auto r = d.Detect(CameraID::CCD1_TOP, TopPart());
    for (const auto& x : r) ADD_FAILURE() << ToString(x.type) << " " << x.description;
}

TEST(TopSide, HoleWithoutCoreIsPinMissing) {
    DefectDetector d(test::SyntheticParams());
    auto r = d.Detect(CameraID::CCD1_TOP, TopPart(4, 1.f, 0.f, true, false, 2));
    ASSERT_TRUE(Has(r, DefectType::PIN_MISSING));
    for (const auto& x : r)
        if (x.type == DefectType::PIN_MISSING) {
            EXPECT_NEAR(x.bbox.x + x.bbox.width / 2, 445, 10);
            EXPECT_NEAR(x.bbox.y + x.bbox.height / 2, 160, 10);
        }
}

TEST(TopSide, ThreeOfFourHolesIsMissing) {
    DefectDetector d(test::SyntheticParams());
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD1_TOP, TopPart(3)), DefectType::PIN_MISSING));
}

TEST(TopSide, ThreePinRecipePasses) {
    DetectionParams p = test::SyntheticParams();
    p.expected_pin_count = 3;
    DefectDetector d(p);
    EXPECT_FALSE(Has(d.Detect(CameraID::CCD1_TOP, TopPart(3)), DefectType::PIN_MISSING));
}

TEST(TopSide, HalfRingIsInsufficientEpoxy) {
    DefectDetector d(test::SyntheticParams());
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD1_TOP, TopPart(4, 0.4f)), DefectType::INSUFFICIENT_EPOXY));
}

TEST(TopSide, BigBlobIsOverflow) {
    DefectDetector d(test::SyntheticParams());
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD1_TOP, TopPart(4, 1.f, 22.f)), DefectType::EPOXY_OVERFLOW));
}

TEST(TopSide, NoCodeAndBlur) {
    DefectDetector d(test::SyntheticParams());
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD1_TOP, TopPart(4, 1.f, 0.f, false)), DefectType::NO_CODE));
    auto r = d.Detect(CameraID::CCD1_TOP, TopPart(4, 1.f, 0.f, true, true));
    EXPECT_TRUE(Has(r, DefectType::CODE_BLUR));
    EXPECT_FALSE(Has(r, DefectType::NO_CODE));
}

TEST(TopSide, GreyGranularPatchIsEpoxyExposal) {
    cv::Mat img = TopPart();
    test::DrawGreyNoise(img, cv::Rect(300, 420, 80, 60), 90, 200);
    DefectDetector d(test::SyntheticParams());
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD1_TOP, img), DefectType::EPOXY_EXPOSAL));
}

// The code ROI (and its exclusion mask) must follow the notch at any angle:
// at CCD1 the parts still have arbitrary orientation (compensation is step 9).
TEST(TopSide, GoodPartPassesAtOtherNotchAngles) {
    for (float notch : {30.f, 120.f, 200.f, 300.f}) {
        cv::Mat img = test::MakeDisc(800, 600, {400, 300}, 220.f, true, notch, 12.f);
        const cv::Point2f P[4] = {{265, 200}, {355, 160}, {445, 160}, {535, 200}};
        // pins rotated with the part so they stay away from the code ROI
        float a = (notch - 90.f) * static_cast<float>(CV_PI) / 180.f;
        for (const auto& p : P) {
            cv::Point2f d = p - cv::Point2f(400, 300);
            cv::Point2f q(400 + d.x * std::cos(a) - d.y * std::sin(a), 300 + d.x * std::sin(a) + d.y * std::cos(a));
            test::DrawPinHead(img, q, 18.f, 10.f);
        }
        test::DrawCodeAlongNotch(img, {400, 300}, 30.f, notch);
        DefectDetector d(test::SyntheticParams());
        auto r = d.Detect(CameraID::CCD1_TOP, img);
        for (const auto& x : r) ADD_FAILURE() << "notch " << notch << ": " << ToString(x.type) << " " << x.description;
    }
}

// The overflow measure must count epoxy only: with the hole included a nominal ring
// already reads ~1.8x nominal and a provisional 2.5 ratio trips far too early.
TEST(TopSide, NominalRingIsNotOverflowAtTightRatio) {
    DetectionParams p = test::SyntheticParams();
    p.epoxy_overflow_ratio = 1.5;
    DefectDetector d(p);
    EXPECT_FALSE(Has(d.Detect(CameraID::CCD1_TOP, TopPart()), DefectType::EPOXY_OVERFLOW));
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD1_TOP, TopPart(4, 1.f, 22.f)), DefectType::EPOXY_OVERFLOW));
}
