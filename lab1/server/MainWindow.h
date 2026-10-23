#pragma once

#include "MainWindow.g.h"
#include "server_core.h"

#include <atomic>
#include <functional>

namespace winrt::ChatServer::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void Start_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Stop_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void build_ui();
        void post(std::function<void()> action);
        void append_log(const std::wstring& line);
        void replace_users(const std::vector<std::wstring>& names);
        void on_closed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::WindowEventArgs const&);

        Microsoft::UI::Xaml::Controls::TextBox port_box_{nullptr};
        Microsoft::UI::Xaml::Controls::Button start_button_{nullptr};
        Microsoft::UI::Xaml::Controls::Button stop_button_{nullptr};
        Microsoft::UI::Xaml::Controls::TextBlock user_text_{nullptr};
        Microsoft::UI::Xaml::Controls::ListView log_list_{nullptr};
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::hstring> logs_;
        std::atomic<bool> open_{true};
        chat::ChatServer server_;
    };
}

namespace winrt::ChatServer::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
