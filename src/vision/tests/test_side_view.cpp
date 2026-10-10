#include <gtest/gtest.h>

#include "DefectDetector.h"
#include "synthetic.h"

using namespace aoi;

static bool Has(const std::vector<DefectResult>& v, DefectType t) {
    for (const auto& r : v) if (r.type == t) return true;
    return false;
}

static const cv::Rect BODY(200, 150, 400, 90);

static DefectDetector BenchDetector() {
    DetectionParams p = test::SyntheticParams();
    p.pins_up = false;   // synthetic bench view draws the pins below the body
    return DefectDetector(p);
}

static cv::Mat Straight(cv::Scalar pin_colour = cv::Scalar(40, 170, 220)) {
    return test::MakeSideView(800, 500, BODY, {300, 400, 500}, {0, 0, 0}, {120, 120, 120}, 6, pin_colour);
}

TEST(Side, StraightPinsPass) {
    DefectDetector d = BenchDetector();
    auto r = d.Detect(CameraID::CCD2_SIDE, Straight());
    for (const auto& x : r) ADD_FAILURE() << ToString(x.type) << " " << x.description;
}

TEST(Side, PinsUpFrameIsFlipped) {
    cv::Mat img = Straight();
    cv::flip(img, img, 0);
    DefectDetector d(test::SyntheticParams());   // pins_up = true (machine orientation)
    auto r = d.Detect(CameraID::CCD2_SIDE, img);
    for (const auto& x : r) ADD_FAILURE() << ToString(x.type) << " " << x.description;

    cv::Mat bent = test::MakeSideView(800, 500, BODY, {300, 400, 500}, {0, 6, 0}, {120, 120, 120});
    cv::flip(bent, bent, 0);
    auto rb = d.Detect(CameraID::CCD2_SIDE, bent);
    ASSERT_TRUE(Has(rb, DefectType::PIN_BENT));
    for (const auto& x : rb)
        if (x.type == DefectType::PIN_BENT) EXPECT_LT(x.bbox.y, 500 - 240);   // bbox mapped back to the pins-up frame
}

TEST(Side, TiltedPinIsBent) {
    cv::Mat img = test::MakeSideView(800, 500, BODY, {300, 400, 500}, {0, 6, 0}, {120, 120, 120});
    EXPECT_TRUE(Has(BenchDetector().Detect(CameraID::CCD2_SIDE, img), DefectType::PIN_BENT));
}

TEST(Side, ShortPinIsMisCut) {
    cv::Mat img = test::MakeSideView(800, 500, BODY, {300, 400, 500}, {0, 0, 0}, {120, 80, 120});
    EXPECT_TRUE(Has(BenchDetector().Detect(CameraID::CCD2_SIDE, img), DefectType::PIN_MIS_CUT));
}

TEST(Side, WideTipIsBur) {
    cv::Mat img = Straight();
    cv::rectangle(img, cv::Rect(500 - 9, 240 + 110, 18, 10), cv::Scalar(40, 170, 220), cv::FILLED);
    auto r = BenchDetector().Detect(CameraID::CCD2_SIDE, img);
    EXPECT_TRUE(Has(r, DefectType::PIN_BUR));
    EXPECT_FALSE(Has(r, DefectType::PIN_MIS_CUT));
}

TEST(Side, DarkPinIsOxidized) {
    cv::Mat img = Straight();
    cv::line(img, {400, 240}, {400, 360}, cv::Scalar(20, 50, 100), 6);
    auto r = BenchDetector().Detect(CameraID::CCD2_SIDE, img);
    EXPECT_TRUE(Has(r, DefectType::PIN_OXIDIZED));
    EXPECT_FALSE(Has(r, DefectType::PIN_MIS_CUT));
}

TEST(Side, BrownPatchOnWallIsGoldExposal) {
    cv::Mat img = Straight();
    cv::ellipse(img, cv::Point(420, 195), cv::Size(16, 10), 0, 0, 360, cv::Scalar(40, 90, 140), cv::FILLED, cv::LINE_AA);
    EXPECT_TRUE(Has(BenchDetector().Detect(CameraID::CCD2_SIDE, img), DefectType::GOLD_EXPOSAL));
}

TEST(Side, BumpOnTopEdgeIsEpoxyHigher) {
    cv::Mat img = Straight();
    cv::rectangle(img, cv::Rect(380, 150 - 9, 30, 10), cv::Scalar(230, 232, 235), cv::FILLED);
    EXPECT_TRUE(Has(BenchDetector().Detect(CameraID::CCD2_SIDE, img), DefectType::EPOXY_HIGHER_THAN_CERAMIC));
}

TEST(Side, EmptyFrameNotLocated) {
    cv::Mat dark(500, 800, CV_8UC3, cv::Scalar(5, 5, 5));
    auto r = BenchDetector().Detect(CameraID::CCD2_SIDE, dark);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].description, "disc not located");
}

TEST(Side, SeverelyBentPinIsStillBent) {
    for (float bend : {30.f, 45.f, 60.f}) {
        cv::Mat img = test::MakeSideView(800, 500, BODY, {300, 400, 500}, {0, bend, 0}, {120, 120, 120});
        EXPECT_TRUE(Has(BenchDetector().Detect(CameraID::CCD2_SIDE, img), DefectType::PIN_BENT)) << bend;
    }
}
