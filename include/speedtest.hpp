#pragma once

#include <cstddef>
#include <chrono>

constexpr size_t BUFFER_SIZE = 128 * 1024;
constexpr std::chrono::seconds REPORT_INTERVAL{1};
constexpr std::chrono::milliseconds PROBE_INTERVAL{100};
