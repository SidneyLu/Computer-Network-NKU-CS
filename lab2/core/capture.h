#pragma once

#include "ethernet.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cap {

struct Device {
    std::string name;
    std::wstring label;
};

// 设备枚举和捕获循环。不包含界面类型。回调可能来自捕获线程。
class CaptureSession {
public:
    using FrameHandler = std::function<void(const FrameLine&)>;
    using StatusHandler = std::function<void(const std::wstring&)>;
    using DoneHandler = std::function<void()>;

    CaptureSession();
    ~CaptureSession();

    CaptureSession(const CaptureSession&) = delete;
    CaptureSession& operator=(const CaptureSession&) = delete;

    std::vector<Device> list_devices(std::wstring& error);
    // limit 为 0 时一直捕获，直到 stop。成功后捕获在新线程中进行。
    bool start(const std::string& device_name, const std::wstring& label, uint32_t limit, FrameHandler on_frame, StatusHandler on_status, DoneHandler on_done, std::wstring& error);
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
