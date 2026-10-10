#include <gtest/gtest.h>

#include "DefectDetector.h"
#include "LightingCheck.h"
#include "synthetic.h"

using namespace aoi;

static const cv::Scalar AMBER(20, 150, 230);
static const cv::Scalar GREEN(140, 190, 150);
static const cv::Scalar BLUE(220, 190, 150);

static bool Has(const std::vector<DefectResult>& v, DefectType t) {
    for (const auto& r : v) if (r.type == t) return true;
    return false;
}

TEST(Lighting, CleanAnnulusPassesInAllThreeColours) {
    for (const auto& tint : {AMBER, GREEN, BLUE}) {
        cv::Mat img = test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, tint);
        LightingResult r = LightingCheck().Analyse(img);
        ASSERT_TRUE(r.located);
        EXPECT_TRUE(r.passed) << tint;
        EXPECT_NEAR(r.radius_px, 200.f, 6.f);
    }
}

TEST(Lighting, ThicknessClassFollowsHue) {
    LightingCheck lc;
    EXPECT_EQ(lc.Analyse(test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, AMBER)).thickness, ThicknessClass::AMBER);
    EXPECT_EQ(lc.Analyse(test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, GREEN)).thickness, ThicknessClass::GREEN);
    EXPECT_EQ(lc.Analyse(test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, BLUE)).thickness, ThicknessClass::BLUE);
}

TEST(Lighting, StructuredAnnulusPasses) {
    // Dark banana and a thin bright radial line are structure (slide 30), not defects.
    cv::Mat img = test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, AMBER);
    cv::ellipse(img, cv::Point(400, 300 - 120), cv::Size(70, 24), 0, 0, 360, cv::Scalar(15, 15, 15), cv::FILLED, cv::LINE_AA);
    float a = 30.f * static_cast<float>(CV_PI) / 180.f;
    cv::line(img, cv::Point(cvRound(400 + 90 * std::cos(a)), cvRound(300 + 90 * std::sin(a))),
             cv::Point(cvRound(400 + 190 * std::cos(a)), cvRound(300 + 190 * std::sin(a))),
             cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
    LightingResult r = LightingCheck().Analyse(img);
    ASSERT_TRUE(r.located);
    for (const auto& d : r.defects) ADD_FAILURE() << ToString(d.type) << " " << d.description;
}

TEST(Lighting, BrightSpotIsLeakageInAllColours) {
    for (const auto& tint : {AMBER, GREEN, BLUE}) {
        cv::Mat img = test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, tint);
        cv::circle(img, {400 + 140, 300}, 9, cv::Scalar(255, 255, 255), cv::FILLED, cv::LINE_AA);
        LightingResult r = LightingCheck().Analyse(img);
        ASSERT_TRUE(r.located);
        EXPECT_FALSE(r.passed);
        ASSERT_TRUE(Has(r.defects, DefectType::LIGHTING_CHECK)) << tint;
        for (const auto& d : r.defects)
            if (d.type == DefectType::LIGHTING_CHECK) {
                EXPECT_EQ(d.severity, Severity::CRITICAL);
                EXPECT_NEAR(d.bbox.x + d.bbox.width / 2, 540, 12);
                EXPECT_NEAR(d.bbox.y + d.bbox.height / 2, 300, 12);
            }
    }
}

TEST(Lighting, DarkCompactPatchOnRimIsEdgeStaining) {
    cv::Mat img = test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, AMBER);
    cv::circle(img, {400, 300 - 185}, 10, cv::Scalar(10, 10, 10), cv::FILLED, cv::LINE_AA);
    LightingResult r = LightingCheck().Analyse(img);
    EXPECT_TRUE(Has(r.defects, DefectType::STAINING_ON_EDGE));
    EXPECT_FALSE(Has(r.defects, DefectType::LIGHTING_CHECK));
}

TEST(Lighting, SaturatedYellowBlobIsCementOnBlueAndAmber) {
    for (const auto& tint : {BLUE, AMBER}) {
        cv::Mat img = test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, tint);
        cv::circle(img, {400 - 130, 300}, 11, cv::Scalar(0, 200, 230), cv::FILLED, cv::LINE_AA);
        LightingResult r = LightingCheck().Analyse(img);
        EXPECT_TRUE(Has(r.defects, DefectType::YELLOW_GLASS_CEMENT)) << tint;
    }
}

TEST(Lighting, NestRingOffsetIsMisalignment) {
    DefectDetector d(test::SyntheticParams());
    cv::Mat centred = test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, AMBER, true, {0, 0});
    LightingResult r = LightingCheck().Analyse(centred);
    ASSERT_TRUE(r.nest_found);
    EXPECT_LT(r.offset_mm, 0.05f);
    EXPECT_FALSE(Has(d.Detect(CameraID::CCD4_INNER, centred), DefectType::MISALIGNMENT));
    cv::Mat shifted = test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, AMBER, true, {30, 0});
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD4_INNER, shifted), DefectType::MISALIGNMENT));
}

TEST(Lighting, DarkFrameNotLocated) {
    cv::Mat dark(600, 800, CV_8UC3, cv::Scalar(4, 4, 4));
    EXPECT_FALSE(LightingCheck().Analyse(dark).located);
    cv::Mat noisy(600, 800, CV_8UC3);
    cv::randu(noisy, cv::Scalar(0, 0, 0), cv::Scalar(25, 25, 25));
    EXPECT_FALSE(LightingCheck().Analyse(noisy).located);
}

// Slide 30: the banana and the pin-hole shadows cover roughly half of the annulus at
// mid radii. The lit remainder must not turn into "leakage" when the dark share grows.
TEST(Lighting, HalfDarkAnnulusPasses) {
    for (int dark_pct : {45, 55, 65}) {
        cv::Mat img = test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, AMBER);
        // dark sectors over [0.5R, 0.9R] covering dark_pct of the circle, in 6 wedges
        float total_deg = 360.f * dark_pct / 100.f;
        for (int k = 0; k < 6; ++k) {
            float a0 = k * 60.f, a1 = a0 + total_deg / 6.f;
            cv::ellipse(img, cv::Point(400, 300), cv::Size(180, 180), 0, a0, a1, cv::Scalar(15, 15, 15), cv::FILLED, cv::LINE_AA);
        }
        cv::circle(img, cv::Point(400, 300), 100, cv::Scalar(0, 0, 0), cv::FILLED);   // restore inner zone
        cv::Mat ring = test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, AMBER);
        cv::Mat inner_mask = cv::Mat::zeros(img.size(), CV_8U);
        cv::circle(inner_mask, cv::Point(400, 300), 100, cv::Scalar(255), cv::FILLED);
        ring.copyTo(img, inner_mask);
        LightingResult r = LightingCheck().Analyse(img);
        ASSERT_TRUE(r.located) << dark_pct;
        EXPECT_FALSE(Has(r.defects, DefectType::LIGHTING_CHECK)) << dark_pct << "% dark: " << r.leak_area_px;
    }
}

// The polar unwrap has a seam at angle 0; a leak there must be found as reliably
// as anywhere else (same blob, same area class).
TEST(Lighting, LeakAtPolarSeamIsFound) {
    LightingCheck lc;
    int area_at_90 = 0, area_at_0 = 0;
    for (float ang : {90.f, 0.f}) {
        cv::Mat img = test::MakeLitAnnulus(800, 600, {400, 300}, 200.f, AMBER);
        float a = ang * static_cast<float>(CV_PI) / 180.f;
        cv::circle(img, cv::Point(cvRound(400 + 140 * std::cos(a)), cvRound(300 + 140 * std::sin(a))), 5,
                   cv::Scalar(255, 255, 255), cv::FILLED, cv::LINE_AA);
        LightingResult r = lc.Analyse(img);
        int n = 0;
        for (const auto& d : r.defects) if (d.type == DefectType::LIGHTING_CHECK) ++n;
        EXPECT_EQ(n, 1) << "angle " << ang;
        (ang == 90.f ? area_at_90 : area_at_0) = r.leak_area_px;
    }
    EXPECT_GT(area_at_0, 0);
    EXPECT_NEAR(area_at_0, area_at_90, area_at_90 * 0.35);
}
