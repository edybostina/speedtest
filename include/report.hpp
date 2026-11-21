#pragma once

#include "receiver.hpp"
#include "socket.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>

struct StreamReport {
    std::uint64_t bytes;
    std::chrono::microseconds elapsed;
};

constexpr std::size_t INTERVAL_SIZE = 25;
constexpr std::size_t REPORT_SIZE = 17;

constexpr char REPORT_TAG = 'R';
constexpr char INTERVAL_TAG = 'I';

std::expected<void, std::string> send_report(Socket &sock, const StreamReport &report);
std::expected<void, std::string> send_interval(Socket &sock, const IntervalReport &interval);
std::expected<StreamReport, std::string>
read_report(Socket &sock, const std::function<void(const IntervalReport &)> &on_interval = {});
