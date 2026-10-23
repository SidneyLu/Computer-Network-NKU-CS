#pragma once

#include "netutil.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace chat {

struct ServerEvents {
    std::function<void(const std::wstring& line)> on_log;
    std::function<void(const std::vector<std::wstring>& names)> on_users;
};

class ChatServer {
public:
    explicit ChatServer(ServerEvents events);
    ~ChatServer();

    ChatServer(const ChatServer&) = delete;
    ChatServer& operator=(const ChatServer&) = delete;

    bool start(uint16_t port, std::wstring& error);
    void stop();
    bool running() const;

private:
    struct Client;

    void accept_loop();
    void client_loop(const std::shared_ptr<Client>& client);
    void finish_client(const std::shared_ptr<Client>& client);
    bool send_to(const std::shared_ptr<Client>& client, MsgType type, const std::string& payload);
    void broadcast(const std::shared_ptr<Client>& except, MsgType type, const std::string& payload);
    void publish_users();
    void log(const std::wstring& line);
    std::string user_list_payload();

    ServerEvents events_;
    SOCKET listen_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::thread accept_thread_;
    std::mutex clients_mu_;
    std::vector<std::shared_ptr<Client>> clients_;
    std::mutex wait_mu_;
    std::condition_variable wait_cv_;
    std::atomic<int> active_clients_{0};
};

}  // namespace chat
