#pragma once

#include "speedtest.hpp"
#include "socket.hpp"
#include <cstdint>
#include <expected>
#include <span>
#include <chrono>

struct SendResult {
    std::uint64_t bytes;
    std::chrono::steady_clock::duration last_send;
    std::chrono::steady_clock::duration peer_done;
};

std::expected<void, std::string> send_all(Socket &sock, std::span<const char> data);
std::expected<SendResult, std::string> send_bytes(Socket &sock, std::uint64_t total);
