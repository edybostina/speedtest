#pragma once

#include <chrono>
#include <cstdint>

double megabits_per_second(std::uint64_t bytes, std::chrono::duration<double> seconds);
