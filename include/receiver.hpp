#pragma once

#include "socket.hpp"
#include "speedtest.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <vector>

struct ReceiveResult {
    std::uint64_t bytes;
    std::chrono::steady_clock::duration elapsed;
    std::vector<std::uint64_t> stream_bytes;
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

std::expected<void, std::string> recv_all(Socket &sock, std::span<char> data);
