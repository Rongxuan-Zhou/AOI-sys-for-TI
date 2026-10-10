#include <gtest/gtest.h>

#include "DefectDetector.h"
#include "synthetic.h"

using namespace aoi;

static bool Has(const std::vector<DefectResult>& v, DefectType t) {
    for (const auto& r : v) if (r.type == t) return true;
    return false;
}

// Bottom-side synthetic part: disc R=220, notch at 90 deg, amber epoxy "banana".
static cv::Mat BottomPart() {
    cv::Mat img = test::MakeDisc(800, 600, {400, 300}, 220.f, true, 90.f, 12.f);
    cv::ellipse(img, cv::Point(400, 300), cv::Size(90, 40), 30, 0, 360, cv::Scalar(40, 90, 130),
                cv::FILLED, cv::LINE_AA);
    return img;
}

TEST(Contam, CleanBottomPasses) {
    DefectDetector d(test::SyntheticParams());
    auto r = d.Detect(CameraID::CCD3_BOTTOM, BottomPart());
    for (const auto& x : r) ADD_FAILURE() << ToString(x.type) << " " << x.description;
}

TEST(Contam, DarkTexturedStreakIsElectrical) {
    cv::Mat img = BottomPart();
    test::DrawGreyNoise(img, cv::Rect(470, 180, 110, 30), 20, 120);
    DefectDetector d(test::SyntheticParams());
    auto r = d.Detect(CameraID::CCD3_BOTTOM, img);
    EXPECT_TRUE(Has(r, DefectType::ELECTRICAL_CONTAMINATION));
    EXPECT_FALSE(Has(r, DefectType::DYEING_CONTAMINATION));
}

TEST(Contam, LightSmudgeIsNonElectrical) {
    cv::Mat img = BottomPart();
    cv::ellipse(img, cv::Point(480, 200), cv::Size(50, 14), 20, 0, 360, cv::Scalar(170, 175, 180),
                cv::FILLED, cv::LINE_AA);
    DefectDetector d(test::SyntheticParams());
    auto r = d.Detect(CameraID::CCD3_BOTTOM, img);
    EXPECT_TRUE(Has(r, DefectType::NON_ELECTRICAL_CONTAMINATION));
    EXPECT_FALSE(Has(r, DefectType::ELECTRICAL_CONTAMINATION));
}

TEST(Contam, MagentaStainIsDyeing) {
    cv::Mat img = BottomPart();
    cv::ellipse(img, cv::Point(300, 200), cv::Size(45, 30), 0, 0, 360, cv::Scalar(200, 120, 220),
                cv::FILLED, cv::LINE_AA);
    DefectDetector d(test::SyntheticParams());
    EXPECT_TRUE(Has(d.Detect(CameraID::CCD3_BOTTOM, img), DefectType::DYEING_CONTAMINATION));
}

TEST(Contam, TopSideContaminationAlsoClassified) {
    cv::Mat img = test::MakeDisc(800, 600, {400, 300}, 220.f, true, 90.f, 12.f);
    cv::ellipse(img, cv::Point(300, 420), cv::Size(45, 30), 0, 0, 360, cv::Scalar(200, 120, 220),
                cv::FILLED, cv::LINE_AA);
    DetectionParams p = test::SyntheticParams();
    p.expected_pin_count = 0;   // bare disc, pins not under test here
    DefectDetector d(p);
    auto r = d.Detect(CameraID::CCD1_TOP, img);
    EXPECT_TRUE(Has(r, DefectType::DYEING_CONTAMINATION));
}
