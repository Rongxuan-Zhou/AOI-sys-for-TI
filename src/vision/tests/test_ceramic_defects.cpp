#include <gtest/gtest.h>

#include "DefectDetector.h"
#include "synthetic.h"

using namespace aoi;

static bool Has(const std::vector<DefectResult>& v, DefectType t) {
    for (const auto& r : v) if (r.type == t) return true;
    return false;
}

TEST(Ceramic, GoodDiscWithNotchPasses) {
    cv::Mat img = test::MakeDisc(640, 480, {320, 240}, 150.f, true, 270.f, 9.f);
    DefectDetector d(test::SyntheticParams());
    auto r = d.Detect(CameraID::CCD3_BOTTOM, img);
    EXPECT_FALSE(Has(r, DefectType::BROKEN));
    EXPECT_FALSE(Has(r, DefectType::CRACK));
    EXPECT_FALSE(Has(r, DefectType::MISALIGNMENT));
}

TEST(Ceramic, SecondChipIsBroken) {
    cv::Mat img = test::MakeDisc(640, 480, {320, 240}, 150.f, true, 270.f, 9.f);
    cv::circle(img, cv::Point(320 + 150, 240), 14, cv::Scalar(8, 8, 8), cv::FILLED, cv::LINE_AA);
    DefectDetector d(test::SyntheticParams());
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD3_BOTTOM, img), DefectType::BROKEN));
}

TEST(Ceramic, OversizedNotchIsBroken) {
    cv::Mat img = test::MakeDisc(640, 480, {320, 240}, 150.f, true, 270.f, 28.f);
    DefectDetector d(test::SyntheticParams());
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD3_BOTTOM, img), DefectType::BROKEN));
}

// A notch-sized chip on a disc without a notch is indistinguishable from the key
// notch; a chip deeper than 1.5 x notch depth is BROKEN regardless.
TEST(Ceramic, DeepChipWithoutNotchIsBroken) {
    cv::Mat img = test::MakeDisc(640, 480, {320, 240}, 150.f, false);
    cv::circle(img, cv::Point(320, 240 - 150), 17, cv::Scalar(8, 8, 8), cv::FILLED, cv::LINE_AA);
    DefectDetector d(test::SyntheticParams());
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD3_BOTTOM, img), DefectType::BROKEN));
}

TEST(Ceramic, ThinDarkLineIsCrack) {
    cv::Mat img = test::MakeDisc(640, 480, {320, 240}, 150.f);
    cv::line(img, {260, 200}, {380, 300}, cv::Scalar(120, 120, 120), 2, cv::LINE_AA);
    DefectDetector d(test::SyntheticParams());
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD3_BOTTOM, img), DefectType::CRACK));
}

TEST(Ceramic, RoundDotIsNotCrack) {
    cv::Mat img = test::MakeDisc(640, 480, {320, 240}, 150.f);
    cv::circle(img, {320, 240}, 6, cv::Scalar(120, 120, 120), cv::FILLED, cv::LINE_AA);
    DefectDetector d(test::SyntheticParams());
    EXPECT_FALSE(Has(d.Detect(CameraID::CCD3_BOTTOM, img), DefectType::CRACK));
}

TEST(Ceramic, OffsetDiscIsMisaligned) {
    DetectionParams p = test::SyntheticParams();
    p.fixture_center_ccd3 = {320, 240};
    DefectDetector d(p);
    cv::Mat ok = test::MakeDisc(640, 480, {322, 241}, 150.f);
    cv::Mat bad = test::MakeDisc(640, 480, {340, 240}, 150.f);
    EXPECT_FALSE(Has(d.Detect(CameraID::CCD3_BOTTOM, ok), DefectType::MISALIGNMENT));
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD3_BOTTOM, bad), DefectType::MISALIGNMENT));
}
