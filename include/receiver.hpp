#pragma once

#include "socket.hpp"
#include "speedtest.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>

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
receive(std::span<Socket> sockets,
        const std::function<void(const IntervalReport &)> &on_interval = {});

std::expected<ReceiveResult, std::string>
receive(Socket &socket, const std::function<void(const IntervalReport &)> &on_interval = {});
