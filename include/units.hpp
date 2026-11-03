#pragma once

#include <chrono>
#include <cstdint>
#include <string>

double megabits_per_second(std::uint64_t bytes, std::chrono::duration<double> seconds);
std::string format_transfer(std::uint64_t bytes, std::chrono::duration<double> seconds);
