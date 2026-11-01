#pragma once

#include "speedtest.hpp"
#include "socket.hpp"
#include <cstdint>
#include <expected>
#include <span>

std::expected<void, std::string> send_all(Socket &sock, std::span<const char> data);
std::expected<void, std::string> send_bytes(Socket &sock, std::uint64_t total);
