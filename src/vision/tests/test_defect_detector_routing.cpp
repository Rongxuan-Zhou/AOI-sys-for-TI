#include <gtest/gtest.h>

#include "DefectDetector.h"
#include "synthetic.h"

using namespace aoi;

TEST(Taxonomy, Has21TypesInSpecOrder) {
    EXPECT_EQ(static_cast<int>(DefectType::COUNT), 21);
    EXPECT_EQ(static_cast<int>(DefectType::LIGHTING_CHECK), 0);
    EXPECT_EQ(static_cast<int>(DefectType::EPOXY_OVERFLOW), 8);
    EXPECT_EQ(static_cast<int>(DefectType::CODE_BLUR), 12);
    EXPECT_EQ(static_cast<int>(DefectType::MISALIGNMENT), 18);
    EXPECT_STREQ(ToString(DefectType::LIGHTING_CHECK), "lighting_check");
    EXPECT_STREQ(ToString(DefectType::EPOXY_HIGHER_THAN_CERAMIC), "epoxy_higher_than_ceramic");
    EXPECT_STREQ(ToString(DefectType::YELLOW_GLASS_CEMENT), "yellow_glass_cement");
    EXPECT_EQ(GroupOf(DefectType::EPOXY_OVERFLOW), DefectGroup::FUNCTION);
    EXPECT_EQ(GroupOf(DefectType::CODE_BLUR), DefectGroup::COSMETIC);
    EXPECT_EQ(GroupOf(DefectType::MISALIGNMENT), DefectGroup::ASSEMBLY);
    EXPECT_EQ(GroupOf(DefectType::YELLOW_GLASS_CEMENT), DefectGroup::INNER_RESIDUE);
}

TEST(Taxonomy, CriticalSet) {
    for (int i = 0; i < static_cast<int>(DefectType::COUNT); ++i) {
        auto t = static_cast<DefectType>(i);
        bool crit = t == DefectType::LIGHTING_CHECK || t == DefectType::CRACK ||
                    t == DefectType::BROKEN || t == DefectType::MISALIGNMENT;
        EXPECT_EQ(SeverityOf(t) == Severity::CRITICAL, crit) << ToString(t);
    }
    EXPECT_EQ(SeverityOf(DefectType::PIN_BENT), Severity::MAJOR);
    EXPECT_EQ(SeverityOf(DefectType::NO_CODE), Severity::MINOR);
    EXPECT_EQ(SeverityOf(DefectType::STAINING_ON_EDGE), Severity::MINOR);
}

TEST(Routing, EmptyOrDarkFrameIsNgNotLocated) {
    DefectDetector d(test::SyntheticParams());
    cv::Mat noisy(480, 640, CV_8UC3);
    cv::randu(noisy, cv::Scalar(0, 0, 0), cv::Scalar(25, 25, 25));
    for (auto cam : {CameraID::CCD1_TOP, CameraID::CCD2_SIDE, CameraID::CCD3_BOTTOM,
                     CameraID::CCD4_INNER}) {
        for (const cv::Mat& frame : {cv::Mat(), cv::Mat(480, 640, CV_8UC3, cv::Scalar(3, 3, 3)), noisy}) {
            auto r = d.Detect(cam, frame);
            ASSERT_EQ(r.size(), 1u) << static_cast<int>(cam);
            EXPECT_EQ(r[0].description, "disc not located");
            EXPECT_EQ(r[0].type, DefectDetector::PrimaryTypeOf(cam));
            EXPECT_EQ(r[0].camera, cam);
            EXPECT_FLOAT_EQ(r[0].confidence, 1.f);
        }
    }
}

TEST(Routing, GrayInputAccepted) {
    DefectDetector d(test::SyntheticParams());
    cv::Mat g(480, 640, CV_8UC1, cv::Scalar(0));
    EXPECT_NO_THROW(d.Detect(CameraID::CCD3_BOTTOM, g));
}

TEST(Routing, SetParamsPropagates) {
    DefectDetector d;
    DetectionParams p = d.GetParams();
    p.locator.notch_width_deg = 9.f;
    p.lighting.leak_residual_min = 77;
    d.SetParams(p);
    EXPECT_FLOAT_EQ(d.GetParams().locator.notch_width_deg, 9.f);
    EXPECT_EQ(d.GetParams().lighting.leak_residual_min, 77);
}
