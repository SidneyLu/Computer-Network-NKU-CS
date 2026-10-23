#include "pch.h"
#include "MainWindow.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <cwchar>
#include <string>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Automation;
using namespace Microsoft::UI::Xaml::Media;

namespace winrt::PacketCapture::implementation
{
    namespace {

    void style_text(TextBlock const& block, bool header)
    {
        block.FontFamily(FontFamily{L"Microsoft YaHei UI"});
        block.VerticalAlignment(VerticalAlignment::Center);
        block.FontSize(header ? 13 : 14);
    }

    } // namespace

    MainWindow::MainWindow()
    {
        build_ui();
        Closed({this, &MainWindow::on_closed});
        reload_devices();
    }

    void MainWindow::build_ui()
    {
        Title(L"数据包捕获");
        AppWindow().Resize({1080, 720});

        Grid root;
        root.Padding(ThicknessHelper::FromUniformLength(20));
        root.RowSpacing(12);
        for (int star = 0; star < 4; ++star)
        {
            RowDefinition row;
            row.Height(GridLengthHelper::Auto());
            root.RowDefinitions().Append(row);
        }
        RowDefinition list_row;
        list_row.Height(GridLength{1, GridUnitType::Star});
        root.RowDefinitions().Append(list_row);

        TextBlock title;
        title.Text(L"数据包捕获");
        title.FontSize(22);
        title.FontFamily(FontFamily{L"Microsoft YaHei UI"});
        root.Children().Append(title);

        StackPanel bar;
        bar.Orientation(Orientation::Horizontal);
        bar.Spacing(8);
        Grid::SetRow(bar, 1);

        TextBlock device_label;
        device_label.Text(L"网卡");
        device_label.VerticalAlignment(VerticalAlignment::Center);
        device_label.FontFamily(FontFamily{L"Microsoft YaHei UI"});
        device_box_ = ComboBox();
        device_box_.Width(420);
        device_box_.FontFamily(FontFamily{L"Microsoft YaHei UI"});
        AutomationProperties::SetAutomationId(device_box_, L"DeviceBox");

        refresh_button_ = Button();
        refresh_button_.Content(box_value(L"刷新"));
        refresh_button_.Click({this, &MainWindow::Refresh_Click});
        AutomationProperties::SetAutomationId(refresh_button_, L"RefreshButton");

        TextBlock count_label;
        count_label.Text(L"条数");
        count_label.VerticalAlignment(VerticalAlignment::Center);
        count_label.FontFamily(FontFamily{L"Microsoft YaHei UI"});
        count_box_ = TextBox();
        count_box_.Text(L"0");
        count_box_.Width(80);
        count_box_.FontFamily(FontFamily{L"Microsoft YaHei UI"});
        AutomationProperties::SetAutomationId(count_box_, L"CountBox");

        start_button_ = Button();
        start_button_.Content(box_value(L"开始捕获"));
        start_button_.Click({this, &MainWindow::Start_Click});
        AutomationProperties::SetAutomationId(start_button_, L"StartButton");
        stop_button_ = Button();
        stop_button_.Content(box_value(L"停止"));
        stop_button_.IsEnabled(false);
        stop_button_.Click({this, &MainWindow::Stop_Click});
        AutomationProperties::SetAutomationId(stop_button_, L"StopButton");

        bar.Children().Append(device_label);
        bar.Children().Append(device_box_);
        bar.Children().Append(refresh_button_);
        bar.Children().Append(count_label);
        bar.Children().Append(count_box_);
        bar.Children().Append(start_button_);
        bar.Children().Append(stop_button_);
        root.Children().Append(bar);

        status_text_ = TextBlock();
        status_text_.Text(L"正在列出网卡");
        status_text_.TextWrapping(TextWrapping::Wrap);
        status_text_.FontFamily(FontFamily{L"Microsoft YaHei UI"});
        AutomationProperties::SetAutomationId(status_text_, L"StatusText");
        Grid::SetRow(status_text_, 2);
        root.Children().Append(status_text_);

        auto header = make_row(L"序号", L"长度", L"目的 MAC", L"源 MAC", L"类型", L"类型后首字节", true);
        Grid::SetRow(header, 3);
        root.Children().Append(header);

        packet_list_ = ListView();
        packet_list_.FontFamily(FontFamily{L"Microsoft YaHei UI"});
        AutomationProperties::SetAutomationId(packet_list_, L"PacketList");
        Grid::SetRow(packet_list_, 4);
        root.Children().Append(packet_list_);

        Content(root);
    }

    Microsoft::UI::Xaml::Controls::Grid MainWindow::make_row(const std::wstring& index, const std::wstring& length, const std::wstring& dst, const std::wstring& src, const std::wstring& type, const std::wstring& next, bool header)
    {
        Grid grid;
        const double widths[] = {72, 80, 180, 180, 160, 140};
        for (double width : widths)
        {
            ColumnDefinition column;
            column.Width(GridLength{width, GridUnitType::Pixel});
            grid.ColumnDefinitions().Append(column);
        }
        const std::wstring cells[] = {index, length, dst, src, type, next};
        for (int i = 0; i < 6; ++i)
        {
            TextBlock block;
            block.Text(cells[i]);
            style_text(block, header);
            Grid::SetColumn(block, i);
            grid.Children().Append(block);
        }
        return grid;
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

    void MainWindow::set_status(const std::wstring& text)
    {
        status_text_.Text(text);
    }

    void MainWindow::set_busy(bool busy)
    {
        start_button_.IsEnabled(!busy);
        refresh_button_.IsEnabled(!busy);
        device_box_.IsEnabled(!busy);
        count_box_.IsEnabled(!busy);
        stop_button_.IsEnabled(busy);
    }

    void MainWindow::reload_devices()
    {
        std::wstring error;
        devices_ = session_.list_devices(error);
        device_box_.Items().Clear();
        if (!error.empty())
        {
            set_status(error);
            return;
        }
        if (devices_.empty())
        {
            set_status(L"没有找到可捕获的设备");
            return;
        }
        for (const auto& device : devices_)
        {
            device_box_.Items().Append(box_value(device.label));
        }
        device_box_.SelectedIndex(0);
        set_status(L"已列出 " + std::to_wstring(devices_.size()) + L" 个设备。条数填 0 表示一直捕获，直到点击停止。");
    }

    void MainWindow::append_row(const cap::FrameLine& line)
    {
        if (packet_list_.Items().Size() >= 1000)
        {
            packet_list_.Items().RemoveAt(0);
        }
        auto row = make_row(std::to_wstring(line.index), std::to_wstring(line.length), line.dst, line.src, line.type, line.next, false);
        packet_list_.Items().Append(row);
        packet_list_.ScrollIntoView(row);
    }

    void MainWindow::Refresh_Click(IInspectable const&, RoutedEventArgs const&)
    {
        reload_devices();
    }

    void MainWindow::Start_Click(IInspectable const&, RoutedEventArgs const&)
    {
        const int index = device_box_.SelectedIndex();
        if (index < 0 || static_cast<std::size_t>(index) >= devices_.size())
        {
            set_status(L"请选择网卡");
            return;
        }
        const std::wstring count_text = count_box_.Text().c_str();
        wchar_t* end = nullptr;
        const unsigned long count = std::wcstoul(count_text.c_str(), &end, 10);
        if (end == count_text.c_str() || *end != L'\0' || count > 100000)
        {
            set_status(L"条数需要是 0 到 100000 的整数，0 表示一直捕获");
            return;
        }

        const uint64_t generation = ++generation_;
        packet_list_.Items().Clear();
        std::wstring error;
        const bool started = session_.start(
            devices_[static_cast<std::size_t>(index)].name,
            devices_[static_cast<std::size_t>(index)].label,
            static_cast<uint32_t>(count),
            [this](const cap::FrameLine& line) {
                post([this, line] { append_row(line); });
            },
            [this](const std::wstring& text) {
                post([this, text] { set_status(text); });
            },
            [this, generation] {
                post([this, generation] {
                    if (generation != generation_.load())
                    {
                        return;
                    }
                    set_busy(false);
                });
            },
            error);
        if (!started)
        {
            set_status(error);
            set_busy(false);
            return;
        }
        set_busy(true);
    }

    void MainWindow::Stop_Click(IInspectable const&, RoutedEventArgs const&)
    {
        session_.stop();
        set_busy(false);
    }

    void MainWindow::on_closed(IInspectable const&, WindowEventArgs const&)
    {
        open_.store(false);
        session_.stop();
    }
}
