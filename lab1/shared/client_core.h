#pragma once

#include "netutil.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace chat {

struct ClientStats {
    uint32_t sent = 0;
    uint32_t frames = 0;
    uint32_t chats = 0;
    uint32_t gaps = 0;
    uint32_t burst_got = 0;
    uint32_t burst_expect = 0;
    bool seq_ok = true;
};

struct ClientEvents {
    std::function<void(const std::wstring& line)> on_line;
    std::function<void(const std::wstring& status)> on_status;
};

class ChatClient {
public:
    explicit ChatClient(ClientEvents events);
    ~ChatClient();

    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    bool connect_to(const std::wstring& host, uint16_t port, const std::wstring& nickname, std::wstring& error);
    bool send_text(const std::wstring& text, std::wstring& error);
    bool request_list(std::wstring& error);
    bool start_burst(int count, std::wstring& error);
    void disconnect();
    bool connected() const;
    ClientStats stats() const;

private:
    bool send_message(MsgType type, const std::string& payload, bool count_as_chat);
    void recv_loop();
    void note_seq(uint32_t seq);
    void publish_status();
    std::wstring status_text() const;

    ClientEvents events_;
    SOCKET sock_ = INVALID_SOCKET;
    std::atomic<bool> connected_{false};
    std::thread recv_thread_;
    std::thread burst_thread_;
    mutable std::mutex send_mu_;
    mutable std::mutex stats_mu_;
    uint32_t send_seq_ = 0;
    uint32_t expected_seq_ = 1;
    ClientStats stats_{};
    std::wstring nickname_;
    std::vector<char> burst_seen_;
};

}  // namespace chat
