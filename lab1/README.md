# 流式套接字多人聊天

用 C++17、Winsock2 和 WinUI 3 实现的多人聊天程序。TCP 字节流按固定 10 字节头组帧，每条连接的每个方向带递增序号，用来观察有没有丢消息。默认端口 `9000`。

## 目录结构

```text
lab1/
  Chat.sln                 解决方案：服务端、客户端、环回测试
  build.bat                编译 Release x64
  shared/                  协议与套接字核心，界面工程共用
    protocol.h             帧布局、消息类型、默认端口
    netutil.h / .cpp       组帧、拆帧、UTF-8 与 UTF-16 转换
    server_core.h / .cpp   监听、客户表、转发
    client_core.h / .cpp   连接、发送、接收、序号统计
  server/                  WinUI 服务端窗口
  client/                  WinUI 客户端窗口
  tests/
    loopback.cpp           不带界面的环回测试
    gui_check.ps1          自动操作三个客户端并截图
  bin/                     编译输出
    chat_server/           服务端及自包含运行库
    chat_client/           客户端及自包含运行库
    loopback/              环回测试程序
  figure/                  实验报告用的界面截图
  lab1.tex / lab1.pdf      实验报告
```

`server` 与 `client` 只负责窗口。套接字调用在 `shared`。分发程序时拷贝整个 `bin\chat_server` 或 `bin\chat_client` 目录，不要只拷贝 exe。目录里的 DLL、语言子目录和 `*.pri` 是 Windows App SDK 自包含运行库。

## 编译

在已安装 Visual Studio 2022（含“使用 C++ 的 Windows 应用程序开发”工作负载）的机器上，于本目录执行：

```bat
build.bat
```

脚本会加载 x64 编译环境，并用 MSBuild 生成 Release。完成后得到：

- `bin\chat_server\chat_server.exe`
- `bin\chat_client\chat_client.exe`
- `bin\loopback\chat_loopback.exe`

目标系统为 64 位 Windows 10（1809 或更新）或 Windows 11。若提示缺少 `VCRUNTIME140.dll`，安装 Microsoft Visual C++ 2015–2022 x64 可再发行组件。

## 运行聊天程序

1. 运行 `bin\chat_server\chat_server.exe`。端口保持 `9000`，点击「开始监听」。日志出现「开始监听端口 9000」后即可接受连接。
2. 运行 `bin\chat_client\chat_client.exe`。可以同时打开多个客户端。
3. 地址填服务器所在机器的 IP。本机测试保持默认 `127.0.0.1`，端口与服务端一致，昵称为 1–16 个字符，然后点击「连接」。
4. 连接成功后，在输入框写消息并点击「发送」，或按 Enter。英文和中文都可以。「在线列表」向服务器查询当前昵称。「断开」会先发送退出通知再关闭连接。
5. 「连续发送」不使用输入框里的文字。它按数字框中的条数发出 `burst 1/n`、`burst 2/n` 等测试消息，转发给其他在线用户，发送者自己的列表不回显。接收方状态栏显示序号是否连续，以及「连续发送收到 i/n」。

服务端点击「停止」或关闭窗口时结束监听并断开在线客户。

## 环回测试

`bin\loopback\chat_loopback.exe` 在本机端口 `19091` 上启动同一套服务端和客户端核心，不打开窗口。它以张三、Alice、李四三个昵称收发英文和中文，并连续发送 100 条。控制台最后一行是 `PASS` 时，表示序号没有空洞，且 100 条测试消息全部到达。
