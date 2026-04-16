/**
 * GigE Vision capture implementation — Hikrobot MVS SDK wrapper.
 *
 * Key design decisions:
 *   - Jumbo frames (8192 bytes) to reduce CPU interrupt load on GigE link
 *   - Hardware trigger via GPIO Line0 synced with Gardasoft RT820F-20
 *   - Lock-free ring buffer avoids mutex contention between SDK callback
 *     thread and the processing pipeline thread
 *   - Zero-copy cv::Mat construction from SDK payload where possible
 *
 * Author: Rongxuan Zhou
 */

#include "GigEVisionCapture.h"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

#include <opencv2/imgproc.hpp>

namespace aoi {

// ---------------------------------------------------------------------------
// FrameRingBuffer implementation
// ---------------------------------------------------------------------------
template <std::size_t N>
bool FrameRingBuffer<N>::try_push(const cv::Mat& frame, uint64_t ts) {
    std::size_t h = head_.load(std::memory_order_relaxed);
    std::size_t next = (h + 1) % N;
    if (next == tail_.load(std::memory_order_acquire))
        return false;  // full — drop frame (acceptable under burst)
    slots_[h].frame = frame;
    slots_[h].timestamp_ns = ts;
    head_.store(next, std::memory_order_release);
    return true;
}

template <std::size_t N>
bool FrameRingBuffer<N>::try_pop(cv::Mat& frame, uint64_t& ts) {
    std::size_t t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire))
        return false;  // empty
    frame = std::move(slots_[t].frame);
    ts = slots_[t].timestamp_ns;
    tail_.store((t + 1) % N, std::memory_order_release);
    return true;
}

template <std::size_t N>
std::size_t FrameRingBuffer<N>::size() const {
    auto h = head_.load(std::memory_order_acquire);
    auto t = tail_.load(std::memory_order_acquire);
    return (h >= t) ? (h - t) : (N - t + h);
}

// Explicit instantiation for the capacity used in CameraHandle.
template class FrameRingBuffer<8>;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------
GigEVisionCapture::GigEVisionCapture() = default;

GigEVisionCapture::~GigEVisionCapture() {
    StopAll();
    for (auto& cam : cameras_) {
        if (cam.handle) {
            MV_CC_CloseDevice(cam.handle);
            MV_CC_DestroyHandle(cam.handle);
            cam.handle = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// Initialize — enumerate GigE devices on vision VLAN and open configured ones
// ---------------------------------------------------------------------------
bool GigEVisionCapture::Initialize(const std::vector<CameraConfig>& configs) {
    MV_CC_DEVICE_INFO_LIST device_list{};
    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE, &device_list);
    if (ret != MV_OK) {
        fprintf(stderr, "[GigECapture] EnumDevices failed: 0x%08X\n", ret);
        return false;
    }

    printf("[GigECapture] Found %u GigE devices\n", device_list.nDeviceNum);

    for (const auto& cfg : configs) {
        int idx = CamIndex(cfg.id);
        if (idx < 0 || idx >= kMaxCameras) continue;
        cameras_[idx].config = cfg;

        // Match device by serial number within the vision subnet
        bool found = false;
        for (unsigned i = 0; i < device_list.nDeviceNum; ++i) {
            auto* info = device_list.pDeviceInfo[i];
            if (info->nTLayerType != MV_GIGE_DEVICE) continue;

            auto& gige = info->SpecialInfo.stGigEInfo;
            char serial[64]{};
            std::strncpy(serial, reinterpret_cast<const char*>(gige.chSerialNumber),
                         sizeof(serial) - 1);

            // Verify device is on the expected vision subnet (192.168.10.x)
            uint32_t ip = gige.nCurrentIp;
            char ip_str[32];
            snprintf(ip_str, sizeof(ip_str), "%u.%u.%u",
                     (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF);

            if (cfg.serial == serial && cfg.subnet == ip_str) {
                if (!OpenDevice(cameras_[idx])) return false;
                found = true;
                printf("[GigECapture] CCD%d (%s) opened on %s.%u\n",
                       static_cast<int>(cfg.id), serial, ip_str, ip & 0xFF);
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "[GigECapture] CCD%d serial %s not found on subnet %s\n",
                    static_cast<int>(cfg.id), cfg.serial.c_str(), cfg.subnet.c_str());
            return false;
        }
    }

    initialized_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Open a single device and apply transport + sensor configuration
// ---------------------------------------------------------------------------
bool GigEVisionCapture::OpenDevice(CameraHandle& cam) {
    int ret = MV_CC_CreateHandle(&cam.handle, cam.config.serial.c_str());
    if (ret != MV_OK) return false;

    ret = MV_CC_OpenDevice(cam.handle);
    if (ret != MV_OK) {
        MV_CC_DestroyHandle(cam.handle);
        cam.handle = nullptr;
        return false;
    }

    if (!ConfigureTransport(cam)) return false;
    if (!ConfigureTrigger(cam, true)) return false;

    // Sensor parameters
    MV_CC_SetIntValue(cam.handle, "Width",  cam.config.width);
    MV_CC_SetIntValue(cam.handle, "Height", cam.config.height);
    MV_CC_SetEnumValue(cam.handle, "PixelFormat", cam.config.pixel_format);
    MV_CC_SetFloatValue(cam.handle, "ExposureTime", cam.config.exposure_us);
    MV_CC_SetFloatValue(cam.handle, "Gain", cam.config.gain_db);

    // Register SDK streaming callback
    ret = MV_CC_RegisterImageCallBackEx(cam.handle, SdkFrameCallback, &cam);
    if (ret != MV_OK) {
        fprintf(stderr, "[GigECapture] RegisterCallback failed: 0x%08X\n", ret);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// GigE transport tuning — jumbo frames, inter-packet delay, stream channels
// ---------------------------------------------------------------------------
bool GigEVisionCapture::ConfigureTransport(CameraHandle& cam) {
    // Jumbo frame — 8192 bytes to minimise interrupt overhead
    constexpr uint32_t kJumboFrameSize = 8192;
    uint32_t pkt_size = cam.config.packet_size > 0
                            ? cam.config.packet_size
                            : kJumboFrameSize;
    MV_CC_SetIntValue(cam.handle, "GevSCPSPacketSize", pkt_size);

    // Inter-packet delay — tuned to avoid switch congestion on 4-camera VLAN.
    // ~40 us gap keeps aggregate bandwidth under 900 Mbit/s on a 1 GbE link.
    MV_CC_SetIntValue(cam.handle, "GevSCPD", 40);

    // Receive buffer — 3 frames worth to absorb trigger jitter
    int frame_bytes = cam.config.width * cam.config.height;  // mono
    MV_CC_SetIntValue(cam.handle, "GevStreamChannelBufferSize",
                      frame_bytes * 3);

    // Heartbeat timeout — 5 s to survive brief network micro-interrupts
    MV_CC_SetIntValue(cam.handle, "GevHeartbeatTimeout", 5000);

    return true;
}

// ---------------------------------------------------------------------------
// Hardware / software trigger configuration
// ---------------------------------------------------------------------------
bool GigEVisionCapture::ConfigureTrigger(CameraHandle& cam, bool hardware) {
    MV_CC_SetEnumValue(cam.handle, "TriggerMode", MV_TRIGGER_MODE_ON);

    if (hardware) {
        // Line0 receives TTL rising edge from Gardasoft RT820F-20
        MV_CC_SetEnumValue(cam.handle, "TriggerSource",
                           MV_TRIGGER_SOURCE_LINE0);
        MV_CC_SetEnumValue(cam.handle, "TriggerActivation", 0);  // rising edge
        // Debounce filter — 10 us to reject electrical noise on the trigger line
        MV_CC_SetIntValue(cam.handle, "TriggerDebouncerValue", 10);
    } else {
        MV_CC_SetEnumValue(cam.handle, "TriggerSource",
                           MV_TRIGGER_SOURCE_SOFTWARE);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Acquisition control
// ---------------------------------------------------------------------------
bool GigEVisionCapture::StartAcquisition(CameraID cam_id) {
    auto& cam = cameras_[CamIndex(cam_id)];
    if (!cam.handle) return false;

    int ret = MV_CC_StartGrabbing(cam.handle);
    if (ret != MV_OK) {
        fprintf(stderr, "[GigECapture] StartGrabbing CCD%d failed: 0x%08X\n",
                static_cast<int>(cam_id), ret);
        return false;
    }
    cam.acquiring.store(true, std::memory_order_release);
    return true;
}

bool GigEVisionCapture::StopAcquisition(CameraID cam_id) {
    auto& cam = cameras_[CamIndex(cam_id)];
    if (!cam.handle || !cam.acquiring.load(std::memory_order_acquire))
        return true;

    cam.acquiring.store(false, std::memory_order_release);
    int ret = MV_CC_StopGrabbing(cam.handle);
    return ret == MV_OK;
}

void GigEVisionCapture::StopAll() {
    for (int i = 0; i < kMaxCameras; ++i)
        StopAcquisition(static_cast<CameraID>(i + 1));
}

bool GigEVisionCapture::SetTriggerMode(CameraID cam_id, bool hardware) {
    auto& cam = cameras_[CamIndex(cam_id)];
    return cam.handle && ConfigureTrigger(cam, hardware);
}

bool GigEVisionCapture::SetExposure(CameraID cam_id, float exposure_us) {
    auto& cam = cameras_[CamIndex(cam_id)];
    if (!cam.handle) return false;
    return MV_CC_SetFloatValue(cam.handle, "ExposureTime", exposure_us) == MV_OK;
}

void GigEVisionCapture::RegisterFrameCallback(CameraID cam_id, FrameCallback cb) {
    cameras_[CamIndex(cam_id)].callback = std::move(cb);
}

// ---------------------------------------------------------------------------
// SDK streaming callback — runs on the MVS internal thread
// ---------------------------------------------------------------------------
void __stdcall GigEVisionCapture::SdkFrameCallback(
        unsigned char* pData,
        MV_FRAME_OUT_INFO_EX* pFrameInfo,
        void* pUser) {
    auto* cam = static_cast<CameraHandle*>(pUser);
    if (!cam || !pData || !pFrameInfo) return;

    uint64_t ts = pFrameInfo->nDevTimeStampHigh;
    ts = (ts << 32) | pFrameInfo->nDevTimeStampLow;

    // Zero-copy wrap for mono8; otherwise convert via OpenCV
    cv::Mat frame;
    if (pFrameInfo->enPixelType == PixelType_Gvsp_Mono8) {
        frame = cv::Mat(pFrameInfo->nHeight, pFrameInfo->nWidth,
                        CV_8UC1, pData).clone();
    } else {
        // Bayer → BGR conversion is deferred to ImagePreprocessor;
        // here we just copy the raw Bayer payload.
        frame = cv::Mat(pFrameInfo->nHeight, pFrameInfo->nWidth,
                        CV_8UC1, pData).clone();
    }

    cam->ring.try_push(frame, ts);

    if (cam->callback) {
        cam->callback(cam->config.id, ts, frame);
    }
}

// ---------------------------------------------------------------------------
// Blocking frame retrieval with polling timeout
// ---------------------------------------------------------------------------
cv::Mat GigEVisionCapture::GetFrame(CameraID cam_id, int timeout_ms) {
    auto& cam = cameras_[CamIndex(cam_id)];
    cv::Mat frame;
    uint64_t ts;

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        if (cam.ring.try_pop(frame, ts))
            return frame;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    return {};  // empty Mat signals timeout
}

// ---------------------------------------------------------------------------
// Pixel format conversion helper (used when SDK callback provides non-mono)
// ---------------------------------------------------------------------------
cv::Mat GigEVisionCapture::ConvertToMat(unsigned char* data,
                                         const MV_FRAME_OUT_INFO_EX& info,
                                         MvGvspPixelType target_fmt) {
    MV_CC_PIXEL_CONVERT_PARAM cvt{};
    cvt.nWidth = info.nWidth;
    cvt.nHeight = info.nHeight;
    cvt.enSrcPixelType = info.enPixelType;
    cvt.pSrcData = data;
    cvt.nSrcDataLen = info.nFrameLen;
    cvt.enDstPixelType = target_fmt;

    int dst_size = info.nWidth * info.nHeight * 3;
    std::vector<unsigned char> dst_buf(dst_size);
    cvt.pDstBuffer = dst_buf.data();
    cvt.nDstBufferSize = dst_size;

    // ConvertToMat is only called on-demand; the primary path uses
    // deferred demosaicing in ImagePreprocessor for better control.
    cv::Mat result;
    if (target_fmt == PixelType_Gvsp_BGR8_Packed) {
        result = cv::Mat(info.nHeight, info.nWidth, CV_8UC3);
        std::memcpy(result.data, dst_buf.data(), dst_size);
    }
    return result;
}

}  // namespace aoi
