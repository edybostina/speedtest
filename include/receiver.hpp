#pragma once

#include "socket.hpp"
#include "speedtest.hpp"

#include <cstdint>
#include <chrono>
#include <expected>
#include <string>
#include <functional>

struct ReceiveResult {
    std::uint64_t bytes;
    std::chrono::steady_clock::duration elapsed;
};

struct IntervalReport {
    std::chrono::duration<double> start;
    std::chrono::duration<double> end;
    std::uint64_t bytes;
};

std::expected<ReceiveResult, std::string>
receive(Socket &socket, const std::function<void(const IntervalReport &)> &on_interval = {});
