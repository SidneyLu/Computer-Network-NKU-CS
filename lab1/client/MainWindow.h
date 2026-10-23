#pragma once

#include "MainWindow.g.h"
#include "client_core.h"

#include <atomic>
#include <functional>

namespace winrt::ChatClient::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void Connect_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Disconnect_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Send_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void List_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Burst_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Input_KeyDown(
            winrt::Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);

    private:
        void build_ui();
        void post(std::function<void()> action);
        void append_line(const std::wstring& line);
        void set_status(const std::wstring& status);
        void set_connected_ui(bool connected);
        void show_error(const std::wstring& error);
        void send_current_text();
        void on_closed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::WindowEventArgs const&);

        Microsoft::UI::Xaml::Controls::TextBox host_box_{nullptr};
        Microsoft::UI::Xaml::Controls::TextBox port_box_{nullptr};
        Microsoft::UI::Xaml::Controls::TextBox nick_box_{nullptr};
        Microsoft::UI::Xaml::Controls::Button connect_button_{nullptr};
        Microsoft::UI::Xaml::Controls::Button disconnect_button_{nullptr};
        Microsoft::UI::Xaml::Controls::Button list_button_{nullptr};
        Microsoft::UI::Xaml::Controls::ListView message_list_{nullptr};
        Microsoft::UI::Xaml::Controls::TextBox input_box_{nullptr};
        Microsoft::UI::Xaml::Controls::Button send_button_{nullptr};
        Microsoft::UI::Xaml::Controls::NumberBox burst_box_{nullptr};
        Microsoft::UI::Xaml::Controls::Button burst_button_{nullptr};
        Microsoft::UI::Xaml::Controls::TextBlock status_text_{nullptr};
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::hstring> messages_;
        std::atomic<bool> open_{true};
        chat::ChatClient client_;
    };
}

namespace winrt::ChatClient::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
