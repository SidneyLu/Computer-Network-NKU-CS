#include "server_core.h"

#include <algorithm>
#include <ctime>
#include <sstream>

namespace chat {
namespace {

std::wstring clock_text() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t buffer[16]{};
    wcsftime(buffer, 16, L"%H:%M:%S", &local);
    return buffer;
}

}  // namespace

struct ChatServer::Client {
    SOCKET sock = INVALID_SOCKET;
    std::string name;
    bool introduced = false;
    std::mutex send_mu;
    uint32_t send_seq = 0;
    std::atomic<bool> alive{true};
};

ChatServer::ChatServer(ServerEvents events) : events_(std::move(events)) {
    startup_wsa();
}

ChatServer::~ChatServer() {
    stop();
}

bool ChatServer::running() const {
    return running_.load();
}

bool ChatServer::start(uint16_t port, std::wstring& error) {
    if (running_.load()) {
        error = L"服务器已经在监听。";
        return false;
    }
    stopping_.store(false);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        error = L"创建套接字失败。";
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(listener);
        error = L"绑定端口失败。请换一个端口，或确认没有其他程序占用它。";
        return false;
    }
    if (listen(listener, 16) == SOCKET_ERROR) {
        closesocket(listener);
        error = L"开始监听失败。";
        return false;
    }

    listen_ = listener;
    running_.store(true);
    accept_thread_ = std::thread([this] { accept_loop(); });
    log(L"开始监听端口 " + std::to_wstring(port));
    publish_users();
    return true;
}

void ChatServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    stopping_.store(true);
    SOCKET listener = listen_;
    listen_ = INVALID_SOCKET;
    if (listener != INVALID_SOCKET) {
        closesocket(listener);
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    std::vector<std::shared_ptr<Client>> snapshot;
    {
        std::lock_guard<std::mutex> lock(clients_mu_);
        snapshot = clients_;
    }
    for (const auto& client : snapshot) {
        std::lock_guard<std::mutex> send_lock(client->send_mu);
        if (client->sock != INVALID_SOCKET) {
            shutdown(client->sock, SD_BOTH);
        }
    }
    {
        std::unique_lock<std::mutex> lock(wait_mu_);
        wait_cv_.wait(lock, [this] { return active_clients_.load() == 0; });
    }
    {
        std::lock_guard<std::mutex> lock(clients_mu_);
        clients_.clear();
    }
    publish_users();
    log(L"已停止监听");
    stopping_.store(false);
}

void ChatServer::accept_loop() {
    while (running_.load()) {
        sockaddr_in peer{};
        int peer_size = sizeof(peer);
        const SOCKET incoming = accept(listen_, reinterpret_cast<sockaddr*>(&peer), &peer_size);
        if (incoming == INVALID_SOCKET) {
            break;
        }
        auto client = std::make_shared<Client>();
        client->sock = incoming;
        {
            std::lock_guard<std::mutex> lock(clients_mu_);
            clients_.push_back(client);
        }
        active_clients_.fetch_add(1);
        std::thread([this, client] { client_loop(client); }).detach();

        char address_text[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &peer.sin_addr, address_text, sizeof(address_text));
        log(L"新连接来自 " + utf8_to_wide(address_text));
    }
}

void ChatServer::client_loop(const std::shared_ptr<Client>& client) {
    while (client->alive.load() && running_.load()) {
        Frame frame;
        const RecvStatus status = recv_frame(client->sock, frame);
        if (status == RecvStatus::Closed) {
            break;
        }
        if (status == RecvStatus::ProtocolError) {
            log(L"收到非法帧，已关闭该连接");
            send_to(client, MsgType::Err, wide_to_utf8(L"协议错误"));
            break;
        }

        if (!client->introduced && frame.type != MsgType::Hello && frame.type != MsgType::Quit) {
            send_to(client, MsgType::Err, wide_to_utf8(L"请先发送昵称"));
            break;
        }

        if (frame.type == MsgType::Hello) {
            if (client->introduced) {
                send_to(client, MsgType::Err, wide_to_utf8(L"昵称已经登记"));
                continue;
            }
            if (!valid_nickname(frame.payload)) {
                send_to(client, MsgType::Err, wide_to_utf8(L"昵称需要是 1 到 16 个字符，且不能包含控制字符"));
                break;
            }
            bool duplicated = false;
            {
                std::lock_guard<std::mutex> lock(clients_mu_);
                for (const auto& other : clients_) {
                    if (other->introduced && other->name == frame.payload) {
                        duplicated = true;
                        break;
                    }
                }
                if (!duplicated) {
                    client->name = frame.payload;
                    client->introduced = true;
                }
            }
            if (duplicated) {
                send_to(client, MsgType::Err, wide_to_utf8(L"这个昵称已经在线"));
                break;
            }
            send_to(client, MsgType::Welcome, client->name);
            publish_users();
            const std::wstring name = utf8_to_wide(client->name);
            log(name + L" 加入聊天");
            broadcast(client, MsgType::Notice, wide_to_utf8(name + L" 加入了聊天"));
            continue;
        }

        if (frame.type == MsgType::Quit) {
            break;
        }

        if (frame.type == MsgType::List) {
            send_to(client, MsgType::Notice, user_list_payload());
            continue;
        }

        if (frame.type == MsgType::Chat) {
            const Utf8Check check = inspect_utf8(frame.payload);
            if (!check.valid || check.has_control || frame.payload.empty()) {
                send_to(client, MsgType::Err, wide_to_utf8(L"消息需要是不含控制字符的 UTF-8 文本"));
                continue;
            }
            const std::string outgoing = client->name + ": " + frame.payload;
            if (outgoing.size() > kMaxPayload) {
                send_to(client, MsgType::Err, wide_to_utf8(L"消息太长"));
                continue;
            }
            int burst_index = 0;
            int burst_total = 0;
            const bool burst = parse_burst_tag(frame.payload, burst_index, burst_total);
            if (!burst || burst_index == 1 || burst_index == burst_total || burst_index % 20 == 0) {
                log(L"转发 " + utf8_to_wide(outgoing));
            }
            broadcast(client, MsgType::Chat, outgoing);
        }
    }
    finish_client(client);
}

void ChatServer::finish_client(const std::shared_ptr<Client>& client) {
    if (!client->alive.exchange(false)) {
        return;
    }

    const bool introduced = client->introduced;
    const std::string name = client->name;
    {
        std::lock_guard<std::mutex> send_lock(client->send_mu);
        if (client->sock != INVALID_SOCKET) {
            closesocket(client->sock);
            client->sock = INVALID_SOCKET;
        }
    }
    {
        std::lock_guard<std::mutex> lock(clients_mu_);
        clients_.erase(
            std::remove_if(
                clients_.begin(),
                clients_.end(),
                [&](const std::shared_ptr<Client>& item) { return item == client; }),
            clients_.end());
    }
    if (introduced && !stopping_.load()) {
        const std::wstring wide_name = utf8_to_wide(name);
        log(wide_name + L" 离开聊天");
        broadcast(nullptr, MsgType::Notice, wide_to_utf8(wide_name + L" 离开了聊天"));
        publish_users();
    }
    if (active_clients_.fetch_sub(1) == 1) {
        std::lock_guard<std::mutex> lock(wait_mu_);
        wait_cv_.notify_all();
    }
}

bool ChatServer::send_to(const std::shared_ptr<Client>& client, MsgType type, const std::string& payload) {
    if (!client || !client->alive.load()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(client->send_mu);
    if (client->sock == INVALID_SOCKET) {
        return false;
    }
    Frame frame;
    frame.type = type;
    frame.seq = ++client->send_seq;
    frame.payload = payload;
    return send_frame(client->sock, frame);
}

void ChatServer::broadcast(const std::shared_ptr<Client>& except, MsgType type, const std::string& payload) {
    std::vector<std::shared_ptr<Client>> snapshot;
    {
        std::lock_guard<std::mutex> lock(clients_mu_);
        snapshot = clients_;
    }
    for (const auto& client : snapshot) {
        if (client == except || !client->introduced) {
            continue;
        }
        send_to(client, type, payload);
    }
}

std::string ChatServer::user_list_payload() {
    std::ostringstream stream;
    stream << "在线用户：";
    std::lock_guard<std::mutex> lock(clients_mu_);
    bool first = true;
    for (const auto& client : clients_) {
        if (!client->introduced) {
            continue;
        }
        if (!first) {
            stream << "、";
        }
        first = false;
        stream << client->name;
    }
    if (first) {
        stream << "（无）";
    }
    return stream.str();
}

void ChatServer::publish_users() {
    if (!events_.on_users) {
        return;
    }
    std::vector<std::wstring> names;
    {
        std::lock_guard<std::mutex> lock(clients_mu_);
        for (const auto& client : clients_) {
            if (client->introduced) {
                names.push_back(utf8_to_wide(client->name));
            }
        }
    }
    events_.on_users(names);
}

void ChatServer::log(const std::wstring& line) {
    if (events_.on_log) {
        events_.on_log(clock_text() + L"  " + line);
    }
}

}  // namespace chat
