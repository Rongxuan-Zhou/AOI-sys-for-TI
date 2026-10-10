#include <gtest/gtest.h>

#include "CameraID.h"
#include "synthetic.h"

TEST(Smoke, CameraIdValues) {
    EXPECT_EQ(static_cast<int>(aoi::CameraID::CCD4_INNER), 4);
    EXPECT_EQ(static_cast<int>(aoi::CameraID::NG_CHECK), 5);
}

TEST(Smoke, SyntheticDiscIsBrightOnDark) {
    cv::Mat img = aoi::test::MakeDisc(400, 300, {200, 150}, 100);
    EXPECT_GT(img.at<cv::Vec3b>(150, 200)[0], 200);
    EXPECT_LT(img.at<cv::Vec3b>(5, 5)[0], 20);
}
