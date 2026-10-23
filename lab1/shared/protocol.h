#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// 聊天帧：固定 10 字节头 + UTF-8 负载。多字节整数使用网络字节序。
//  0  魔数 0x43
//  1  魔数 0x48
//  2  版本
//  3  类型
//  4  序号 uint32，每条连接的每个方向从 1 递增
//  8  负载长度 uint16
// 10  负载

namespace chat {

constexpr uint16_t kDefaultPort = 9000;
constexpr uint8_t kVersion = 1;
constexpr std::size_t kHeaderSize = 10;
constexpr uint16_t kMaxPayload = 1024;
constexpr uint8_t kMagic0 = 0x43;
constexpr uint8_t kMagic1 = 0x48;
constexpr int kMaxNicknameChars = 16;

enum class MsgType : uint8_t {
    Hello = 1,
    Welcome = 2,
    Chat = 3,
    Notice = 4,
    List = 5,
    Quit = 6,
    Err = 7,
};

struct Frame {
    MsgType type = MsgType::Chat;
    uint32_t seq = 0;
    std::string payload;
};

enum class RecvStatus {
    Ok,
    Closed,
    ProtocolError,
};

struct Utf8Check {
    bool valid = false;
    std::size_t codepoints = 0;
    bool has_control = false;
};

Utf8Check inspect_utf8(const std::string& bytes);
bool valid_nickname(const std::string& utf8);

}  // namespace chat
