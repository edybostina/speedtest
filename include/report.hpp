#pragma once

#include "socket.hpp"
#include <chrono>
#include <cstdint>
#include <expected>

struct StreamReport {
    std::uint64_t bytes;
    std::chrono::microseconds elapsed;
};

constexpr std::size_t REPORT_SIZE = 16;

std::expected<void, std::string> send_report(Socket &sock, const StreamReport &report);
std::expected<StreamReport, std::string> read_report(Socket &sock);
