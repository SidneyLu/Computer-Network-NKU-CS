#pragma once

#include "MainWindow.g.h"
#include "capture.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace winrt::PacketCapture::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void Refresh_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Start_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Stop_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void build_ui();
        void post(std::function<void()> action);
        void reload_devices();
        void set_status(const std::wstring& text);
        void set_busy(bool busy);
        void append_row(const cap::FrameLine& line);
        void on_closed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::WindowEventArgs const&);
        Microsoft::UI::Xaml::Controls::Grid make_row(const std::wstring& index, const std::wstring& length, const std::wstring& dst, const std::wstring& src, const std::wstring& type, const std::wstring& next, bool header);

        Microsoft::UI::Xaml::Controls::ComboBox device_box_{nullptr};
        Microsoft::UI::Xaml::Controls::Button refresh_button_{nullptr};
        Microsoft::UI::Xaml::Controls::TextBox count_box_{nullptr};
        Microsoft::UI::Xaml::Controls::Button start_button_{nullptr};
        Microsoft::UI::Xaml::Controls::Button stop_button_{nullptr};
        Microsoft::UI::Xaml::Controls::TextBlock status_text_{nullptr};
        Microsoft::UI::Xaml::Controls::ListView packet_list_{nullptr};
        std::vector<cap::Device> devices_;
        std::atomic<bool> open_{true};
        std::atomic<uint64_t> generation_{0};
        cap::CaptureSession session_;
    };
}

namespace winrt::PacketCapture::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
