#include "netutil.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

namespace chat {
namespace {

std::once_flag g_wsa_once;

void write_u32(uint8_t* dest, uint32_t value) {
    const uint32_t net = htonl(value);
    std::memcpy(dest, &net, sizeof(net));
}

void write_u16(uint8_t* dest, uint16_t value) {
    const uint16_t net = htons(value);
    std::memcpy(dest, &net, sizeof(net));
}

uint32_t read_u32(const uint8_t* src) {
    uint32_t net = 0;
    std::memcpy(&net, src, sizeof(net));
    return ntohl(net);
}

uint16_t read_u16(const uint8_t* src) {
    uint16_t net = 0;
    std::memcpy(&net, src, sizeof(net));
    return ntohs(net);
}

}  // namespace

Utf8Check inspect_utf8(const std::string& bytes) {
    Utf8Check result;
    result.valid = true;
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::size_t size = bytes.size();
    std::size_t i = 0;
    while (i < size) {
        const unsigned char lead = data[i];
        std::size_t need = 0;
        uint32_t codepoint = 0;
        if (lead <= 0x7F) {
            need = 1;
            codepoint = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            need = 2;
            codepoint = lead & 0x1F;
        } else if ((lead & 0xF0) == 0xE0) {
            need = 3;
            codepoint = lead & 0x0F;
        } else if ((lead & 0xF8) == 0xF0) {
            need = 4;
            codepoint = lead & 0x07;
        } else {
            result.valid = false;
            return result;
        }
        if (i + need > size) {
            result.valid = false;
            return result;
        }
        for (std::size_t extra = 1; extra < need; ++extra) {
            const unsigned char cont = data[i + extra];
            if ((cont & 0xC0) != 0x80) {
                result.valid = false;
                return result;
            }
            codepoint = (codepoint << 6) | (cont & 0x3F);
        }
        const bool overlong =
            (need == 2 && codepoint < 0x80) ||
            (need == 3 && codepoint < 0x800) ||
            (need == 4 && codepoint < 0x10000);
        if (overlong || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            result.valid = false;
            return result;
        }
        if (codepoint < 0x20 || codepoint == 0x7F) {
            result.has_control = true;
        }
        ++result.codepoints;
        i += need;
    }
    return result;
}

bool valid_nickname(const std::string& utf8) {
    const Utf8Check check = inspect_utf8(utf8);
    return check.valid && !check.has_control && check.codepoints >= 1 &&
           check.codepoints <= static_cast<std::size_t>(kMaxNicknameChars);
}

void startup_wsa() {
    std::call_once(g_wsa_once, [] {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

bool send_all(SOCKET socket, const char* data, int size) {
    int sent_total = 0;
    while (sent_total < size) {
        const int sent = send(socket, data + sent_total, size - sent_total, 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            return false;
        }
        sent_total += sent;
    }
    return true;
}

bool recv_exact(SOCKET socket, char* data, int size) {
    int got_total = 0;
    while (got_total < size) {
        const int got = recv(socket, data + got_total, size - got_total, 0);
        if (got == SOCKET_ERROR || got == 0) {
            return false;
        }
        got_total += got;
    }
    return true;
}

bool send_frame(SOCKET socket, const Frame& frame) {
    if (frame.payload.size() > kMaxPayload) {
        return false;
    }
    uint8_t header[kHeaderSize]{};
    header[0] = kMagic0;
    header[1] = kMagic1;
    header[2] = kVersion;
    header[3] = static_cast<uint8_t>(frame.type);
    write_u32(header + 4, frame.seq);
    write_u16(header + 8, static_cast<uint16_t>(frame.payload.size()));
    if (!send_all(socket, reinterpret_cast<const char*>(header), static_cast<int>(kHeaderSize))) {
        return false;
    }
    if (frame.payload.empty()) {
        return true;
    }
    return send_all(socket, frame.payload.data(), static_cast<int>(frame.payload.size()));
}

RecvStatus recv_frame(SOCKET socket, Frame& frame) {
    uint8_t header[kHeaderSize]{};
    if (!recv_exact(socket, reinterpret_cast<char*>(header), static_cast<int>(kHeaderSize))) {
        return RecvStatus::Closed;
    }
    if (header[0] != kMagic0 || header[1] != kMagic1 || header[2] != kVersion) {
        return RecvStatus::ProtocolError;
    }
    const auto type = static_cast<MsgType>(header[3]);
    switch (type) {
    case MsgType::Hello:
    case MsgType::Welcome:
    case MsgType::Chat:
    case MsgType::Notice:
    case MsgType::List:
    case MsgType::Quit:
    case MsgType::Err:
        break;
    default:
        return RecvStatus::ProtocolError;
    }
    const uint16_t length = read_u16(header + 8);
    if (length > kMaxPayload) {
        return RecvStatus::ProtocolError;
    }
    std::string payload(length, '\0');
    if (length > 0 &&
        !recv_exact(socket, payload.data(), static_cast<int>(length))) {
        return RecvStatus::Closed;
    }
    frame.type = type;
    frame.seq = read_u32(header + 4);
    frame.payload = std::move(payload);
    return RecvStatus::Ok;
}

std::wstring utf8_to_wide(const std::string& bytes) {
    if (bytes.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (size <= 0) {
        return L"[无法解码的 UTF-8]";
    }
    std::wstring text(size, L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), size);
    return text;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string bytes(size, '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        bytes.data(),
        size,
        nullptr,
        nullptr);
    return bytes;
}

bool parse_burst_tag(const std::string& utf8, int& index, int& total) {
    const std::string tag = "burst ";
    const auto pos = utf8.rfind(tag);
    if (pos == std::string::npos) {
        return false;
    }
    const char* cursor = utf8.c_str() + pos + tag.size();
    char* end = nullptr;
    const long parsed_index = std::strtol(cursor, &end, 10);
    if (end == cursor || *end != '/') {
        return false;
    }
    cursor = end + 1;
    const long parsed_total = std::strtol(cursor, &end, 10);
    if (end == cursor || parsed_index <= 0 || parsed_total <= 0 || parsed_index > parsed_total) {
        return false;
    }
    index = static_cast<int>(parsed_index);
    total = static_cast<int>(parsed_total);
    return true;
}

}  // namespace chat
