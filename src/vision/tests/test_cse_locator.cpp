#include <gtest/gtest.h>

#include <cmath>

#include "CseLocator.h"
#include "synthetic.h"

using namespace aoi;

TEST(CseLocator, FindsCenterRadiusAndNotch) {
    cv::Mat img = test::MakeDisc(640, 480, {320.5f, 240.5f}, 150.f, true, 270.f, 9.f);
    CseLocator loc;
    CseGeometry g = loc.Locate(img);
    ASSERT_TRUE(g.found);
    EXPECT_NEAR(g.center.x, 320.5f, 1.5f);
    EXPECT_NEAR(g.center.y, 240.5f, 1.5f);
    EXPECT_NEAR(g.radius_px, 150.f, 2.f);
    ASSERT_FALSE(std::isnan(g.notch_angle_deg));
    EXPECT_NEAR(g.notch_angle_deg, 270.f, 4.f);
    EXPECT_GT(g.notch_depth_px, 5.f);
}

TEST(CseLocator, NotchDoesNotBiasFit) {
    cv::Mat a = test::MakeDisc(640, 480, {320, 240}, 150.f, false);
    cv::Mat b = test::MakeDisc(640, 480, {320, 240}, 150.f, true, 0.f, 12.f);
    CseLocator loc;
    CseGeometry ga = loc.Locate(a);
    CseGeometry gb = loc.Locate(b);
    ASSERT_TRUE(ga.found);
    ASSERT_TRUE(gb.found);
    EXPECT_NEAR(ga.radius_px, gb.radius_px, 1.0f);
    EXPECT_NEAR(ga.center.x, gb.center.x, 1.0f);
    EXPECT_TRUE(std::isnan(ga.notch_angle_deg));
}

TEST(CseLocator, NoDiscReturnsNotFound) {
    CseLocator loc;
    EXPECT_FALSE(loc.Locate(cv::Mat()).found);
    cv::Mat dark(480, 640, CV_8UC3, cv::Scalar(5, 5, 5));
    EXPECT_FALSE(loc.Locate(dark).found);
    cv::Mat noisy(480, 640, CV_8UC3);
    cv::randu(noisy, cv::Scalar(0, 0, 0), cv::Scalar(25, 25, 25));
    EXPECT_FALSE(loc.Locate(noisy).found);
}

TEST(CseLocator, RimProfileFlatExceptNotch) {
    cv::Mat img = test::MakeDisc(640, 480, {320, 240}, 150.f, true, 90.f, 9.f);
    CseLocator loc;
    CseGeometry g = loc.Locate(img);
    ASSERT_TRUE(g.found);
    std::vector<float> prof = loc.RimProfile(g, 360);
    ASSERT_EQ(prof.size(), 360u);
    EXPECT_NEAR(prof[0], 150.f, 2.f);
    EXPECT_LT(prof[90], 145.f);
}

TEST(CseLocator, PolarUnwrapShape) {
    cv::Mat img = test::MakeDisc(640, 480, {320, 240}, 150.f);
    cv::Mat u = CseLocator::PolarUnwrap(img, {320, 240}, 100.f, 160.f, 720, 60);
    EXPECT_EQ(u.rows, 60);
    EXPECT_EQ(u.cols, 720);
    EXPECT_EQ(u.type(), CV_8UC3);
}

TEST(CseLocator, InteriorMaskExcludesRimAndNotch) {
    cv::Mat img = test::MakeDisc(640, 480, {320, 240}, 150.f, true, 90.f, 9.f);
    CseLocator loc;
    CseGeometry g = loc.Locate(img);
    ASSERT_TRUE(g.found);
    cv::Mat m = loc.InteriorMask(g);
    EXPECT_EQ(m.at<uchar>(240, 320), 255);        // centre
    EXPECT_EQ(m.at<uchar>(240, 320 + 145), 0);    // rim band
    EXPECT_EQ(m.at<uchar>(240 + 135, 320), 0);    // notch zone (angle 90 = +y)
    EXPECT_EQ(m.at<uchar>(240 - 110, 320), 255);  // interior
}
