#pragma once
/**
 * GigE Vision capture interface for the TI CSE AOI system.
 *
 * Wraps the Hikrobot MVS SDK to manage four industrial cameras
 * (3x MV-GE501GC 5MP + 1x MV-GE2000C 20MP) over GigE Vision 2.0.
 * Supports hardware triggering synchronised with Gardasoft RT820F-20
 * strobe controller via GPIO Line0.
 *
 * Author: Rongxuan Zhou
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <MvCameraControl.h>

namespace aoi {

enum class CameraID : int {
    CCD1_TOP    = 1,
    CCD2_SIDE   = 2,
    CCD3_BOTTOM = 3,
    CCD4_INNER  = 4
};

struct CameraConfig {
    CameraID          id;
    std::string       serial;            // e.g. "DA0232856"
    int               width;
    int               height;
    MvGvspPixelType   pixel_format;      // e.g. PixelType_Gvsp_BayerRG8
    float             exposure_us;
    float             gain_db;
    uint32_t          packet_size;       // jumbo frame byte size
    std::string       subnet;            // e.g. "192.168.10"
};

/// Callback signature: (camera_id, timestamp_ns, frame_mat)
using FrameCallback = std::function<void(CameraID, uint64_t, const cv::Mat&)>;

/**
 * Lock-free SPSC ring buffer for inter-thread frame passing.
 * One writer (SDK callback thread), one reader (processing thread).
 */
template <std::size_t N>
class FrameRingBuffer {
public:
    struct Slot { cv::Mat frame; uint64_t timestamp_ns = 0; };

    bool try_push(const cv::Mat& frame, uint64_t ts);
    bool try_pop(cv::Mat& frame, uint64_t& ts);
    std::size_t size() const;

private:
    std::array<Slot, N> slots_;
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

class GigEVisionCapture {
public:
    GigEVisionCapture();
    ~GigEVisionCapture();

    GigEVisionCapture(const GigEVisionCapture&) = delete;
    GigEVisionCapture& operator=(const GigEVisionCapture&) = delete;

    bool Initialize(const std::vector<CameraConfig>& configs);
    bool StartAcquisition(CameraID cam);
    bool StopAcquisition(CameraID cam);
    void StopAll();

    bool SetTriggerMode(CameraID cam, bool hardware);
    bool SetExposure(CameraID cam, float exposure_us);
    void RegisterFrameCallback(CameraID cam, FrameCallback cb);

    /// Blocking retrieval with timeout. Returns empty Mat on timeout.
    cv::Mat GetFrame(CameraID cam, int timeout_ms = 1000);

private:
    static constexpr int kMaxCameras   = 4;
    static constexpr int kRingCapacity = 8;

    struct CameraHandle {
        void*                            handle = nullptr;
        CameraConfig                     config;
        FrameCallback                    callback;
        FrameRingBuffer<8>               ring;
        std::atomic<bool>                acquiring{false};
    };

    std::array<CameraHandle, kMaxCameras> cameras_;
    bool                                  initialized_ = false;

    int  CamIndex(CameraID id) const { return static_cast<int>(id) - 1; }
    bool OpenDevice(CameraHandle& cam);
    bool ConfigureTransport(CameraHandle& cam);
    bool ConfigureTrigger(CameraHandle& cam, bool hardware);
    static void __stdcall SdkFrameCallback(unsigned char* pData,
                                           MV_FRAME_OUT_INFO_EX* pFrameInfo,
                                           void* pUser);
    cv::Mat ConvertToMat(unsigned char* data,
                         const MV_FRAME_OUT_INFO_EX& info,
                         MvGvspPixelType target_fmt);
};

}  // namespace aoi
