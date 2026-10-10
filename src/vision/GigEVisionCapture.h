#pragma once
/**
 * GigE Vision capture interface for the TI CSE AOI system.
 *
 * Wraps the Hikrobot MVS SDK to manage the machine's GigE Vision 2.0
 * cameras: four MV-GE501GC per inspection station for CCD#1/#2/#3, four
 * MV-GE2000C for CCD#4 (spec slide 10: "MV-GE501GC * 4",
 * "MV-GE2000C-T1P-C*4"), plus the single Poka-Yoke, NG-check and tray
 * monitor cameras. A camera is addressed by its station (CameraID) and its
 * unit index 1..4 within the station.
 *
 * Supports hardware triggering synchronised with the Gardasoft RT820F-20
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

#include "CameraID.h"

namespace aoi {

struct CameraConfig {
    CameraID          id;
    int               unit = 1;          // 1..4 within an inspection station, 1 otherwise
    std::string       serial;            // e.g. "DA0232856"
    int               width;
    int               height;
    MvGvspPixelType   pixel_format;      // e.g. PixelType_Gvsp_BayerRG8
    float             exposure_us;
    float             gain_db;
    uint32_t          packet_size;       // jumbo frame byte size
    std::string       subnet;            // e.g. "192.168.10"
};

/// Callback signature: (station, unit, timestamp_ns, frame_mat)
using FrameCallback = std::function<void(CameraID, int, uint64_t, const cv::Mat&)>;

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
    bool StartAcquisition(CameraID cam, int unit = 1);
    bool StopAcquisition(CameraID cam, int unit = 1);
    void StopAll();

    bool SetTriggerMode(CameraID cam, int unit, bool hardware);
    bool SetExposure(CameraID cam, int unit, float exposure_us);
    void RegisterFrameCallback(CameraID cam, int unit, FrameCallback cb);

    /// Blocking retrieval with timeout. Returns empty Mat on timeout or unknown slot.
    cv::Mat GetFrame(CameraID cam, int unit = 1, int timeout_ms = 1000);

    /// Number of configured camera slots.
    int SlotCount() const { return static_cast<int>(cameras_.size()); }

    static constexpr int kMaxCameras   = 20;   // 4 stations x 4 units + auxiliary cameras
    static constexpr int kRingCapacity = 8;

private:
    struct CameraHandle {
        void*                            handle = nullptr;
        CameraConfig                     config;
        FrameCallback                    callback;
        FrameRingBuffer<kRingCapacity>   ring;
        std::atomic<bool>                acquiring{false};
    };

    std::vector<std::unique_ptr<CameraHandle>> cameras_;
    bool                                       initialized_ = false;

    /// Index into cameras_ for (station, unit); -1 when not configured.
    int  FindSlot(CameraID id, int unit) const;
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
