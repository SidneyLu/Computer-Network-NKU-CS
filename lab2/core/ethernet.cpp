#include "ethernet.h"

#include <cstdio>

namespace cap {

EthernetView parse_ethernet(const uint8_t* data, std::size_t length)
{
    EthernetView view;
    if (data == nullptr || length < 15)
    {
        return view;
    }
    for (int i = 0; i < 6; ++i)
    {
        view.dst[i] = data[i];
        view.src[i] = data[6 + i];
    }
    view.type = static_cast<uint16_t>((static_cast<uint16_t>(data[12]) << 8) | data[13]);
    view.after_type = data[14];
    view.ok = true;
    return view;
}

std::wstring format_mac(const uint8_t mac[6])
{
    wchar_t text[18];
    swprintf_s(text, L"%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return text;
}

std::wstring format_type(uint16_t type)
{
    const wchar_t* name = nullptr;
    switch (type)
    {
    case 0x0800:
        name = L"IPv4";
        break;
    case 0x0806:
        name = L"ARP";
        break;
    case 0x86DD:
        name = L"IPv6";
        break;
    case 0x8100:
        name = L"VLAN";
        break;
    default:
        break;
    }
    wchar_t text[32];
    if (name != nullptr)
    {
        swprintf_s(text, L"0x%04X (%s)", type, name);
    }
    else
    {
        swprintf_s(text, L"0x%04X", type);
    }
    return text;
}

std::wstring format_byte(uint8_t value)
{
    wchar_t text[8];
    swprintf_s(text, L"0x%02X", value);
    return text;
}

}
