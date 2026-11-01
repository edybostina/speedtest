#pragma once

#include "socket.hpp"
#include "speedtest.hpp"

#include <cstdint>
#include <chrono>
#include <expected>
#include <string>

struct ReceiveResult {
    std::uint64_t bytes;
    std::chrono::steady_clock::duration elapsed;
};

std::expected<ReceiveResult, std::string> receive(Socket &socket);
