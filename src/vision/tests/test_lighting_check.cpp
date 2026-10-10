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
