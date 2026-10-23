#include "pch.h"
#include "MainWindow.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <cwchar>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Automation;

namespace winrt::ChatServer::implementation
{
    MainWindow::MainWindow()
        : logs_(single_threaded_observable_vector<hstring>()),
          server_(chat::ServerEvents{
              [this](const std::wstring& line) {
                  post([this, line] { append_log(line); });
              },
              [this](const std::vector<std::wstring>& names) {
                  post([this, names] { replace_users(names); });
              },
          })
    {
        build_ui();
        Closed({this, &MainWindow::on_closed});
    }

    void MainWindow::build_ui()
    {
        Title(L"聊天服务器");
        AppWindow().Resize({900, 680});

        Grid root;
        root.Padding(ThicknessHelper::FromUniformLength(20));
        root.RowSpacing(12);
        RowDefinition header;
        header.Height(GridLengthHelper::Auto());
        RowDefinition toolbar;
        toolbar.Height(GridLengthHelper::Auto());
        RowDefinition users;
        users.Height(GridLengthHelper::Auto());
        RowDefinition logs;
        logs.Height(GridLength{2, GridUnitType::Star});
        root.RowDefinitions().Append(header);
        root.RowDefinitions().Append(toolbar);
        root.RowDefinitions().Append(users);
        root.RowDefinitions().Append(logs);

        TextBlock title;
        title.Text(L"流式套接字聊天服务器");
        title.FontSize(22);
        title.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{L"Microsoft YaHei UI"});
        root.Children().Append(title);

        StackPanel bar;
        bar.Orientation(Orientation::Horizontal);
        bar.Spacing(8);
        Grid::SetRow(bar, 1);
        TextBlock port_label;
        port_label.Text(L"端口");
        port_label.VerticalAlignment(VerticalAlignment::Center);
        port_label.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{L"Microsoft YaHei UI"});
        port_box_ = TextBox();
        port_box_.Text(L"9000");
        port_box_.Width(100);
        AutomationProperties::SetAutomationId(port_box_, L"PortBox");
        start_button_ = Button();
        start_button_.Content(box_value(L"开始监听"));
        start_button_.Click({this, &MainWindow::Start_Click});
        AutomationProperties::SetAutomationId(start_button_, L"StartButton");
        stop_button_ = Button();
        stop_button_.Content(box_value(L"停止"));
        stop_button_.IsEnabled(false);
        stop_button_.Click({this, &MainWindow::Stop_Click});
        AutomationProperties::SetAutomationId(stop_button_, L"StopButton");
        bar.Children().Append(port_label);
        bar.Children().Append(port_box_);
        bar.Children().Append(start_button_);
        bar.Children().Append(stop_button_);
        root.Children().Append(bar);

        StackPanel users_panel;
        users_panel.Spacing(6);
        Grid::SetRow(users_panel, 2);
        TextBlock users_title;
        users_title.Text(L"在线用户");
        users_title.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{L"Microsoft YaHei UI"});
        user_text_ = TextBlock();
        user_text_.Text(L"（无）");
        user_text_.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{L"Microsoft YaHei UI"});
        user_text_.TextWrapping(TextWrapping::Wrap);
        AutomationProperties::SetAutomationId(user_text_, L"UserList");
        users_panel.Children().Append(users_title);
        users_panel.Children().Append(user_text_);
        root.Children().Append(users_panel);

        log_list_ = ListView();
        log_list_.Header(box_value(L"日志"));
        log_list_.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{L"Microsoft YaHei UI"});
        log_list_.ItemsSource(logs_);
        AutomationProperties::SetAutomationId(log_list_, L"LogList");
        Grid::SetRow(log_list_, 3);
        root.Children().Append(log_list_);

        Content(root);
    }

    void MainWindow::post(std::function<void()> action)
    {
        if (!open_.load())
        {
            return;
        }
        DispatcherQueue().TryEnqueue([this, action = std::move(action)] {
            if (!open_.load())
            {
                return;
            }
            action();
        });
    }

    void MainWindow::append_log(const std::wstring& line)
    {
        logs_.Append(line);
        if (logs_.Size() > 300)
        {
            logs_.RemoveAt(0);
        }
        log_list_.ScrollIntoView(log_list_.Items().GetAt(log_list_.Items().Size() - 1));
    }

    void MainWindow::replace_users(const std::vector<std::wstring>& names)
    {
        if (names.empty())
        {
            user_text_.Text(L"（无）");
            return;
        }
        std::wstring text;
        for (const auto& name : names)
        {
            if (!text.empty())
            {
                text += L"\n";
            }
            text += name;
        }
        user_text_.Text(text);
    }

    void MainWindow::Start_Click(IInspectable const&, RoutedEventArgs const&)
    {
        const std::wstring port_text = port_box_.Text().c_str();
        wchar_t* end = nullptr;
        const unsigned long port = std::wcstoul(port_text.c_str(), &end, 10);
        if (end == port_text.c_str() || port == 0 || port > 65535)
        {
            append_log(L"端口需要是 1 到 65535 的整数");
            return;
        }
        std::wstring error;
        if (!server_.start(static_cast<uint16_t>(port), error))
        {
            append_log(error);
            return;
        }
        start_button_.IsEnabled(false);
        stop_button_.IsEnabled(true);
        port_box_.IsEnabled(false);
    }

    void MainWindow::Stop_Click(IInspectable const&, RoutedEventArgs const&)
    {
        server_.stop();
        start_button_.IsEnabled(true);
        stop_button_.IsEnabled(false);
        port_box_.IsEnabled(true);
    }

    void MainWindow::on_closed(IInspectable const&, WindowEventArgs const&)
    {
        open_.store(false);
        server_.stop();
    }
}
