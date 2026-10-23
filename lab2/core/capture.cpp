#include "capture.h"

#include <winsock2.h>
#include <windows.h>
#include <pcap.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

namespace cap {
namespace {

std::wstring to_wide(const char* text)
{
    if (text == nullptr || text[0] == '\0')
    {
        return {};
    }
    const int bytes = static_cast<int>(std::strlen(text));
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, bytes, nullptr, 0);
    UINT code_page = CP_UTF8;
    if (count <= 0)
    {
        code_page = CP_ACP;
        count = MultiByteToWideChar(CP_ACP, 0, text, bytes, nullptr, 0);
    }
    if (count <= 0)
    {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(code_page, code_page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, text, bytes, wide.data(), count);
    return wide;
}

bool ensure_pcap(std::wstring& error)
{
    static std::once_flag once;
    static std::wstring init_error;
    std::call_once(once, [] {
        char errbuf[PCAP_ERRBUF_SIZE]{};
        if (pcap_init(PCAP_CHAR_ENC_UTF_8, errbuf) != 0)
        {
            init_error = to_wide(errbuf);
            if (init_error.empty())
            {
                init_error = L"pcap_init 失败";
            }
        }
    });
    error = init_error;
    return error.empty();
}

} // namespace

struct CaptureSession::Impl {
    std::mutex mu;
    pcap_t* handle = nullptr;
    std::thread worker;
    std::atomic<bool> stopping{false};
};

CaptureSession::CaptureSession() : impl_(std::make_unique<Impl>()) {}

CaptureSession::~CaptureSession()
{
    stop();
}

std::vector<Device> CaptureSession::list_devices(std::wstring& error)
{
    std::vector<Device> devices;
    if (!ensure_pcap(error))
    {
        return devices;
    }
    char errbuf[PCAP_ERRBUF_SIZE]{};
    pcap_if_t* all = nullptr;
    if (pcap_findalldevs(&all, errbuf) != 0)
    {
        error = to_wide(errbuf);
        if (error.empty())
        {
            error = L"pcap_findalldevs 失败";
        }
        return devices;
    }
    for (pcap_if_t* it = all; it != nullptr; it = it->next)
    {
        Device device;
        device.name = it->name != nullptr ? it->name : "";
        const char* text = (it->description != nullptr && it->description[0] != '\0') ? it->description : it->name;
        device.label = to_wide(text);
        if (device.label.empty())
        {
            device.label = L"（未命名设备）";
        }
        devices.push_back(std::move(device));
    }
    pcap_freealldevs(all);
    error.clear();
    return devices;
}

bool CaptureSession::start(const std::string& device_name, const std::wstring& label, uint32_t limit, FrameHandler on_frame, StatusHandler on_status, DoneHandler on_done, std::wstring& error)
{
    stop();
    if (!ensure_pcap(error))
    {
        return false;
    }
    if (device_name.empty())
    {
        error = L"请选择网卡";
        return false;
    }

    char errbuf[PCAP_ERRBUF_SIZE]{};
    // 65535 字节足够装下一帧以太网。混杂模式打开，超时 1000 毫秒以便停止时能返回。
    pcap_t* handle = pcap_open_live(device_name.c_str(), 65535, 1, 1000, errbuf);
    if (handle == nullptr)
    {
        error = to_wide(errbuf);
        if (error.empty())
        {
            error = L"pcap_open_live 失败";
        }
        return false;
    }
    if (pcap_datalink(handle) != DLT_EN10MB)
    {
        pcap_close(handle);
        error = L"所选设备不是以太网，不能按 MAC 帧解析";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        impl_->handle = handle;
        impl_->stopping.store(false);
    }
    if (on_status)
    {
        on_status(L"正在捕获：" + label + L"，链路类型以太网 (DLT_EN10MB)");
    }

    impl_->worker = std::thread([this, handle, limit, on_frame = std::move(on_frame), on_status = std::move(on_status), on_done = std::move(on_done)] {
        uint32_t captured = 0;
        uint32_t skipped = 0;
        std::wstring failure;
        while (!impl_->stopping.load())
        {
            if (limit != 0 && captured >= limit)
            {
                break;
            }
            pcap_pkthdr* header = nullptr;
            const u_char* data = nullptr;
            const int rc = pcap_next_ex(handle, &header, &data);
            if (rc == 0)
            {
                continue;
            }
            if (rc == -2)
            {
                break;
            }
            if (rc < 0)
            {
                failure = to_wide(pcap_geterr(handle));
                if (failure.empty())
                {
                    failure = L"pcap_next_ex 失败";
                }
                break;
            }
            if (header == nullptr || data == nullptr || header->caplen < 15)
            {
                ++skipped;
                if (on_status)
                {
                    on_status(L"正在捕获，已跳过 " + std::to_wstring(skipped) + L" 个不足 15 字节的帧");
                }
                continue;
            }
            const EthernetView view = parse_ethernet(data, header->caplen);
            if (!view.ok)
            {
                continue;
            }
            FrameLine line;
            line.index = ++captured;
            line.length = header->len;
            line.dst = format_mac(view.dst);
            line.src = format_mac(view.src);
            line.type = format_type(view.type);
            line.next = format_byte(view.after_type);
            if (on_frame)
            {
                on_frame(line);
            }
        }

        {
            std::lock_guard<std::mutex> lock(impl_->mu);
            if (impl_->handle == handle)
            {
                pcap_close(impl_->handle);
                impl_->handle = nullptr;
            }
        }
        if (on_status)
        {
            std::wstring summary = L"捕获结束，共 " + std::to_wstring(captured) + L" 帧";
            if (skipped != 0)
            {
                summary += L"，跳过 " + std::to_wstring(skipped) + L" 个不足 15 字节的帧";
            }
            if (!failure.empty())
            {
                summary += L"。" + failure;
            }
            on_status(summary);
        }
        if (on_done)
        {
            on_done();
        }
    });
    error.clear();
    return true;
}

void CaptureSession::stop()
{
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        impl_->stopping.store(true);
        if (impl_->handle != nullptr)
        {
            pcap_breakloop(impl_->handle);
        }
    }
    if (impl_->worker.joinable())
    {
        impl_->worker.join();
    }
}

}
