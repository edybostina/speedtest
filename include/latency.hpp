#pragma once

#include "socket.hpp"
#include "speedtest.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

std::expected<std::uint64_t, std::string> echo_probes(Socket &sock);
std::expected<std::vector<double>, std::string> measure_latency(Socket &sock,
                                                                std::chrono::seconds duration);
