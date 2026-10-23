#pragma once

#include "protocol.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>

namespace chat {

void startup_wsa();

bool send_all(SOCKET socket, const char* data, int size);
bool recv_exact(SOCKET socket, char* data, int size);

bool send_frame(SOCKET socket, const Frame& frame);
RecvStatus recv_frame(SOCKET socket, Frame& frame);

std::wstring utf8_to_wide(const std::string& bytes);
std::string wide_to_utf8(const std::wstring& text);

bool parse_burst_tag(const std::string& utf8, int& index, int& total);

}  // namespace chat
