#include "client_core.h"
#include "server_core.h"

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct Transcript {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<std::wstring> lines;
    std::wstring status;
};

void print_wide(const std::wstring& text) {
    const std::string utf8 = chat::wide_to_utf8(text);
    std::cout << utf8 << "\n";
}

bool wait_until(Transcript& box, const std::wstring& needle, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    std::unique_lock<std::mutex> lock(box.mu);
    while (std::chrono::steady_clock::now() < deadline) {
        if (box.status.find(needle) != std::wstring::npos) {
            return true;
        }
        for (const auto& line : box.lines) {
            if (line.find(needle) != std::wstring::npos) {
                return true;
            }
        }
        box.cv.wait_until(lock, deadline);
    }
    return false;
}

chat::ClientEvents watch(Transcript& box) {
    return chat::ClientEvents{
        [&box](const std::wstring& line) {
            std::lock_guard<std::mutex> lock(box.mu);
            box.lines.push_back(line);
            box.cv.notify_all();
        },
        [&box](const std::wstring& status) {
            std::lock_guard<std::mutex> lock(box.mu);
            box.status = status;
            box.cv.notify_all();
        },
    };
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    const uint16_t port = 19091;
    std::wstring error;
    chat::ChatServer server({});
    if (!server.start(port, error)) {
        print_wide(error);
        return 1;
    }

    Transcript zhang;
    Transcript alice;
    Transcript li;
    chat::ChatClient zhang_client(watch(zhang));
    chat::ChatClient alice_client(watch(alice));
    chat::ChatClient li_client(watch(li));

    auto fail = [&](const wchar_t* reason) {
        print_wide(reason);
        print_wide(L"张三状态: " + zhang.status);
        print_wide(L"Alice状态: " + alice.status);
        print_wide(L"李四状态: " + li.status);
        zhang_client.disconnect();
        alice_client.disconnect();
        li_client.disconnect();
        server.stop();
        return 1;
    };

    if (!zhang_client.connect_to(L"127.0.0.1", port, L"张三", error) ||
        !wait_until(zhang, L"已加入聊天", std::chrono::seconds(3))) {
        return fail(L"张三连接失败");
    }
    if (!alice_client.connect_to(L"127.0.0.1", port, L"Alice", error) ||
        !wait_until(zhang, L"Alice 加入了聊天", std::chrono::seconds(3))) {
        return fail(L"Alice 连接失败");
    }
    if (!zhang_client.send_text(L"Hello", error) || !zhang_client.send_text(L"你好，晚上见", error)) {
        return fail(L"张三发送失败");
    }
    if (!wait_until(alice, L"你好，晚上见", std::chrono::seconds(3))) {
        return fail(L"Alice 没有收到中文");
    }
    if (!alice_client.send_text(L"Good evening 晚上好", error) ||
        !wait_until(zhang, L"Good evening 晚上好", std::chrono::seconds(3))) {
        return fail(L"张三没有收到英文和中文");
    }
    if (!zhang_client.start_burst(100, error) ||
        !wait_until(alice, L"连续发送收到 100/100", std::chrono::seconds(8))) {
        return fail(L"连续发送没有到齐");
    }
    const chat::ClientStats alice_stats = alice_client.stats();
    if (!alice_stats.seq_ok || alice_stats.gaps != 0 || alice_stats.burst_got != 100) {
        return fail(L"序号不连续或条数不符");
    }
    if (!li_client.connect_to(L"127.0.0.1", port, L"李四", error) ||
        !li_client.request_list(error) ||
        !wait_until(li, L"张三", std::chrono::seconds(3)) ||
        !wait_until(li, L"Alice", std::chrono::seconds(3))) {
        return fail(L"李四没有看到在线列表");
    }
    zhang_client.disconnect();
    if (!wait_until(alice, L"张三 离开了聊天", std::chrono::seconds(3))) {
        return fail(L"离开通知没有送达");
    }
    alice_client.disconnect();
    li_client.disconnect();
    server.stop();

    print_wide(L"英文消息: Hello");
    print_wide(L"中文消息: 你好，晚上见 / Good evening 晚上好");
    print_wide(L"在线用户包含张三、Alice、李四");
    print_wide(alice.status);
    print_wide(L"序号空洞: " + std::to_wstring(alice_stats.gaps));
    print_wide(L"PASS");
    return 0;
}
