# Npcap 数据包捕获

用 C++17、WinUI 3 和 Npcap 捕获本机以太网帧。窗口列出网卡，捕获后每一行显示序号、长度、目的 MAC、源 MAC、类型字段和类型字段之后的第一个字节。捕获结果只显示在窗口里，不写入硬盘。

## 目录结构

```text
lab2/
  Capture.sln              解决方案
  build.bat                编译 Release x64
  core/                    抓包核心，不含界面
    ethernet.h / .cpp      以太网帧解析
    capture.h / .cpp       设备列表、打开网卡、捕获线程
  app/                     WinUI 窗口
  third_party/npcap/       Npcap SDK 1.17 的头文件和导入库
  bin/capture/             可执行文件和自包含运行库
  figure/                  实验报告用的窗口截图
  lab2.tex / lab2.pdf      实验报告
```

`app` 只负责窗口。`pcap_findalldevs`、`pcap_open_live` 和 `pcap_next_ex` 在 `core`。分发程序时拷贝整个 `bin\capture` 目录，不要只拷贝 exe。目录里的 DLL 和 `*.pri` 是 Windows App SDK 自包含运行库。

## 环境

本机需要安装 Npcap，并勾选 WinPcap API 兼容模式。当前开发机安装的是 Npcap 1.90，服务 `npcap` 处于运行状态，注册表 `AdminOnly` 为 0，因此普通进程可以抓包。若打开网卡失败，用管理员身份重新运行。

编译还需要 Npcap SDK。本目录已经放了 SDK 1.17，头文件在 `third_party\npcap\Include`，x64 导入库在 `third_party\npcap\Lib\x64`。

## 编译

在已安装 Visual Studio 2022（含“使用 C++ 的 Windows 应用程序开发”工作负载）的机器上，于本目录执行：

```bat
build.bat
```

脚本会加载 x64 编译环境，并用 MSBuild 生成 Release。完成后得到 `bin\capture\capture.exe`。

目标系统为 64 位 Windows 10（1809 或更新）或 Windows 11。若提示缺少 `VCRUNTIME140.dll`，安装 Microsoft Visual C++ 2015–2022 x64 可再发行组件。

## 运行

1. 运行 `bin\capture\capture.exe`。窗口列出 Npcap 看到的设备。
2. 在下拉框中选择正在使用的以太网或无线网卡。回环和部分虚拟网卡不是以太网，开始捕获时状态行会说明原因。
3. 「条数」填要捕获的帧数。填 `0` 表示一直捕获，直到点击「停止」。
4. 点击「开始捕获」。列表中每行一帧：序号、长度、目的 MAC、源 MAC、类型、类型后首字节。类型旁会附上 IPv4、ARP、IPv6 或 VLAN 这些常见名称。
5. 点击「停止」后不再追加新行。关闭窗口时捕获线程会先结束。

可以在捕获期间用浏览器打开网页，或在另一个终端执行 `ping`，让所选网卡上出现流量。
