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
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::System;

namespace winrt::ChatClient::implementation
{
    MainWindow::MainWindow()
        : messages_(single_threaded_observable_vector<hstring>()),
          client_(chat::ClientEvents{
              [this](const std::wstring& line) {
                  post([this, line] { append_line(line); });
              },
              [this](const std::wstring& status) {
                  post([this, status] { set_status(status); });
              },
          })
    {
        build_ui();
        Closed({this, &MainWindow::on_closed});
    }

    void MainWindow::build_ui()
    {
        Title(L"聊天客户端");
        AppWindow().Resize({980, 700});
        const FontFamily font{L"Microsoft YaHei UI"};

        Grid root;
        root.Padding(ThicknessHelper::FromUniformLength(20));
        root.RowSpacing(12);
        for (int row = 0; row < 5; ++row)
        {
            RowDefinition definition;
            definition.Height(row == 2 ? GridLength{1, GridUnitType::Star} : GridLengthHelper::Auto());
            root.RowDefinitions().Append(definition);
        }

        TextBlock title;
        title.Text(L"流式套接字聊天");
        title.FontSize(22);
        title.FontFamily(font);
        root.Children().Append(title);

        StackPanel bar;
        bar.Orientation(Orientation::Horizontal);
        bar.Spacing(8);
        Grid::SetRow(bar, 1);
        auto add_label = [&](const wchar_t* text) {
            TextBlock label;
            label.Text(text);
            label.VerticalAlignment(VerticalAlignment::Center);
            label.FontFamily(font);
            bar.Children().Append(label);
        };
        add_label(L"地址");
        host_box_ = TextBox();
        host_box_.Text(L"127.0.0.1");
        host_box_.Width(140);
        AutomationProperties::SetAutomationId(host_box_, L"HostBox");
        bar.Children().Append(host_box_);
        add_label(L"端口");
        port_box_ = TextBox();
        port_box_.Text(L"9000");
        port_box_.Width(80);
        AutomationProperties::SetAutomationId(port_box_, L"PortBox");
        bar.Children().Append(port_box_);
        add_label(L"昵称");
        nick_box_ = TextBox();
        nick_box_.Width(120);
        nick_box_.PlaceholderText(L"1–16 个字符");
        AutomationProperties::SetAutomationId(nick_box_, L"NickBox");
        bar.Children().Append(nick_box_);
        connect_button_ = Button();
        connect_button_.Content(box_value(L"连接"));
        connect_button_.Click({this, &MainWindow::Connect_Click});
        AutomationProperties::SetAutomationId(connect_button_, L"ConnectButton");
        disconnect_button_ = Button();
        disconnect_button_.Content(box_value(L"断开"));
        disconnect_button_.IsEnabled(false);
        disconnect_button_.Click({this, &MainWindow::Disconnect_Click});
        AutomationProperties::SetAutomationId(disconnect_button_, L"DisconnectButton");
        list_button_ = Button();
        list_button_.Content(box_value(L"在线列表"));
        list_button_.IsEnabled(false);
        list_button_.Click({this, &MainWindow::List_Click});
        AutomationProperties::SetAutomationId(list_button_, L"ListButton");
        bar.Children().Append(connect_button_);
        bar.Children().Append(disconnect_button_);
        bar.Children().Append(list_button_);
        root.Children().Append(bar);

        message_list_ = ListView();
        message_list_.FontFamily(font);
        message_list_.ItemsSource(messages_);
        AutomationProperties::SetAutomationId(message_list_, L"MessageList");
        Grid::SetRow(message_list_, 2);
        root.Children().Append(message_list_);

        Grid input_row;
        input_row.ColumnSpacing(8);
        Grid::SetRow(input_row, 3);
        ColumnDefinition stretch;
        stretch.Width(GridLength{1, GridUnitType::Star});
        input_row.ColumnDefinitions().Append(stretch);
        for (int column = 0; column < 3; ++column)
        {
            ColumnDefinition automatic;
            automatic.Width(GridLengthHelper::Auto());
            input_row.ColumnDefinitions().Append(automatic);
        }
        input_box_ = TextBox();
        input_box_.PlaceholderText(L"输入英文或中文消息");
        input_box_.IsEnabled(false);
        input_box_.KeyDown({this, &MainWindow::Input_KeyDown});
        AutomationProperties::SetAutomationId(input_box_, L"InputBox");
        send_button_ = Button();
        send_button_.Content(box_value(L"发送"));
        send_button_.IsEnabled(false);
        send_button_.Click({this, &MainWindow::Send_Click});
        AutomationProperties::SetAutomationId(send_button_, L"SendButton");
        Grid::SetColumn(send_button_, 1);
        burst_box_ = NumberBox();
        burst_box_.Value(100);
        burst_box_.Minimum(1);
        burst_box_.Maximum(500);
        burst_box_.Width(120);
        burst_box_.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
        AutomationProperties::SetAutomationId(burst_box_, L"BurstBox");
        Grid::SetColumn(burst_box_, 2);
        burst_button_ = Button();
        burst_button_.Content(box_value(L"连续发送"));
        burst_button_.IsEnabled(false);
        burst_button_.Click({this, &MainWindow::Burst_Click});
        AutomationProperties::SetAutomationId(burst_button_, L"BurstButton");
        Grid::SetColumn(burst_button_, 3);
        input_row.Children().Append(input_box_);
        input_row.Children().Append(send_button_);
        input_row.Children().Append(burst_box_);
        input_row.Children().Append(burst_button_);
        root.Children().Append(input_row);

        status_text_ = TextBlock();
        status_text_.Text(L"未连接");
        status_text_.FontFamily(font);
        status_text_.TextWrapping(TextWrapping::Wrap);
        AutomationProperties::SetAutomationId(status_text_, L"StatusText");
        Grid::SetRow(status_text_, 4);
        root.Children().Append(status_text_);

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

    void MainWindow::append_line(const std::wstring& line)
    {
        messages_.Append(line);
        if (messages_.Size() > 400)
        {
            messages_.RemoveAt(0);
        }
        message_list_.ScrollIntoView(message_list_.Items().GetAt(message_list_.Items().Size() - 1));
    }

    void MainWindow::set_status(const std::wstring& status)
    {
        status_text_.Text(status);
        if (!client_.connected())
        {
            set_connected_ui(false);
        }
    }

    void MainWindow::set_connected_ui(bool connected)
    {
        connect_button_.IsEnabled(!connected);
        host_box_.IsEnabled(!connected);
        port_box_.IsEnabled(!connected);
        nick_box_.IsEnabled(!connected);
        disconnect_button_.IsEnabled(connected);
        list_button_.IsEnabled(connected);
        send_button_.IsEnabled(connected);
        burst_button_.IsEnabled(connected);
        input_box_.IsEnabled(connected);
    }

    void MainWindow::show_error(const std::wstring& error)
    {
        append_line(L"系统: " + error);
    }

    void MainWindow::Connect_Click(IInspectable const&, RoutedEventArgs const&)
    {
        const std::wstring port_text = port_box_.Text().c_str();
        wchar_t* end = nullptr;
        const unsigned long port = std::wcstoul(port_text.c_str(), &end, 10);
        if (end == port_text.c_str() || port == 0 || port > 65535)
        {
            show_error(L"端口需要是 1 到 65535 的整数");
            return;
        }
        std::wstring error;
        if (!client_.connect_to(host_box_.Text().c_str(), static_cast<uint16_t>(port), nick_box_.Text().c_str(), error))
        {
            show_error(error);
            return;
        }
        set_connected_ui(true);
        Title(L"聊天客户端 - " + nick_box_.Text());
    }

    void MainWindow::Disconnect_Click(IInspectable const&, RoutedEventArgs const&)
    {
        client_.disconnect();
        set_connected_ui(false);
        Title(L"聊天客户端");
    }

    void MainWindow::send_current_text()
    {
        const std::wstring text = input_box_.Text().c_str();
        std::wstring error;
        if (!client_.send_text(text, error))
        {
            show_error(error);
            return;
        }
        input_box_.Text(L"");
    }

    void MainWindow::Send_Click(IInspectable const&, RoutedEventArgs const&)
    {
        send_current_text();
    }

    void MainWindow::List_Click(IInspectable const&, RoutedEventArgs const&)
    {
        std::wstring error;
        if (!client_.request_list(error))
        {
            show_error(error);
        }
    }

    void MainWindow::Burst_Click(IInspectable const&, RoutedEventArgs const&)
    {
        std::wstring error;
        if (!client_.start_burst(static_cast<int>(burst_box_.Value()), error))
        {
            show_error(error);
        }
    }

    void MainWindow::Input_KeyDown(IInspectable const&, KeyRoutedEventArgs const& args)
    {
        if (args.Key() == VirtualKey::Enter)
        {
            args.Handled(true);
            send_current_text();
        }
    }

    void MainWindow::on_closed(IInspectable const&, WindowEventArgs const&)
    {
        open_.store(false);
        client_.disconnect();
    }
}
