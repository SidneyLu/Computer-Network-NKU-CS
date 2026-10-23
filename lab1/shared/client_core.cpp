#include "client_core.h"

namespace chat {

ChatClient::ChatClient(ClientEvents events) : events_(std::move(events)) {
    startup_wsa();
}

ChatClient::~ChatClient() {
    disconnect();
}

bool ChatClient::connected() const {
    return connected_.load();
}

ClientStats ChatClient::stats() const {
    std::lock_guard<std::mutex> lock(stats_mu_);
    return stats_;
}

bool ChatClient::connect_to(
    const std::wstring& host,
    uint16_t port,
    const std::wstring& nickname,
    std::wstring& error) {
    if (connected_.load()) {
        error = L"已经连接。";
        return false;
    }
    const std::string nick_utf8 = wide_to_utf8(nickname);
    if (!valid_nickname(nick_utf8)) {
        error = L"昵称需要是 1 到 16 个字符，且不能包含控制字符。";
        return false;
    }
    const std::string host_utf8 = wide_to_utf8(host);
    if (host_utf8.empty()) {
        error = L"请填写服务器地址。";
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    const std::string port_text = std::to_string(port);
    if (getaddrinfo(host_utf8.c_str(), port_text.c_str(), &hints, &result) != 0) {
        error = L"无法解析服务器地址。";
        return false;
    }

    SOCKET sock = INVALID_SOCKET;
    for (addrinfo* item = result; item != nullptr; item = item->ai_next) {
        sock = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (sock == INVALID_SOCKET) {
            continue;
        }
        if (connect(sock, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0) {
            break;
        }
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    if (sock == INVALID_SOCKET) {
        error = L"连接服务器失败。";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mu_);
        stats_ = {};
        stats_.seq_ok = true;
        expected_seq_ = 1;
        send_seq_ = 0;
        burst_seen_.clear();
        nickname_ = nickname;
    }
    sock_ = sock;
    connected_.store(true);
    if (!send_message(MsgType::Hello, nick_utf8, false)) {
        disconnect();
        error = L"发送昵称失败。";
        return false;
    }
    recv_thread_ = std::thread([this] { recv_loop(); });
    publish_status();
    return true;
}

bool ChatClient::send_text(const std::wstring& text, std::wstring& error) {
    const std::string utf8 = wide_to_utf8(text);
    const Utf8Check check = inspect_utf8(utf8);
    if (!connected_.load()) {
        error = L"还没有连接服务器。";
        return false;
    }
    if (!check.valid || check.has_control || utf8.empty()) {
        error = L"消息需要是不含控制字符的文本。";
        return false;
    }
    if (utf8.size() > kMaxPayload) {
        error = L"消息太长。";
        return false;
    }
    if (!send_message(MsgType::Chat, utf8, true)) {
        error = L"发送失败，连接可能已经断开。";
        return false;
    }
    if (events_.on_line) {
        events_.on_line(L"我: " + text);
    }
    publish_status();
    return true;
}

bool ChatClient::request_list(std::wstring& error) {
    if (!connected_.load()) {
        error = L"还没有连接服务器。";
        return false;
    }
    if (!send_message(MsgType::List, {}, false)) {
        error = L"请求在线列表失败。";
        return false;
    }
    return true;
}

bool ChatClient::start_burst(int count, std::wstring& error) {
    if (!connected_.load()) {
        error = L"还没有连接服务器。";
        return false;
    }
    if (count < 1 || count > 500) {
        error = L"连续发送的条数需要在 1 到 500 之间。";
        return false;
    }
    if (burst_thread_.joinable()) {
        burst_thread_.join();
    }
    burst_thread_ = std::thread([this, count] {
        for (int index = 1; index <= count && connected_.load(); ++index) {
            const std::string payload = "burst " + std::to_string(index) + "/" + std::to_string(count);
            if (!send_message(MsgType::Chat, payload, true)) {
                break;
            }
        }
        publish_status();
    });
    return true;
}

void ChatClient::disconnect() {
    connected_.store(false);
    SOCKET closing = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(send_mu_);
        closing = sock_;
        sock_ = INVALID_SOCKET;
        if (closing != INVALID_SOCKET) {
            Frame frame;
            frame.type = MsgType::Quit;
            frame.seq = ++send_seq_;
            send_frame(closing, frame);
            shutdown(closing, SD_BOTH);
        }
    }
    if (burst_thread_.joinable() && burst_thread_.get_id() != std::this_thread::get_id()) {
        burst_thread_.join();
    }
    if (recv_thread_.joinable() && recv_thread_.get_id() != std::this_thread::get_id()) {
        recv_thread_.join();
    }
    if (closing != INVALID_SOCKET) {
        closesocket(closing);
    }
    publish_status();
}

bool ChatClient::send_message(MsgType type, const std::string& payload, bool count_as_chat) {
    std::lock_guard<std::mutex> lock(send_mu_);
    if (sock_ == INVALID_SOCKET) {
        return false;
    }
    Frame frame;
    frame.type = type;
    frame.seq = ++send_seq_;
    frame.payload = payload;
    if (!send_frame(sock_, frame)) {
        return false;
    }
    if (count_as_chat) {
        std::lock_guard<std::mutex> stats_lock(stats_mu_);
        ++stats_.sent;
    }
    return true;
}

void ChatClient::recv_loop() {
    while (connected_.load()) {
        SOCKET current = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(send_mu_);
            current = sock_;
        }
        if (current == INVALID_SOCKET) {
            break;
        }
        Frame frame;
        const RecvStatus status = recv_frame(current, frame);
        if (status != RecvStatus::Ok) {
            if (events_.on_line && status == RecvStatus::ProtocolError) {
                events_.on_line(L"系统: 收到无法识别的数据，连接已关闭");
            }
            break;
        }
        note_seq(frame.seq);
        if (frame.type == MsgType::Welcome) {
            if (events_.on_line) {
                events_.on_line(L"系统: 已加入聊天，昵称 " + utf8_to_wide(frame.payload));
            }
        } else if (frame.type == MsgType::Notice) {
            if (events_.on_line) {
                events_.on_line(L"系统: " + utf8_to_wide(frame.payload));
            }
        } else if (frame.type == MsgType::Err) {
            if (events_.on_line) {
                events_.on_line(L"系统: " + utf8_to_wide(frame.payload));
            }
            break;
        } else if (frame.type == MsgType::Chat) {
            int index = 0;
            int total = 0;
            {
                std::lock_guard<std::mutex> lock(stats_mu_);
                ++stats_.chats;
                if (parse_burst_tag(frame.payload, index, total)) {
                    if (stats_.burst_expect != static_cast<uint32_t>(total)) {
                        stats_.burst_expect = static_cast<uint32_t>(total);
                        stats_.burst_got = 0;
                        burst_seen_.assign(static_cast<std::size_t>(total), 0);
                    }
                    if (index >= 1 && index <= total && burst_seen_[static_cast<std::size_t>(index - 1)] == 0) {
                        burst_seen_[static_cast<std::size_t>(index - 1)] = 1;
                        ++stats_.burst_got;
                    }
                }
            }
            if (events_.on_line) {
                events_.on_line(utf8_to_wide(frame.payload));
            }
        }
        publish_status();
    }
    connected_.store(false);
    publish_status();
}

void ChatClient::note_seq(uint32_t seq) {
    std::lock_guard<std::mutex> lock(stats_mu_);
    ++stats_.frames;
    if (seq != expected_seq_) {
        stats_.seq_ok = false;
        if (seq > expected_seq_) {
            stats_.gaps += seq - expected_seq_;
        } else {
            ++stats_.gaps;
        }
        expected_seq_ = seq + 1;
    } else {
        ++expected_seq_;
    }
}

std::wstring ChatClient::status_text() const {
    std::lock_guard<std::mutex> lock(stats_mu_);
    std::wstring text = connected_.load() ? (L"已连接  " + nickname_) : L"未连接";
    text += L"  |  已发送 " + std::to_wstring(stats_.sent);
    text += L"  |  已收到帧 " + std::to_wstring(stats_.frames);
    text += L"（聊天 " + std::to_wstring(stats_.chats) + L"）";
    if (stats_.seq_ok) {
        text += L"  |  序号连续";
    } else {
        text += L"  |  序号空洞 " + std::to_wstring(stats_.gaps);
    }
    if (stats_.burst_expect > 0) {
        text += L"  |  连续发送收到 " + std::to_wstring(stats_.burst_got) + L"/" +
                std::to_wstring(stats_.burst_expect);
    }
    return text;
}

void ChatClient::publish_status() {
    if (events_.on_status) {
        events_.on_status(status_text());
    }
}

}  // namespace chat
