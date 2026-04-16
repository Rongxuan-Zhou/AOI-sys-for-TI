/**
 * pybind11 bindings for aoi_vision_core.
 *
 * Exposes GigEVisionCapture, ImagePreprocessor, and DefectDetector to
 * Python with zero-copy NumPy ↔ cv::Mat conversion.
 *
 * Usage from Python:
 *     import aoi_vision_core as vc
 *     cap = vc.GigEVisionCapture()
 *     cap.initialize([vc.CameraConfig(vc.CameraID.CCD1_TOP, ...)])
 *
 * Author: Rongxuan Zhou
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/numpy.h>

#include <opencv2/core.hpp>

#include "GigEVisionCapture.h"
#include "ImagePreprocessor.h"
#include "DefectDetector.h"

namespace py = pybind11;

// ---------------------------------------------------------------------------
// NumPy ↔ cv::Mat zero-copy conversion helpers
// ---------------------------------------------------------------------------
namespace {

/// Wrap a cv::Mat as a NumPy array (zero-copy; Mat must outlive the array).
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

    // Clone the Mat data so the NumPy array owns its memory.
    // For truly zero-copy, the caller must ensure the Mat stays alive.
    cv::Mat* owned = new cv::Mat(mat.clone());
    py::capsule release(owned, [](void* p) { delete static_cast<cv::Mat*>(p); });

    return py::array(py::buffer_info(
        owned->data, static_cast<ssize_t>(owned->elemSize1()),
        fmt, static_cast<ssize_t>(shape.size()), shape, strides
    ));
}

/// Create a cv::Mat that shares memory with a NumPy array.
cv::Mat numpy_to_mat(py::array arr) {
    py::buffer_info buf = arr.request();
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

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Module definition
// ---------------------------------------------------------------------------
PYBIND11_MODULE(_aoi_vision_core, m) {
    m.doc() = "C++ vision processing core for the TI CSE AOI system";

    using namespace aoi;

    // ---- Enums ------------------------------------------------------------
    py::enum_<CameraID>(m, "CameraID")
        .value("CCD1_TOP",    CameraID::CCD1_TOP)
        .value("CCD2_SIDE",   CameraID::CCD2_SIDE)
        .value("CCD3_BOTTOM", CameraID::CCD3_BOTTOM)
        .value("CCD4_INNER",  CameraID::CCD4_INNER);

    py::enum_<DefectType>(m, "DefectType")
        .value("F_CRACK",                        DefectType::F_CRACK)
        .value("F_BROKEN",                       DefectType::F_BROKEN)
        .value("F_EPOXY_EXPOSAL",                DefectType::F_EPOXY_EXPOSAL)
        .value("F_INSUFFICIENT_EPOXY",           DefectType::F_INSUFFICIENT_EPOXY)
        .value("F_EPOXY_OVERFLOW",               DefectType::F_EPOXY_OVERFLOW)
        .value("F_PIN_BENT",                     DefectType::F_PIN_BENT)
        .value("F_PIN_OXIDIZED",                 DefectType::F_PIN_OXIDIZED)
        .value("F_PIN_MIS_CUT",                  DefectType::F_PIN_MIS_CUT)
        .value("C_DYEING_CONTAMINATION",         DefectType::C_DYEING_CONTAMINATION)
        .value("C_NON_ELECTRICAL_CONTAMINATION", DefectType::C_NON_ELECTRICAL_CONTAMINATION)
        .value("C_STAINING",                     DefectType::C_STAINING)
        .value("C_CODE_BLUR",                    DefectType::C_CODE_BLUR)
        .value("A_NO_CODE",                      DefectType::A_NO_CODE)
        .value("A_MISALIGNMENT",                 DefectType::A_MISALIGNMENT)
        .value("A_PIN_BUR",                      DefectType::A_PIN_BUR)
        .value("A_GOLD_EXPOSAL",                 DefectType::A_GOLD_EXPOSAL)
        .value("A_LIGHT_LEAKAGE",                DefectType::A_LIGHT_LEAKAGE)
        .value("A_YELLOW_GLASS_CEMENT",          DefectType::A_YELLOW_GLASS_CEMENT)
        .value("A_EDGE_STAINING",                DefectType::A_EDGE_STAINING);

    py::enum_<Severity>(m, "Severity")
        .value("CRITICAL", Severity::CRITICAL)
        .value("MAJOR",    Severity::MAJOR)
        .value("MINOR",    Severity::MINOR);

    // ---- CameraConfig -----------------------------------------------------
    py::class_<CameraConfig>(m, "CameraConfig")
        .def(py::init<>())
        .def_readwrite("id",           &CameraConfig::id)
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
        .def_property_readonly("bbox", [](const DefectResult& r) {
            return py::make_tuple(r.bbox.x, r.bbox.y,
                                  r.bbox.width, r.bbox.height);
        });

    // ---- DetectionParams --------------------------------------------------
    py::class_<DetectionParams>(m, "DetectionParams")
        .def(py::init<>())
        .def_readwrite("crack_ncc_threshold",       &DetectionParams::crack_ncc_threshold)
        .def_readwrite("broken_ncc_threshold",      &DetectionParams::broken_ncc_threshold)
        .def_readwrite("contam_area_min_px",        &DetectionParams::contam_area_min_px)
        .def_readwrite("pin_bend_angle_deg_max",    &DetectionParams::pin_bend_angle_deg_max)
        .def_readwrite("code_laplacian_blur_limit", &DetectionParams::code_laplacian_blur_limit)
        .def_readwrite("leakage_intensity_thresh",  &DetectionParams::leakage_intensity_thresh);

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
        .def("initialize",       &GigEVisionCapture::Initialize)
        .def("start_acquisition", &GigEVisionCapture::StartAcquisition)
        .def("stop_acquisition",  &GigEVisionCapture::StopAcquisition)
        .def("stop_all",          &GigEVisionCapture::StopAll)
        .def("set_trigger_mode",  &GigEVisionCapture::SetTriggerMode)
        .def("set_exposure",      &GigEVisionCapture::SetExposure)
        .def("get_frame", [](GigEVisionCapture& self, CameraID cam, int timeout) {
            cv::Mat frame = self.GetFrame(cam, timeout);
            return mat_to_numpy(frame);
        }, py::arg("cam"), py::arg("timeout_ms") = 1000);

    // ---- ImagePreprocessor ------------------------------------------------
    py::class_<ImagePreprocessor>(m, "ImagePreprocessor")
        .def(py::init<>())
        .def("load_calibration", &ImagePreprocessor::LoadCalibration)
        .def("set_config",       &ImagePreprocessor::SetConfig)
        .def("process", [](ImagePreprocessor& self, CameraID cam,
                           py::array np_img) {
            cv::Mat mat = numpy_to_mat(np_img);
            cv::Mat result = self.Process(cam, mat);
            return mat_to_numpy(result);
        })
        .def_static("extract_roi", [](py::array np_img, py::tuple roi,
                                      float angle) {
            cv::Mat mat = numpy_to_mat(np_img);
            cv::Rect2f r(roi[0].cast<float>(), roi[1].cast<float>(),
                         roi[2].cast<float>(), roi[3].cast<float>());
            return mat_to_numpy(ImagePreprocessor::ExtractROI(mat, r, angle));
        }, py::arg("image"), py::arg("roi"), py::arg("angle_deg") = 0.0f)
        .def_static("cylindrical_unwrap", [](py::array np_img,
                                             py::tuple center, float radius,
                                             int out_w, int out_h) {
            cv::Mat mat = numpy_to_mat(np_img);
            cv::Point2f c(center[0].cast<float>(), center[1].cast<float>());
            return mat_to_numpy(
                ImagePreprocessor::CylindricalUnwrap(mat, c, radius, out_w, out_h));
        });

    // ---- DefectDetector ---------------------------------------------------
    py::class_<DefectDetector>(m, "DefectDetector")
        .def(py::init<>())
        .def("load_templates", &DefectDetector::LoadTemplates)
        .def("set_params",     &DefectDetector::SetParams)
        .def("get_params",     &DefectDetector::GetParams)
        .def("detect", [](DefectDetector& self, CameraID cam,
                          py::array np_img) {
            cv::Mat mat = numpy_to_mat(np_img);
            return self.Detect(cam, mat);
        });
}
