#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// 以太网帧按字面偏移解析，不跳过 VLAN。
//  0  目的 MAC，6 字节
//  6  源 MAC，6 字节
// 12  类型字段，2 字节，网络字节序
// 14  类型字段之后的第一个字节

namespace cap {

struct EthernetView {
    bool ok = false;
    uint8_t dst[6]{};
    uint8_t src[6]{};
    uint16_t type = 0;
    uint8_t after_type = 0;
};

struct FrameLine {
    uint32_t index = 0;
    uint32_t length = 0;
    std::wstring dst;
    std::wstring src;
    std::wstring type;
    std::wstring next;
};

EthernetView parse_ethernet(const uint8_t* data, std::size_t length);
std::wstring format_mac(const uint8_t mac[6]);
std::wstring format_type(uint16_t type);
std::wstring format_byte(uint8_t value);

}
