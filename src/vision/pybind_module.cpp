/**
 * pybind11 bindings for aoi_vision_core.
 *
 * Exposes GigEVisionCapture, ImagePreprocessor, CseLocator, LightingCheck
 * and DefectDetector to Python with NumPy <-> cv::Mat conversion.
 *
 * Usage from Python:
 *     import aoi_vision_core as vc
 *     det = vc.DefectDetector()
 *     results = det.detect(vc.CameraID.CCD1_TOP, frame)   # frame: HxWx3 uint8
 *
 * Author: Rongxuan Zhou
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/numpy.h>

#include <memory>

#include <opencv2/core.hpp>

#include "GigEVisionCapture.h"
#include "ImagePreprocessor.h"
#include "CseLocator.h"
#include "LightingCheck.h"
#include "DefectDetector.h"

namespace py = pybind11;

// ---------------------------------------------------------------------------
// NumPy <-> cv::Mat conversion helpers
// ---------------------------------------------------------------------------
namespace {

/// Copy a cv::Mat into a NumPy array that owns its memory.
py::array mat_to_numpy(const cv::Mat& mat) {
    if (mat.empty())
        return py::array();

    int depth = mat.depth();
    std::string fmt;
    switch (depth) {
        case CV_8U:  fmt = py::format_descriptor<uint8_t>::format();  break;
        case CV_32F: fmt = py::format_descriptor<float>::format();    break;
        case CV_64F: fmt = py::format_descriptor<double>::format();   break;
        default:     throw std::runtime_error("unsupported Mat depth");
    }

    std::vector<ssize_t> shape, strides;
    if (mat.channels() == 1) {
        shape   = {mat.rows, mat.cols};
        strides = {static_cast<ssize_t>(mat.step[0]),
                   static_cast<ssize_t>(mat.step[1])};
    } else {
        shape   = {mat.rows, mat.cols, mat.channels()};
        strides = {static_cast<ssize_t>(mat.step[0]),
                   static_cast<ssize_t>(mat.step[1]),
                   static_cast<ssize_t>(mat.elemSize1())};
    }

    auto owned = std::make_unique<cv::Mat>(mat.clone());
    auto* raw = owned.get();
    py::capsule release(owned.release(), [](void* p) {
        delete static_cast<cv::Mat*>(p);
    });

    return py::array(py::buffer_info(
        raw->data, static_cast<ssize_t>(raw->elemSize1()),
        fmt, static_cast<ssize_t>(shape.size()), shape, strides
    ));
}

/// Create a cv::Mat that shares memory with a NumPy array.
cv::Mat numpy_to_mat(py::array arr) {
    py::buffer_info buf = arr.request();
    if (buf.ndim < 2) throw std::runtime_error("expected a 2-D or 3-D array");
    int rows = static_cast<int>(buf.shape[0]);
    int cols = static_cast<int>(buf.shape[1]);
    int channels = (buf.ndim == 3) ? static_cast<int>(buf.shape[2]) : 1;

    int cv_type;
    if (buf.format == py::format_descriptor<uint8_t>::format())
        cv_type = CV_MAKETYPE(CV_8U, channels);
    else if (buf.format == py::format_descriptor<float>::format())
        cv_type = CV_MAKETYPE(CV_32F, channels);
    else if (buf.format == py::format_descriptor<double>::format())
        cv_type = CV_MAKETYPE(CV_64F, channels);
    else
        throw std::runtime_error("unsupported numpy dtype for Mat conversion");

    return cv::Mat(rows, cols, cv_type, buf.ptr,
                   static_cast<size_t>(buf.strides[0]));
}

py::tuple rect_to_tuple(const cv::Rect& r) {
    return py::make_tuple(r.x, r.y, r.width, r.height);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Module definition
// ---------------------------------------------------------------------------
PYBIND11_MODULE(_aoi_vision_core, m) {
    m.doc() = "C++ vision processing core for the TI CSE AOI system (spec V1.4)";

    using namespace aoi;

    // ---- Enums ------------------------------------------------------------
    py::enum_<CameraID>(m, "CameraID")
        .value("CCD1_TOP",     CameraID::CCD1_TOP)
        .value("CCD2_SIDE",    CameraID::CCD2_SIDE)
        .value("CCD3_BOTTOM",  CameraID::CCD3_BOTTOM)
        .value("CCD4_INNER",   CameraID::CCD4_INNER)
        .value("NG_CHECK",     CameraID::NG_CHECK)
        .value("POKAYOKE",     CameraID::POKAYOKE)
        .value("TRAY_MONITOR", CameraID::TRAY_MONITOR);

    py::enum_<DefectGroup>(m, "DefectGroup")
        .value("FUNCTION",      DefectGroup::FUNCTION)
        .value("COSMETIC",      DefectGroup::COSMETIC)
        .value("ASSEMBLY",      DefectGroup::ASSEMBLY)
        .value("INNER_RESIDUE", DefectGroup::INNER_RESIDUE);

    py::enum_<DefectType>(m, "DefectType")
        .value("LIGHTING_CHECK",               DefectType::LIGHTING_CHECK)
        .value("CRACK",                        DefectType::CRACK)
        .value("BROKEN",                       DefectType::BROKEN)
        .value("EPOXY_EXPOSAL",                DefectType::EPOXY_EXPOSAL)
        .value("PIN_MISSING",                  DefectType::PIN_MISSING)
        .value("ELECTRICAL_CONTAMINATION",     DefectType::ELECTRICAL_CONTAMINATION)
        .value("GOLD_EXPOSAL",                 DefectType::GOLD_EXPOSAL)
        .value("INSUFFICIENT_EPOXY",           DefectType::INSUFFICIENT_EPOXY)
        .value("EPOXY_OVERFLOW",               DefectType::EPOXY_OVERFLOW)
        .value("DYEING_CONTAMINATION",         DefectType::DYEING_CONTAMINATION)
        .value("NON_ELECTRICAL_CONTAMINATION", DefectType::NON_ELECTRICAL_CONTAMINATION)
        .value("NO_CODE",                      DefectType::NO_CODE)
        .value("CODE_BLUR",                    DefectType::CODE_BLUR)
        .value("PIN_BENT",                     DefectType::PIN_BENT)
        .value("PIN_OXIDIZED",                 DefectType::PIN_OXIDIZED)
        .value("PIN_BUR",                      DefectType::PIN_BUR)
        .value("PIN_MIS_CUT",                  DefectType::PIN_MIS_CUT)
        .value("EPOXY_HIGHER_THAN_CERAMIC",    DefectType::EPOXY_HIGHER_THAN_CERAMIC)
        .value("MISALIGNMENT",                 DefectType::MISALIGNMENT)
        .value("STAINING_ON_EDGE",             DefectType::STAINING_ON_EDGE)
        .value("YELLOW_GLASS_CEMENT",          DefectType::YELLOW_GLASS_CEMENT);

    py::enum_<Severity>(m, "Severity")
        .value("CRITICAL", Severity::CRITICAL)
        .value("MAJOR",    Severity::MAJOR)
        .value("MINOR",    Severity::MINOR);

    py::enum_<ThicknessClass>(m, "ThicknessClass")
        .value("UNKNOWN", ThicknessClass::UNKNOWN)
        .value("AMBER",   ThicknessClass::AMBER)
        .value("GREEN",   ThicknessClass::GREEN)
        .value("BLUE",    ThicknessClass::BLUE);

    m.def("group_of",    &GroupOf);
    m.def("severity_of", &SeverityOf);
    m.def("to_string",   &ToString);

    // ---- CameraConfig -----------------------------------------------------
    py::class_<CameraConfig>(m, "CameraConfig")
        .def(py::init<>())
        .def_readwrite("id",           &CameraConfig::id)
        .def_readwrite("unit",         &CameraConfig::unit)
        .def_readwrite("serial",       &CameraConfig::serial)
        .def_readwrite("width",        &CameraConfig::width)
        .def_readwrite("height",       &CameraConfig::height)
        .def_readwrite("exposure_us",  &CameraConfig::exposure_us)
        .def_readwrite("gain_db",      &CameraConfig::gain_db)
        .def_readwrite("packet_size",  &CameraConfig::packet_size)
        .def_readwrite("subnet",       &CameraConfig::subnet);

    // ---- DefectResult -----------------------------------------------------
    py::class_<DefectResult>(m, "DefectResult")
        .def_readonly("type",        &DefectResult::type)
        .def_readonly("severity",    &DefectResult::severity)
        .def_readonly("confidence",  &DefectResult::confidence)
        .def_readonly("camera",      &DefectResult::camera)
        .def_readonly("description", &DefectResult::description)
        .def_property_readonly("bbox", [](const DefectResult& r) { return rect_to_tuple(r.bbox); });

    // ---- Parameter structs ------------------------------------------------
    py::class_<LocatorParams>(m, "LocatorParams")
        .def(py::init<>())
        .def_readwrite("min_disc_radius_px", &LocatorParams::min_disc_radius_px)
        .def_readwrite("min_otsu_thresh",    &LocatorParams::min_otsu_thresh)
        .def_readwrite("notch_width_deg",    &LocatorParams::notch_width_deg)
        .def_readwrite("notch_depth_px",     &LocatorParams::notch_depth_px)
        .def_readwrite("fit_inlier_ratio",   &LocatorParams::fit_inlier_ratio)
        .def_readwrite("interior_margin_px", &LocatorParams::interior_margin_px);

    py::class_<LightingParams>(m, "LightingParams")
        .def(py::init<>())
        .def_readwrite("inner_zone_ratio",   &LightingParams::inner_zone_ratio)
        .def_readwrite("outer_zone_ratio",   &LightingParams::outer_zone_ratio)
        .def_readwrite("edge_band_ratio",    &LightingParams::edge_band_ratio)
        .def_readwrite("unwrap_w",           &LightingParams::unwrap_w)
        .def_readwrite("unwrap_h",           &LightingParams::unwrap_h)
        .def_readwrite("leak_residual_min",  &LightingParams::leak_residual_min)
        .def_readwrite("leak_area_min_px",   &LightingParams::leak_area_min_px)
        .def_readwrite("stain_residual_min", &LightingParams::stain_residual_min)
        .def_readwrite("stain_area_min_px",  &LightingParams::stain_area_min_px)
        .def_readwrite("stain_area_max_px",  &LightingParams::stain_area_max_px)
        .def_readwrite("cement_hue_low",     &LightingParams::cement_hue_low)
        .def_readwrite("cement_hue_high",    &LightingParams::cement_hue_high)
        .def_readwrite("cement_sat_delta",   &LightingParams::cement_sat_delta)
        .def_readwrite("cement_val_delta",   &LightingParams::cement_val_delta)
        .def_readwrite("cement_area_min_px", &LightingParams::cement_area_min_px)
        .def_readwrite("blob_aspect_max",    &LightingParams::blob_aspect_max)
        .def_readwrite("min_lit_radius_px",  &LightingParams::min_lit_radius_px)
        .def_readwrite("min_otsu_thresh",    &LightingParams::min_otsu_thresh)
        .def_readwrite("nest_ring_enabled",  &LightingParams::nest_ring_enabled)
        .def_readwrite("nest_val_min",       &LightingParams::nest_val_min)
        .def_readwrite("mm_per_px",          &LightingParams::mm_per_px)
        .def_readwrite("misalign_max_offset_mm", &LightingParams::misalign_max_offset_mm);

    py::class_<DetectionParams>(m, "DetectionParams")
        .def(py::init<>())
        .def_readwrite("locator",                  &DetectionParams::locator)
        .def_readwrite("lighting",                 &DetectionParams::lighting)
        .def_readwrite("mm_per_px_ccd123",         &DetectionParams::mm_per_px_ccd123)
        .def_property("fixture_center_ccd1",
            [](const DetectionParams& p) { return py::make_tuple(p.fixture_center_ccd1.x, p.fixture_center_ccd1.y); },
            [](DetectionParams& p, py::tuple t) { p.fixture_center_ccd1 = {t[0].cast<float>(), t[1].cast<float>()}; })
        .def_property("fixture_center_ccd3",
            [](const DetectionParams& p) { return py::make_tuple(p.fixture_center_ccd3.x, p.fixture_center_ccd3.y); },
            [](DetectionParams& p, py::tuple t) { p.fixture_center_ccd3 = {t[0].cast<float>(), t[1].cast<float>()}; })
        .def_readwrite("misalign_max_offset_mm",   &DetectionParams::misalign_max_offset_mm)
        .def_readwrite("expected_pin_count",       &DetectionParams::expected_pin_count)
        .def_readwrite("crack_blackhat_kernel_px", &DetectionParams::crack_blackhat_kernel_px)
        .def_readwrite("crack_blackhat_thresh",    &DetectionParams::crack_blackhat_thresh)
        .def_readwrite("crack_aspect_min",         &DetectionParams::crack_aspect_min)
        .def_readwrite("crack_min_length_px",      &DetectionParams::crack_min_length_px)
        .def_readwrite("broken_depth_px",          &DetectionParams::broken_depth_px)
        .def_readwrite("pin_head_dark_thresh",     &DetectionParams::pin_head_dark_thresh)
        .def_readwrite("pin_head_radius_px",       &DetectionParams::pin_head_radius_px)
        .def_readwrite("epoxy_ring_px",            &DetectionParams::epoxy_ring_px)
        .def_readwrite("pin_core_val_min",         &DetectionParams::pin_core_val_min)
        .def_readwrite("pin_core_area_min_px",     &DetectionParams::pin_core_area_min_px)
        .def_readwrite("epoxy_sat_min",            &DetectionParams::epoxy_sat_min)
        .def_readwrite("epoxy_val_max",            &DetectionParams::epoxy_val_max)
        .def_readwrite("epoxy_ring_min_fill",      &DetectionParams::epoxy_ring_min_fill)
        .def_readwrite("epoxy_overflow_ratio",     &DetectionParams::epoxy_overflow_ratio)
        .def_readwrite("epoxy_hue_low",            &DetectionParams::epoxy_hue_low)
        .def_readwrite("epoxy_hue_high",           &DetectionParams::epoxy_hue_high)
        .def_readwrite("exposal_sat_max",          &DetectionParams::exposal_sat_max)
        .def_readwrite("exposal_texture_min",      &DetectionParams::exposal_texture_min)
        .def_readwrite("exposal_area_min_px",      &DetectionParams::exposal_area_min_px)
        .def_readwrite("contam_area_min_px",       &DetectionParams::contam_area_min_px)
        .def_readwrite("dyeing_sat_min",           &DetectionParams::dyeing_sat_min)
        .def_readwrite("electrical_dark_max",      &DetectionParams::electrical_dark_max)
        .def_readwrite("electrical_texture_min",   &DetectionParams::electrical_texture_min)
        .def_readwrite("code_roi_w",               &DetectionParams::code_roi_w)
        .def_readwrite("code_roi_h",               &DetectionParams::code_roi_h)
        .def_readwrite("code_offset_px",           &DetectionParams::code_offset_px)
        .def_readwrite("code_ink_contrast",        &DetectionParams::code_ink_contrast)
        .def_readwrite("code_min_ink_ratio",       &DetectionParams::code_min_ink_ratio)
        .def_readwrite("code_laplacian_blur_limit",&DetectionParams::code_laplacian_blur_limit)
        .def_readwrite("pins_up",                  &DetectionParams::pins_up)
        .def_readwrite("side_fg_thresh",           &DetectionParams::side_fg_thresh)
        .def_readwrite("pin_w_max",                &DetectionParams::pin_w_max)
        .def_readwrite("pin_bend_angle_deg_max",   &DetectionParams::pin_bend_angle_deg_max)
        .def_readwrite("pin_length_px",            &DetectionParams::pin_length_px)
        .def_readwrite("pin_length_tol_px",        &DetectionParams::pin_length_tol_px)
        .def_readwrite("pin_bur_width_px",         &DetectionParams::pin_bur_width_px)
        .def_readwrite("pin_oxide_val_max",        &DetectionParams::pin_oxide_val_max)
        .def_readwrite("gold_area_min_px",         &DetectionParams::gold_area_min_px)
        .def_readwrite("epoxy_bump_px",            &DetectionParams::epoxy_bump_px)
        .def_readwrite("epoxy_bump_width_px",      &DetectionParams::epoxy_bump_width_px);

    // ---- PreprocessConfig -------------------------------------------------
    py::class_<PreprocessConfig>(m, "PreprocessConfig")
        .def(py::init<>())
        .def_readwrite("clahe_clip_limit",  &PreprocessConfig::clahe_clip_limit)
        .def_readwrite("clahe_grid_size",   &PreprocessConfig::clahe_grid_size)
        .def_readwrite("apply_flatfield",   &PreprocessConfig::apply_flatfield)
        .def_readwrite("apply_chromatic_correction",
                       &PreprocessConfig::apply_chromatic_correction);

    // ---- GigEVisionCapture ------------------------------------------------
    py::class_<GigEVisionCapture>(m, "GigEVisionCapture")
        .def(py::init<>())
        .def("initialize",        &GigEVisionCapture::Initialize)
        .def("start_acquisition", &GigEVisionCapture::StartAcquisition, py::arg("cam"), py::arg("unit") = 1)
        .def("stop_acquisition",  &GigEVisionCapture::StopAcquisition, py::arg("cam"), py::arg("unit") = 1)
        .def("stop_all",          &GigEVisionCapture::StopAll)
        .def("set_trigger_mode",  &GigEVisionCapture::SetTriggerMode, py::arg("cam"), py::arg("unit"), py::arg("hardware"))
        .def("set_exposure",      &GigEVisionCapture::SetExposure, py::arg("cam"), py::arg("unit"), py::arg("exposure_us"))
        .def("slot_count",        &GigEVisionCapture::SlotCount)
        .def("get_frame", [](GigEVisionCapture& self, CameraID cam, int unit, int timeout) {
            cv::Mat frame = self.GetFrame(cam, unit, timeout);
            return mat_to_numpy(frame);
        }, py::arg("cam"), py::arg("unit") = 1, py::arg("timeout_ms") = 1000);

    // ---- ImagePreprocessor ------------------------------------------------
    py::class_<ImagePreprocessor>(m, "ImagePreprocessor")
        .def(py::init<>())
        .def("load_calibration", &ImagePreprocessor::LoadCalibration)
        .def("set_config",       &ImagePreprocessor::SetConfig)
        .def("process", [](ImagePreprocessor& self, CameraID cam, py::array np_img) {
            cv::Mat mat = numpy_to_mat(np_img);
            cv::Mat result = self.Process(cam, mat);
            return mat_to_numpy(result);
        })
        .def_static("extract_roi", [](py::array np_img, py::tuple roi, float angle) {
            cv::Mat mat = numpy_to_mat(np_img);
            cv::Rect2f r(roi[0].cast<float>(), roi[1].cast<float>(),
                         roi[2].cast<float>(), roi[3].cast<float>());
            return mat_to_numpy(ImagePreprocessor::ExtractROI(mat, r, angle));
        }, py::arg("image"), py::arg("roi"), py::arg("angle_deg") = 0.0f)
        .def_static("cylindrical_unwrap", [](py::array np_img, py::tuple center, float radius,
                                             int out_w, int out_h) {
            cv::Mat mat = numpy_to_mat(np_img);
            cv::Point2f c(center[0].cast<float>(), center[1].cast<float>());
            return mat_to_numpy(
                ImagePreprocessor::CylindricalUnwrap(mat, c, radius, out_w, out_h));
        });

    // ---- CseLocator -------------------------------------------------------
    py::class_<CseGeometry>(m, "CseGeometry")
        .def_readonly("found",           &CseGeometry::found)
        .def_readonly("radius_px",       &CseGeometry::radius_px)
        .def_readonly("notch_angle_deg", &CseGeometry::notch_angle_deg)
        .def_readonly("notch_depth_px",  &CseGeometry::notch_depth_px)
        .def_readonly("notch_width_deg", &CseGeometry::notch_width_deg)
        .def_property_readonly("center", [](const CseGeometry& g) { return py::make_tuple(g.center.x, g.center.y); });

    py::class_<CseLocator>(m, "CseLocator")
        .def(py::init<>())
        .def(py::init<LocatorParams>())
        .def("locate", [](const CseLocator& self, py::array np_img) {
            return self.Locate(numpy_to_mat(np_img));
        });

    // ---- LightingCheck ----------------------------------------------------
    py::class_<LightingResult>(m, "LightingResult")
        .def_readonly("located",        &LightingResult::located)
        .def_readonly("passed",         &LightingResult::passed)
        .def_readonly("thickness",      &LightingResult::thickness)
        .def_readonly("radius_px",      &LightingResult::radius_px)
        .def_readonly("nest_found",     &LightingResult::nest_found)
        .def_readonly("offset_mm",      &LightingResult::offset_mm)
        .def_readonly("leak_area_px",   &LightingResult::leak_area_px)
        .def_readonly("stain_area_px",  &LightingResult::stain_area_px)
        .def_readonly("cement_area_px", &LightingResult::cement_area_px)
        .def_readonly("defects",        &LightingResult::defects)
        .def_property_readonly("center", [](const LightingResult& r) { return py::make_tuple(r.center.x, r.center.y); });

    py::class_<LightingCheck>(m, "LightingCheck")
        .def(py::init<>())
        .def(py::init<LightingParams>())
        .def("set_params", &LightingCheck::SetParams)
        .def("analyse", [](const LightingCheck& self, py::array np_img) {
            return self.Analyse(numpy_to_mat(np_img));
        });

    // ---- DefectDetector ---------------------------------------------------
    py::class_<DefectDetector>(m, "DefectDetector")
        .def(py::init<>())
        .def(py::init<const DetectionParams&>())
        .def("set_params",   &DefectDetector::SetParams)
        .def("get_params",   &DefectDetector::GetParams)
        .def_static("primary_type_of", &DefectDetector::PrimaryTypeOf)
        .def("detect", [](DefectDetector& self, CameraID cam, py::array np_img) {
            cv::Mat mat = numpy_to_mat(np_img);
            return self.Detect(cam, mat);
        });
}
